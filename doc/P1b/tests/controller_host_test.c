/* Executes real control.c and p1b_test.c against explicit RTOS/hardware stubs.
 * This checks decisions and calls; it is NOT a Cortex-M/FreeRTOS simulation,
 * timing measurement, stack measurement, or test of the protected PWM driver.
 */
#include "control.h"
#include "control_config.h"
#include "stm32f1xx_hal.h"
#include "actuator.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

HostDwt host_dwt;
HostDebug host_debug;
HostSysTick host_systick = {7U, 71999U};
uint32_t SystemCoreClock = 72000000U;
static jmp_buf finish;
static void (*created_task)(void *);
static uint32_t now_tick, initial_tick, delays, enables, applies, disables, forces;
static uint32_t enabled_tick, enabled_cycle;
static ActuatorSnapshot state;
static const char *scenario;

void HostAssert(int condition, int line)
{
    if (!condition) { fprintf(stderr, "firmware assertion at line %d\n", line); }
    assert(condition);
}
void HostNop(void) { host_dwt.CYCCNT++; }
uint32_t HAL_NVIC_GetPriorityGrouping(void) { return NVIC_PRIORITYGROUP_4; }
TickType_t xTaskGetTickCount(void) { return now_tick; }
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
TaskHandle_t xTaskGetIdleTaskHandle(void) { return (void *)2; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    host_dwt.CYCCNT += 20U;
    return task ? 100U : 180U; /* synthetic fixture, never hardware evidence */
}
TaskHandle_t xTaskCreateStatic(void (*task)(void *), const char *name,
                              uint32_t words, void *arg, UBaseType_t priority,
                              StackType_t *stack, StaticTask_t *tcb)
{
    (void)name; (void)arg;
    assert(words == 256U && priority == 1U && stack && tcb);
    if (strcmp(scenario, "create_fail") == 0) { return NULL; }
    assert(created_task == NULL);
    created_task = task;
    return tcb;
}
BaseType_t xTaskDelayUntil(TickType_t *previous, TickType_t period)
{
    TickType_t target = *previous + period;
    uint32_t delta = target - now_tick;
    *previous = target;
    if (++delays > 1600U) { longjmp(finish, 1); }
    if ((strcmp(scenario, "no_wait") == 0) && (enables != 0U) &&
        ((uint32_t)(now_tick - initial_tick) >= 6000U) && !state.fault_latched)
    {
        now_tick += 40U;
        host_dwt.CYCCNT += 40U * 72000U;
        return pdFALSE;
    }
    if ((delta == 0U) || (delta >= 0x80000000U)) { return pdFALSE; }
    now_tick = target;
    host_dwt.CYCCNT += delta * 72000U;
    return pdTRUE;
}
void Actuator_GetSnapshot(ActuatorSnapshot *out)
{
    if ((strcmp(scenario, "external_fault") == 0) && enables &&
        ((uint32_t)(now_tick - initial_tick) >= 6000U))
    {
        state.fault_latched = true;
        state.enabled = false;
    }
    host_dwt.CYCCNT += 20U;
    *out = state;
}
bool Actuator_Enable(void)
{
    assert(!state.enabled && !state.fault_latched && enables == 0U);
    assert((uint32_t)(now_tick - initial_tick) >= 2000U);
    enables++;
    if (strcmp(scenario, "refuse_enable") == 0) { return false; }
    state.enabled = true;
    enabled_tick = now_tick;
    enabled_cycle = host_dwt.CYCCNT;
    return true;
}
bool Actuator_Apply(const ActuatorCommand *command, ActuatorApplyResult *result)
{
    assert(state.enabled && !state.fault_latched);
    assert((uint32_t)(now_tick - enabled_tick) >= 40U);
    assert((uint32_t)(host_dwt.CYCCNT - enabled_cycle) >= 40U * 72000U);
    assert(command->pan_position_us >= 1450 && command->pan_position_us <= 1550);
    assert(command->tilt_position_us >= 1450 && command->tilt_position_us <= 1550);
    applies++;
    memset(result, 0, sizeof(*result));
    return strcmp(scenario, "refuse_apply") != 0;
}
void Actuator_Disable(void) { state.enabled = false; disables++; }
void Actuator_ForceSafeOutput(void)
{
    state.enabled = false;
    state.fault_latched = true;
    forces++;
}

int main(int argc, char **argv)
{
    ControlDiagnostics diagnostics;
    ControlFault expected = CONTROL_FAULT_NONE;
    scenario = (argc > 1) ? argv[1] : "nominal";
    state.initialized = true;
    initial_tick = now_tick = (strcmp(scenario, "rollover") == 0) ? 0xfffffff0U : 0U;
    if (strcmp(scenario, "no_counter") == 0) { host_dwt.CTRL = DWT_CTRL_NOCYCCNT_Msk; }
    if (strcmp(scenario, "create_fail") == 0)
    {
        assert(!Control_Init());
        assert(enables == 0U && applies == 0U);
        puts("PASS: static task creation failure, no output");
        return 0;
    }
    assert(Control_Init());
    assert(!Control_Init()); /* no second task */
    if (setjmp(finish) == 0) { created_task(NULL); }
    Control_GetDiagnostics(&diagnostics);
#if P1B_AUTOTEST_ENABLE
#if P1B_AUTOTEST_CASE == 2
    expected = CONTROL_FAULT_EXPIRED;
#elif P1B_AUTOTEST_CASE == 3
    expected = CONTROL_FAULT_DEADLINE;
#elif P1B_AUTOTEST_CASE == 4
    expected = CONTROL_FAULT_EXECUTION;
#endif
    if (strcmp(scenario, "external_fault") == 0) { expected = CONTROL_FAULT_EXTERNAL; }
    if (strcmp(scenario, "no_wait") == 0) { expected = CONTROL_FAULT_DEADLINE; }
    if ((strcmp(scenario, "refuse_enable") == 0) || (strcmp(scenario, "refuse_apply") == 0))
    {
        expected = CONTROL_FAULT_DRIVER;
    }
#endif
    if (strcmp(scenario, "no_counter") == 0) { expected = CONTROL_FAULT_CLOCK; }
    assert(diagnostics.fault == expected);
    assert(!state.enabled);
    if (expected != CONTROL_FAULT_NONE)
    {
        assert(diagnostics.mode == CONTROL_FAULT && state.fault_latched && forces >= 1U);
        assert(disables == 0U); /* errors use latching emergency close */
    }
#if P1B_AUTOTEST_ENABLE
    else
    {
        assert(diagnostics.mode == CONTROL_COMPLETE && disables == 1U && forces == 0U);
        assert(enables == 1U && applies > 1000U && diagnostics.accepted_count == 1400U);
    }
    assert(enables <= 1U); /* terminal loops cannot reauthorize/re-enable */
#else
    else { assert(diagnostics.mode == CONTROL_DISABLED); }
    assert(enables == 0U && applies == 0U && disables == 0U);
#endif
    printf("PASS: real controller case=%d scenario=%s enables=%u applies=%u fault=%d terminal-safe\n",
           P1B_AUTOTEST_CASE, scenario, enables, applies, (int)expected);
    return 0;
}
