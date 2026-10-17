#include "control.h"
#include "control_config.h"
#include "../p1b_test/p1b_test.h"
#include "../actuator/include/actuator.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>

#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0)
#error "Control snapshots require 32-bit, 1 kHz RTOS ticks"
#endif
#if (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "ControlTask requires static-only RTOS allocation"
#endif

#define CYCLES_PER_US 72U
#define CYCLES_PER_TICK 72000U
typedef char TickWidthMustBe32[(sizeof(TickType_t) == sizeof(uint32_t)) ? 1 : -1];

static StaticTask_t s_control_tcb;
static StackType_t s_control_stack[CONTROL_STACK_WORDS];
static TaskHandle_t s_control_handle;
static volatile ControlDiagnostics s_diagnostics;
static void ControlTask(void *argument);

bool Control_Init(void)
{
    if (s_control_handle != NULL) { return false; }
    s_control_handle = xTaskCreateStatic(ControlTask, "ControlTask", CONTROL_STACK_WORDS,
                                        NULL, tskIDLE_PRIORITY + 1U,
                                        s_control_stack, &s_control_tcb);
    return s_control_handle != NULL;
}

void Control_GetDiagnostics(ControlDiagnostics *out)
{
    if (out == NULL) { return; }
    configASSERT(__get_IPSR() == 0U);
    taskENTER_CRITICAL();
    *out = s_diagnostics;
    taskEXIT_CRITICAL();
}

static void Stop(ControlSession *session, ControlFault reason)
{
    /* Hardware first. A normal finite test end does not invent a fault.
     * Every error latches in the existing driver, before touching diagnostics.
     */
    if (reason == CONTROL_FAULT_NONE) { Actuator_Disable(); }
    else { Actuator_ForceSafeOutput(); }
    ControlSession_Close(session, reason);
    s_diagnostics.mode = (reason == CONTROL_FAULT_NONE) ? CONTROL_COMPLETE : CONTROL_FAULT;
    s_diagnostics.fault = session->fault;
    s_diagnostics.generation_fence = session->last_generation;
}

static bool StartCycleCounter(void)
{
    uint32_t before;
    uint32_t n;
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U) { return false; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    before = DWT->CYCCNT;
    for (n = 0U; n < 128U; n++) { __NOP(); }
    return ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) && (DWT->CYCCNT != before);
}

static void SampleStack(void)
{
    UBaseType_t control = uxTaskGetStackHighWaterMark(NULL);
    UBaseType_t idle = uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle());
    if (control < s_diagnostics.min_control_stack_words)
    {
        s_diagnostics.min_control_stack_words = control;
    }
    if (idle < s_diagnostics.min_idle_stack_words)
    {
        s_diagnostics.min_idle_stack_words = idle;
    }
}

#if P1B_AUTOTEST_ENABLE
static bool Accept(ControlSession *session, const ControlCommandSnapshot *command,
                   TickType_t now)
{
    if (!ControlSession_Accept(session, command, now))
    {
        Stop(session, session->fault);
        return false;
    }
    s_diagnostics.last_accepted_generation = command->generation;
    s_diagnostics.generation_fence = session->last_generation;
    s_diagnostics.accepted_count++;
    return true;
}
#endif

