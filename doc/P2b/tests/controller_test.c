/* Runs the real production ControlTask via its registered function pointer.
 * HAL/RTOS/Actuator stubs check decisions and call order, NOT physical PWM/UIF,
 * Cortex-M scheduling, WCET, stack margin or electrical safety.
 */
#include "control.h"
#include "control_config.h"
#include "imu.h"
#include "stm32f1xx_hal.h"
#include "actuator.h"
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

HostDwt host_dwt;
HostDebug host_debug;
HostSysTick host_systick = {7U, 71999U};
uint32_t SystemCoreClock = 72000000U;
static jmp_buf finish;
static void (*created_task)(void *);
static const char *scenario;
static uint32_t now_tick, initial_tick, delays, enables, applies, disables, forces;
static uint32_t enabled_tick, enabled_cycle, first_apply_age, key_init, clocks;
static int32_t last_pan = 1500, last_tilt = 1500;
static ActuatorSnapshot state;
static bool Is(const char *name) { return strcmp(scenario, name) == 0; }
static uint32_t Age(void) { return now_tick - initial_tick; }

void HostAssert(int condition, int line)
{
    if (!condition) { fprintf(stderr, "firmware assertion at line %d\n", line); }
    assert(condition);
}
void HostNop(void) { host_dwt.CYCCNT++; }
void HostGpioClock(void) { clocks++; }
uint32_t HAL_NVIC_GetPriorityGrouping(void) { return NVIC_PRIORITYGROUP_4; }
TickType_t xTaskGetTickCount(void) { return now_tick; }
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
TaskHandle_t xTaskGetIdleTaskHandle(void) { return (void *)2; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    host_dwt.CYCCNT += Is("execution") && Age() == 260U ? 144000U : 20U;
    return task ? 100U : 180U; /* synthetic fixture */
}
TaskHandle_t xTaskCreateStatic(void (*task)(void *), const char *name,
                              uint32_t words, void *arg, UBaseType_t priority,
                              StackType_t *stack, StaticTask_t *tcb)
{
    (void)name; (void)arg;
    assert(words == 256U && priority == 1U && stack && tcb && !created_task);
    created_task = task;
    return tcb;
}
BaseType_t xTaskDelayUntil(TickType_t *previous, TickType_t period)
{
    TickType_t target = *previous + period;
    uint32_t delta = target - now_tick;
    assert(period == 20U);
    if (++delays > 60U) { longjmp(finish, 1); }
    *previous = target;
    now_tick = target;
    host_dwt.CYCCNT += delta * 72000U;
    if (Is("prime_dwt") && Age() == 220U) { host_dwt.CYCCNT -= 72000U; }
    if (Is("prime_dwt") && Age() == 240U) { host_dwt.CYCCNT += 72000U; }
    if (Is("deadline") && Age() == 260U) { return pdFALSE; }
    if (Is("clock") && Age() == 260U) { host_dwt.CYCCNT += 4U * 72000U; }
    return pdTRUE;
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    assert(clocks == 1U && port == GPIOE);
    assert(gpio->Pin == (GPIO_PIN_3 | GPIO_PIN_4));
    assert(gpio->Mode == GPIO_MODE_INPUT && gpio->Pull == GPIO_PULLUP);
    key_init++;
}
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
    uint32_t t = Age();
    bool pressed;
    assert(port == GPIOE && key_init == 1U);
    if (pin == GPIO_PIN_3)
    {
        pressed = (t >= 400U && t <= 440U) || (Is("key1_bounce") && t == 300U) ||
                  (Is("stop_before_arm") && t >= 120U && t <= 180U) ||
                  (Is("both_keys") && t >= 160U && t <= 180U);
    }
    else
    {
        assert(pin == GPIO_PIN_4);
        pressed = (t >= 160U && t <= 180U) || (t >= 500U && t <= 560U);
        if (Is("no_key")) { pressed = false; }
        if (Is("boot_hold")) { pressed = t <= 200U; }
        if (Is("boot_release_press"))
        { pressed = t <= 200U || (t >= 280U && t <= 300U) || t >= 500U; }
        if (Is("pre_ready_press"))
        { pressed = (t >= 60U && t <= 200U) || (t >= 280U && t <= 300U) || t >= 500U; }
        if (Is("key0_bounce"))
        { pressed = t == 160U || (t >= 240U && t <= 260U) || t >= 500U; }
    }
    return pressed ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
