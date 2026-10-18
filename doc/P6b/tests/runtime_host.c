/* Executes the actual infinite HealthTask with a deterministic task/tick
 * simulator. HAL calls record decisions only; no board/watchdog IO. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "actuator.h"
#define P6B_RUNTIME_HOST 1
uint32_t host_tick;
int host_scheduler;
bool host_key0, host_key1, host_i2c_fail;
uint32_t host_tx_calls;
#include "../../../App/health/health_runtime.c"
HostDwt host_dwt;
HostDebug host_debug;
HostSysTick host_systick;
HostAfio host_afio;
static jmp_buf end_task;
static void (*created_task)(void *);
static ControlDiagnostics diagnostics;
static HealthBootSnapshot boot;
static ControlStatusSnapshot control_status;
static ActuatorSnapshot actuator;
static unsigned step, finish_at, init_calls, refresh_calls, last_refresh_step;
static unsigned progress_limit, freeze_begin, freeze_end, gap_step, gap_ms;
static unsigned init_delay, suspended_step, refresh_failure_step;
static bool init_fails, create_fails;
static uint32_t tick_origin;

TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name,
    uint32_t size, void *arg, uint32_t priority, StackType_t *stack, StaticTask_t *tcb)
{
    assert(strcmp(name, "HealthTask") == 0 && size == 256U && priority == 0U);
    assert(arg == NULL && stack == s_health_stack && tcb == &s_health_tcb);
    created_task = fn; return create_fails ? NULL : tcb;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{ assert(task == NULL); return 190U; }
void Control_GetDiagnostics(ControlDiagnostics *out) { *out = diagnostics; }
void Health_GetBootSnapshot(HealthBootSnapshot *out) { *out = boot; }
void Control_GetStatus(ControlStatusSnapshot *out) { *out = control_status; }
void Actuator_GetSnapshot(ActuatorSnapshot *out) { *out = actuator; }
int HAL_IWDG_Init(IWDG_HandleTypeDef *wdg)
{
    assert(step >= 2U && host_scheduler == taskSCHEDULER_RUNNING);
    assert(s_runtime.watchdog == HEALTH_WDG_STARTING);
    assert(init_calls++ == 0U && wdg->Instance == IWDG);
    assert(wdg->Init.Prescaler == IWDG_PRESCALER_64 && wdg->Init.Reload == 1249U);
    host_tick += init_delay; return init_fails ? HAL_ERROR : HAL_OK;
}
int HAL_IWDG_Refresh(IWDG_HandleTypeDef *wdg)
{
    assert(wdg == &s_iwdg && init_calls == 1U && !init_fails);
    assert(host_scheduler == taskSCHEDULER_RUNNING);
    assert(s_runtime.control_progress != HEALTH_CTRL_STALLED);
    assert(!s_runtime.test_stop_active);
    assert((uint32_t)(host_tick - s_runtime.last_progress_tick) <= 400U);
    refresh_calls++; last_refresh_step = step;
    return step == refresh_failure_step ? HAL_ERROR : HAL_OK;
}
BaseType_t xTaskDelayUntil(TickType_t *tick, TickType_t delay)
{
    HealthRuntimeSnapshot out;
    assert(delay == 100U); Health_GetRuntimeSnapshot(&out);
    assert(out.min_stack_words == 190U);
    if (step++ >= finish_at) { longjmp(end_task, 1); }
    *tick += delay; host_tick += delay;
    if (step == gap_step) { host_tick += gap_ms; }
    if (step <= progress_limit && !(step >= freeze_begin && step <= freeze_end))
    { diagnostics.wake_count += 5U; }
    if (step == suspended_step) { host_scheduler = 1; }
    else { host_scheduler = taskSCHEDULER_RUNNING; }
    return 1; /* pdTRUE, no port/hardware scheduling */
}
static void Fixture(void)
{
    memset(&s_runtime, 0, sizeof(s_runtime)); memset(&diagnostics, 0, sizeof(diagnostics));
    memset(&boot, 0, sizeof(boot)); memset(&control_status, 0, sizeof(control_status));
    memset(&actuator, 0, sizeof(actuator)); actuator.initialized = true;
    s_health_handle = NULL; created_task = NULL;
    step = 0U; finish_at = 10U; init_calls = 0U; refresh_calls = 0U;
    last_refresh_step = 0U; progress_limit = 1000U;
    freeze_begin = 1001U; freeze_end = 1002U; gap_step = 1001U; gap_ms = 0U;
    suspended_step = 1001U; refresh_failure_step = 1001U;
    init_delay = 0U; init_fails = false; create_fails = false;
    host_tick = tick_origin; host_scheduler = taskSCHEDULER_NOT_STARTED;
}
static void Run(void)
{
    assert(Health_InitTask()); assert(!Health_InitTask());
    assert(created_task == HealthTask && init_calls == 0U && refresh_calls == 0U);
    host_scheduler = taskSCHEDULER_RUNNING;
    if (setjmp(end_task) == 0) { created_task(NULL); assert(false); }
}
int main(void)
{
    unsigned expected;
    Fixture(); Run();
    assert(init_calls == 1U && refresh_calls == 8U);
    assert(s_runtime.control_progress == HEALTH_CTRL_RUNNING && s_runtime.watchdog == HEALTH_WDG_RUNNING);
    Fixture(); finish_at = 1U; Run(); assert(init_calls == 0U); /* baseline + one interval */
    Fixture(); progress_limit = 0U; Run();
    assert(init_calls == 0U && refresh_calls == 0U && s_runtime.control_progress == HEALTH_CTRL_STALLED);
    Fixture(); progress_limit = 3U; Run();
    assert(last_refresh_step == 6U && s_runtime.control_progress == HEALTH_CTRL_STALLED);
    Fixture(); freeze_begin = 4U; freeze_end = 7U; finish_at = 15U; Run();
    assert(last_refresh_step == 6U && s_runtime.control_progress == HEALTH_CTRL_STALLED);
    /* Progress resumes at step8, but a latched >300ms stall never re-feeds. */
    Fixture(); gap_step = 5U; gap_ms = 301U; Run();
    assert(last_refresh_step == 4U && s_runtime.control_progress == HEALTH_CTRL_STALLED);
    Fixture(); init_fails = true; Run();
    assert(init_calls == 1U && refresh_calls == 0U && s_runtime.watchdog == HEALTH_WDG_START_ERROR);
    Fixture(); init_delay = 301U; Run();
    assert(init_calls == 1U && refresh_calls == 0U && s_runtime.control_progress == HEALTH_CTRL_STALLED);
    Fixture(); suspended_step = 5U; Run();
    assert(last_refresh_step == 4U && s_runtime.control_progress == HEALTH_CTRL_STALLED);
    Fixture(); refresh_failure_step = 4U; Run();
    assert(last_refresh_step == 4U && s_runtime.watchdog == HEALTH_WDG_START_ERROR);
    tick_origin = 0xFFFFFF00U;
    Fixture(); diagnostics.wake_count = 0xFFFFFFFAU; Run();
    assert(init_calls == 1U && refresh_calls == 8U && s_runtime.control_progress == HEALTH_CTRL_RUNNING);
    tick_origin = 0U;
    Fixture(); diagnostics.mode = CONTROL_FAULT; diagnostics.fault = CONTROL_FAULT_DRIVER;
    boot.persistent_state = HEALTH_STORE_IWDG_LOCKED; Run();
    assert(init_calls == 1U && refresh_calls == 8U); /* safe FAULT/lock does not stop feeding */
    Fixture(); create_fails = true; assert(!Health_InitTask() && !init_calls);

    Fixture(); finish_at = 70U; Run();
#if P6_WDG_TEST_ENABLE
    assert(s_runtime.test_stop_active && last_refresh_step == 51U);
    for (expected = 0U; expected < 4U; expected++)
    {
        Fixture(); finish_at = 70U;
        if (expected == 0U) { boot.reset_reason = HEALTH_RESET_IWDG; }
        if (expected == 1U) { boot.persistent_state = HEALTH_STORE_IWDG_LOCKED; }
        if (expected == 2U) { boot.persistent_state = HEALTH_STORE_MANUAL_ONLY; }
        if (expected == 3U) { control_status.state = P3B_WAIT_REMOTE; }
        Run(); assert(!s_runtime.test_stop_active && last_refresh_step == 70U);
    }
#else
    expected = 68U;
    assert(!s_runtime.test_stop_active && refresh_calls == expected && last_refresh_step == 70U);
#endif
    printf("P6b runtime assertions PASS (T2=%d): warmup, stall latch, gaps, wrap, safe FAULT, HAL errors, test guards; hardware NOT RUN\n", P6_WDG_TEST_ENABLE);
    return 0;
}
