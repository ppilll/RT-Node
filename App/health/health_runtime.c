#include "health_app.h"
#include "health_config.h"
#include "../control/control.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#if P6_WDG_TEST_ENABLE
#include "../actuator/include/actuator.h"
#endif

#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0) || \
    (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "P6 requires static tasks and a 32-bit 1 kHz tick"
#endif

static StaticTask_t s_health_tcb;
static StackType_t s_health_stack[HEALTH_STACK_WORDS];
static TaskHandle_t s_health_handle;
static IWDG_HandleTypeDef s_iwdg;
static HealthRuntimeSnapshot s_runtime;
static void HealthTask(void *argument);

bool Health_InitTask(void)
{
    if (s_health_handle != NULL || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) { return false; }
    s_runtime.min_stack_words = HEALTH_STACK_WORDS;
    s_health_handle = xTaskCreateStatic(HealthTask, "HealthTask", HEALTH_STACK_WORDS,
                                       NULL, tskIDLE_PRIORITY,
                                       s_health_stack, &s_health_tcb);
    return s_health_handle != NULL;
}

void Health_GetRuntimeSnapshot(HealthRuntimeSnapshot *out)
{
    if (out == NULL) { return; }
    configASSERT(__get_IPSR() == 0U);
    taskENTER_CRITICAL();
    *out = s_runtime;
    taskEXIT_CRITICAL();
}

#if P6_WDG_TEST_ENABLE
/* After the IWDG boot records LOCK, subsequent IWDG/NRST/POR boots cannot
 * re-inject. MANUAL/BAD also cannot inject. No actuator writes in this task. */
static bool TestCanStop(uint32_t now, uint32_t started)
{
    HealthBootSnapshot boot;
    ControlStatusSnapshot control;
    ActuatorSnapshot actuator;
    Health_GetBootSnapshot(&boot);
    Control_GetStatus(&control);
    Actuator_GetSnapshot(&actuator);
    return (uint32_t)(now - started) >= 5000U &&
           boot.reset_reason != HEALTH_RESET_IWDG &&
           (boot.persistent_state == HEALTH_STORE_EMPTY_LOCKED ||
            boot.persistent_state == HEALTH_STORE_AUTO) &&
           control.state == P3B_DISABLED && !control.enabled &&
           actuator.initialized && !actuator.enabled && !actuator.fault_latched;
}
#endif

static void HealthTask(void *argument)
{
    HealthRuntimeSnapshot next = {HEALTH_CTRL_INIT, HEALTH_WDG_OFF, 0U, 0U,
                                   HEALTH_STACK_WORDS, false};
    ControlDiagnostics control;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t previous_wake = 0U, previous_sample = 0U;
    uint32_t good_intervals = 0U;
    bool baseline = false, stalled = false, start_attempted = false;
#if P6_WDG_TEST_ENABLE
    uint32_t started_tick = 0U;
#endif
    (void)argument;
    next.min_stack_words = HEALTH_STACK_WORDS;
    for (;;)
    {
        uint32_t now;
        UBaseType_t free_words;
        Control_GetDiagnostics(&control); /* Only wake_count supervises production. */
        now = xTaskGetTickCount();
        if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) { stalled = true; }
        if (!baseline)
        {
            baseline = true;
            next.last_progress_tick = now;
        }
        else
        {
            /* Check gaps BEFORE accepting a fresh counter. A delayed HealthTask
             * must not hide a >300 ms supervision gap by seeing a new value. */
            if ((uint32_t)(now - previous_sample) > HEALTH_STALL_TICKS ||
                (uint32_t)(now - next.last_progress_tick) > HEALTH_STALL_TICKS)
            { stalled = true; }
            if (control.wake_count != previous_wake)
            {
                next.last_progress_tick = now;
                if ((uint32_t)(now - previous_sample) >= HEALTH_PERIOD_TICKS)
                { if (good_intervals < 2U) { good_intervals++; } }
                else { good_intervals = 0U; }
                next.control_progress = HEALTH_CTRL_RUNNING;
            }
            else { good_intervals = 0U; }
        }
        previous_wake = control.wake_count;
        previous_sample = now;
        next.last_control_wake = control.wake_count;
        if (stalled) { next.control_progress = HEALTH_CTRL_STALLED; }

        if (!stalled && !start_attempted && good_intervals >= 2U)
        {
            HAL_StatusTypeDef status;
            start_attempted = true; /* HAL starts hardware before it can fail. */
            s_iwdg.Instance = IWDG;
            s_iwdg.Init.Prescaler = IWDG_PRESCALER_64;
            s_iwdg.Init.Reload = 1249U;
            /* UART can run while HAL waits for SR. Publish START before HAL
             * turns on hardware so this interval is never reported as OFF. */
            next.watchdog = HEALTH_WDG_STARTING;
            taskENTER_CRITICAL();
            s_runtime = next;
            taskEXIT_CRITICAL();
            status = HAL_IWDG_Init(&s_iwdg);
            next.watchdog = status == HAL_OK ? HEALTH_WDG_RUNNING : HEALTH_WDG_START_ERROR;
            /* HAL init itself reloads once on success. Do not add a refresh in
             * that same sample, or report OFF after an init timeout. */
            now = xTaskGetTickCount();
            if ((uint32_t)(now - next.last_progress_tick) > HEALTH_STALL_TICKS)
            { stalled = true; next.control_progress = HEALTH_CTRL_STALLED; }
#if P6_WDG_TEST_ENABLE
            started_tick = now;
#endif
        }
        else if (!stalled && next.watchdog == HEALTH_WDG_RUNNING)
        {
#if P6_WDG_TEST_ENABLE
            if (!next.test_stop_active && TestCanStop(now, started_tick))
            { next.test_stop_active = true; }
#endif
            if (!next.test_stop_active &&
                xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
                (uint32_t)(xTaskGetTickCount() - next.last_progress_tick) <= HEALTH_STALL_TICKS)
            {
                /* Sole application refresh site, only in HealthTask. */
                if (HAL_IWDG_Refresh(&s_iwdg) != HAL_OK)
                { next.watchdog = HEALTH_WDG_START_ERROR; }
            }
        }
        free_words = uxTaskGetStackHighWaterMark(NULL);
        if (free_words < next.min_stack_words) { next.min_stack_words = free_words; }
        taskENTER_CRITICAL();
        s_runtime = next;
        taskEXIT_CRITICAL();
        if (xTaskDelayUntil(&last_wake, HEALTH_PERIOD_TICKS) == pdFALSE)
        { last_wake = xTaskGetTickCount(); } /* No catch-up progress credit. */
    }
}