bool Imu_GetSnapshot(ImuAttitudeSnapshot *out)
{
    uint32_t t = Age();
    out->state = t < 100U ? IMU_CALIBRATING : IMU_READY;
    out->valid = t >= 100U;
    out->roll_deg = 10.0f + (enables ? 100.0f : 0.0f);
    out->pitch_deg = -5.0f - (enables ? 100.0f : 0.0f);
    out->sample_tick = now_tick - 1U;
    out->generation = 0xfffffff0U + t / 10U; /* real session sees wrap */
    if (Is("failed_before_arm")) { out->state = IMU_FAILED; out->valid = false; }
    if (t == 260U)
    {
        if (Is("failed")) { out->state = IMU_FAILED; }
        if (Is("invalid")) { out->valid = false; }
        if (Is("not_ready")) { out->state = IMU_CALIBRATING; }
        if (Is("stale")) { out->sample_tick = now_tick - 40U; }
        if (Is("fresh39")) { out->sample_tick = now_tick - 39U; }
        if (Is("future")) { out->sample_tick = now_tick + 1U; }
        if (Is("duplicate")) { out->generation -= 2U; }
        if (Is("nan")) { out->roll_deg = NAN; }
        if (Is("inf")) { out->pitch_deg = INFINITY; }
        if (Is("unavailable")) { return false; }
    }
    if (Is("prime_nan") && t == 200U) { out->roll_deg = NAN; }
    return true;
}
void Actuator_GetSnapshot(ActuatorSnapshot *out)
{
    if (Is("external") && Age() == 260U)
    { state.fault_latched = true; state.enabled = false; }
    host_dwt.CYCCNT += 20U;
    *out = state;
}
bool Actuator_Enable(void)
{
    uint32_t expected = 180U;
    if (Is("boot_release_press") || Is("pre_ready_press")) { expected = 300U; }
    if (Is("key0_bounce")) { expected = 260U; }
    assert(Age() == expected && enables == 0U && !state.enabled && !state.fault_latched);
    enables++;
    if (Is("refuse_enable")) { return false; }
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
    assert(command->pan_position_us >= 1455 && command->pan_position_us <= 1545);
    assert(command->tilt_position_us >= 1455 && command->tilt_position_us <= 1545);
    assert(command->pan_position_us - last_pan <= 2 && last_pan - command->pan_position_us <= 2);
    assert(command->tilt_position_us - last_tilt <= 2 && last_tilt - command->tilt_position_us <= 2);
    last_pan = command->pan_position_us; last_tilt = command->tilt_position_us;
    if (applies == 0U) { first_apply_age = now_tick - enabled_tick; }
    applies++;
    memset(result, 0, sizeof(*result));
    return !Is("refuse_apply");
}
void Actuator_Disable(void) { state.enabled = false; disables++; }
void Actuator_ForceSafeOutput(void)
{ state.enabled = false; state.fault_latched = true; forces++; }

int main(int argc, char **argv)
{
    ControlDiagnostics d;
    ControlFault expected = CONTROL_FAULT_NONE;
    scenario = argc > 1 ? argv[1] : "nominal";
    initial_tick = now_tick = Is("rollover") ? 0xffffff00U : 0U;
    state.initialized = true;
    assert(Control_Init()); assert(!Control_Init());
    if (setjmp(finish) == 0) { created_task(NULL); }
    Control_GetDiagnostics(&d);
#if P2B_OUTPUT_STAGE > 0
    if (Is("stale") || Is("future") || Is("invalid") || Is("failed") ||
        Is("not_ready") || Is("nan") || Is("inf") || Is("prime_nan") ||
        Is("unavailable") || Is("failed_before_arm")) { expected = CONTROL_FAULT_COMMAND; }
    if (Is("duplicate")) { expected = CONTROL_FAULT_GENERATION; }
    if (Is("refuse_enable") || Is("refuse_apply")) { expected = CONTROL_FAULT_DRIVER; }
    if (Is("external")) { expected = CONTROL_FAULT_EXTERNAL; }
    if (Is("deadline")) { expected = CONTROL_FAULT_DEADLINE; }
    if (Is("execution")) { expected = CONTROL_FAULT_EXECUTION; }
    if (Is("clock")) { expected = CONTROL_FAULT_CLOCK; }
    assert(key_init == 1U && clocks == 1U);
    assert(!state.enabled && enables <= 1U && d.fault == expected);
    if (expected != CONTROL_FAULT_NONE)
    { assert(d.mode == CONTROL_FAULT && forces == 1U && disables == 0U && state.fault_latched); }
    else
    { assert(d.mode == CONTROL_COMPLETE && disables == 1U && forces == 0U); }
    if (Is("no_key") || Is("boot_hold") || Is("stop_before_arm") ||
        Is("both_keys") || Is("failed_before_arm")) { assert(enables == 0U && applies == 0U); }
    else
    {
        assert(enables == 1U);
        if (!Is("refuse_enable") && !Is("prime_nan"))
        { assert(applies > 0U && first_apply_age == (Is("prime_dwt") ? 60U : 40U)); }
    }
#else
    assert(d.mode == CONTROL_DISABLED && d.fault == expected);
    assert(enables == 0U && applies == 0U && disables == 0U && forces == 0U);
    assert(key_init == 0U && clocks == 0U);
#endif
    printf("PASS U03 real ControlTask stage=%d scenario=%s enables=%u applies=%u first_apply_age=%u fault=%d\n",
           P2B_OUTPUT_STAGE, scenario, enables, applies, first_apply_age, (int)expected);
    return 0;
}