static void ControlTask(void *argument)
{
    ControlSession session = {0};
    ActuatorSnapshot state;
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t previous_wake = last_wake;
    uint32_t previous_cycle = 0U;
    bool has_previous = false;
#if P1B_AUTOTEST_ENABLE
    TickType_t boot_tick = last_wake;
    TickType_t enabled_tick = 0U;
    uint32_t enabled_cycle = 0U;
    uint32_t step = 0U;
    ControlCommandSnapshot command;
    ActuatorCommand actuator_command;
    ActuatorApplyResult result;
#endif
    (void)argument;

    /* Retain the reviewed C1 first-task checks after SVC restores PSP/BASEPRI. */
    configASSERT(__get_IPSR() == 0U);
    configASSERT(__get_PRIMASK() == 0U);
    configASSERT(__get_BASEPRI() == 0U);
    configASSERT(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    configASSERT(HAL_NVIC_GetPriorityGrouping() == NVIC_PRIORITYGROUP_4);
    configASSERT(SystemCoreClock == 72000000U);
    configASSERT(SysTick->LOAD == (SystemCoreClock / configTICK_RATE_HZ) - 1U);
    Actuator_GetSnapshot(&state);
    configASSERT(state.initialized && !state.enabled && !state.fault_latched);
    s_diagnostics.min_control_stack_words = CONTROL_STACK_WORDS;
    s_diagnostics.min_idle_stack_words = configMINIMAL_STACK_SIZE;
#if P1B_AUTOTEST_ENABLE
    s_diagnostics.mode = CONTROL_PRESTART;
#endif
    if (!StartCycleCounter()) { Stop(&session, CONTROL_FAULT_CLOCK); }
    /* Counter initialization is before the first scheduling epoch. */
    last_wake = xTaskGetTickCount();
    previous_wake = last_wake;

    for (;;)
    {
        uint32_t entry_cycle = DWT->CYCCNT;
        TickType_t now;
        uint32_t elapsed_cycles;
        uint32_t period;
#if P1B_AUTOTEST_ENABLE
        if (!session.terminal && session.authorized)
        {
            P1bTest_InjectLateWake(step);
        }
#endif
        now = xTaskGetTickCount();
        period = (uint32_t)(now - previous_wake);
        s_diagnostics.wake_count++;
        s_diagnostics.last_period_ticks = has_previous ? period : 0U;
        if (!session.terminal)
        {
            if (((TickType_t)(now - last_wake) > CONTROL_MAX_LATE_TICKS) ||
                (has_previous && ((period < 19U) || (period > 21U))))
            {
                s_diagnostics.late_count++;
                Stop(&session, CONTROL_FAULT_DEADLINE);
            }
            else if (has_previous &&
                (((uint32_t)(entry_cycle - previous_cycle) < (period - 2U) * CYCLES_PER_TICK) ||
                 ((uint32_t)(entry_cycle - previous_cycle) > (period + 2U) * CYCLES_PER_TICK)))
            {
                Stop(&session, CONTROL_FAULT_CLOCK);
            }
            else if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
            {
                Stop(&session, CONTROL_FAULT_CLOCK);
            }
            else
            {
                Actuator_GetSnapshot(&state);
                if (state.fault_latched) { Stop(&session, CONTROL_FAULT_EXTERNAL); }
            }
        }

#if P1B_AUTOTEST_ENABLE
        if (!session.terminal && (s_diagnostics.mode == CONTROL_PRESTART) &&
            ((TickType_t)(now - boot_tick) >= CONTROL_PRESTART_TICKS))
        {
            if (!ControlSession_Authorize(&session)) { Stop(&session, CONTROL_FAULT_MODE); }
            else
            {
                P1bTest_MakeCommand(step, now, &command);
                if (Accept(&session, &command, now))
                {
                    if (!Actuator_Enable()) { Stop(&session, CONTROL_FAULT_DRIVER); }
                    else
                    {
                        enabled_tick = xTaskGetTickCount();
                        enabled_cycle = DWT->CYCCNT;
                        s_diagnostics.mode = CONTROL_PRIMING;
                        step++;
                    }
                }
            }
        }
        else if (!session.terminal && session.authorized)
        {
            if (step >= P1B_TEST_ACTIVE_STEPS) { Stop(&session, CONTROL_FAULT_NONE); }
            else
            {
                P1bTest_MakeCommand(step, now, &command);
                if (Accept(&session, &command, now))
                {
                    /* >=40 actual ms, checked with both clocks, before Apply.
                     * No TIM3 access here. The driver still rejects missing UIF.
                     */
                    if (((TickType_t)(now - enabled_tick) >= CONTROL_PRIMING_TICKS) &&
                        ((uint32_t)(DWT->CYCCNT - enabled_cycle) >=
                         CONTROL_PRIMING_TICKS * CYCLES_PER_TICK))
                    {
                        s_diagnostics.mode = CONTROL_TEST;
                        actuator_command.pan_position_us = command.pan_us;
                        actuator_command.tilt_position_us = command.tilt_us;
                        if (!Actuator_Apply(&actuator_command, &result))
                        {
                            Stop(&session, CONTROL_FAULT_DRIVER);
                        }
                        else { s_diagnostics.applied_count++; }
                    }
                    P1bTest_InjectExecution(step);
                    step++;
                }
            }
        }
#endif
        SampleStack();
        elapsed_cycles = DWT->CYCCNT - entry_cycle;
        if (elapsed_cycles > s_diagnostics.worst_execution_cycles)
        {
            s_diagnostics.worst_execution_cycles = elapsed_cycles;
        }
        if (!session.terminal && (elapsed_cycles >= CONTROL_EXECUTION_US * CYCLES_PER_US))
        {
            Stop(&session, CONTROL_FAULT_EXECUTION);
        }
        previous_wake = now;
        previous_cycle = entry_cycle;
        has_previous = true;
        if (xTaskDelayUntil(&last_wake, CONTROL_PERIOD_TICKS) == pdFALSE)
        {
            if (!session.terminal)
            {
                s_diagnostics.late_count++;
                Stop(&session, CONTROL_FAULT_DEADLINE);
            }
            /* Resynchronize only AFTER terminal shutdown; no control catch-up. */
            last_wake = xTaskGetTickCount();
        }
    }
}
