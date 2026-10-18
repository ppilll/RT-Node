/* Real ControlTask + policy + mapping, synthetic HAL/RTOS/actuator.
 * Decision/order checks only: no PWM, UIF, scheduling or WCET evidence. */
#include "control.h"
#include "control_config.h"
#include "imu.h"
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
static void (*control_task)(void *);
static const char *scenario;
static uint32_t tick, origin, enables, applies, forces, enabled_tick, first_apply;
static uint32_t enable_times[4], apply_at_tick, disabled_at_tick;
static uint16_t seq;
static bool overflow;
static int32_t pan = 1500, tilt = 1500;
static ActuatorSnapshot actuator;
static bool Is(const char *s) { return strcmp(scenario, s) == 0; }
static uint32_t Age(void) { return tick - origin; }
void HostAssert(int condition, int line)
{ if (!condition) { fprintf(stderr, "assert line %d\n", line); } assert(condition); }
void HostNop(void) { host_dwt.CYCCNT++; }
void HostGpioClock(void) { }
uint32_t HAL_NVIC_GetPriorityGrouping(void) { return NVIC_PRIORITYGROUP_4; }
TickType_t xTaskGetTickCount(void) { return tick; }
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
TaskHandle_t xTaskGetIdleTaskHandle(void) { return (void *)2; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{ (void)task; host_dwt.CYCCNT += 20U; return 100U; }
TaskHandle_t xTaskCreateStatic(void (*task)(void *), const char *name,
                              uint32_t words, void *arg, UBaseType_t priority,
                              StackType_t *stack, StaticTask_t *tcb)
{
    (void)name; (void)arg;
    assert(words == 256U && priority == 1U && stack && tcb);
    control_task = task; return tcb;
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{ assert(port == GPIOE && gpio->Pin == (GPIO_PIN_3 | GPIO_PIN_4)); }
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
    uint32_t t = Age();
    bool down = false;
    assert(port == GPIOE);
    if (pin == GPIO_PIN_4)
    {
        if (Is("local")) { down = (t >= 60U && t <= 300U) || t >= 360U; }
        if (Is("switch")) { down = (t >= 160U && t <= 260U) || (t >= 320U && t <= 560U); }
        if (Is("boot_hold")) { down = true; }
        if (Is("local_fault")) { down = t >= 60U; }
    }
    else { assert(pin == GPIO_PIN_3); down = Is("switch") && t >= 720U; }
    return down ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
bool Imu_GetSnapshot(ImuAttitudeSnapshot *out)
{
    out->state = Is("remote_imu_failed") || (Is("local_fault") && Age() >= 160U) ? IMU_FAILED : IMU_READY;
    out->valid = out->state == IMU_READY;
    out->roll_deg = enables ? 5.0f : 0.0f; out->pitch_deg = 0.0f;
    out->sample_tick = tick - 1U; out->generation = tick / 10U;
    return true;
}
bool CanApp_TakeOverflow(void) { bool result = overflow; overflow = false; return result; }
void Actuator_GetSnapshot(ActuatorSnapshot *out) { *out = actuator; }
bool Actuator_Enable(void)
{
    assert(!actuator.enabled && !actuator.fault_latched && enables < 4U);
    assert(disabled_at_tick != Age() || Age() == 80U || Age() == 380U);
    enable_times[enables++] = Age(); enabled_tick = tick;
    actuator.enabled = true; pan = 1500; tilt = 1500; return true;
}
bool Actuator_Apply(const ActuatorCommand *command, ActuatorApplyResult *result)
{
    int32_t dp = command->pan_position_us - pan, dt = command->tilt_position_us - tilt;
    assert(actuator.enabled && !actuator.fault_latched);
    assert((uint32_t)(tick - enabled_tick) >= 40U);
    assert(dp >= -2 && dp <= 2 && dt >= -2 && dt <= 2);
    assert(command->pan_position_us >= 1000 && command->pan_position_us <= 2000);
    assert(command->tilt_position_us >= 1200 && command->tilt_position_us <= 1600);
    if (!applies) { first_apply = tick - enabled_tick; }
    applies++; apply_at_tick = Age();
    pan = command->pan_position_us; tilt = command->tilt_position_us;
    memset(result, 0, sizeof(*result)); return true;
}
void Actuator_Disable(void) { actuator.enabled = false; disabled_at_tick = Age(); }
void Actuator_ForceSafeOutput(void)
{ actuator.enabled = false; actuator.fault_latched = true; forces++; }
static void Mode(uint8_t mode, uint16_t action, uint16_t epoch)
{ Control_PostMode(mode, action, epoch, tick - 1U); }
static void Remote(uint16_t epoch, uint16_t value)
{ Control_PostRemote(epoch, ++seq, value, 1490U, tick - 1U); }

static void CheckCycle(void)
{
    ControlStatusSnapshot s;
    uint32_t t = Age();
    Control_GetStatus(&s);
    assert(s.enabled == actuator.enabled);
#if P3B_OUTPUT_STAGE > 0
    if (Is("local_fault") && t >= 160U)
    { assert(s.state == P3B_FAULT && actuator.fault_latched); return; }
#endif
    if (Is("local") || Is("boot_hold") || Is("local_fault")) { return; }
    if (t == 20U) { assert(s.state == P3B_WAIT_REMOTE && s.epoch == 1U && !s.enabled); }
    if (t == 40U)
    {
#if P3B_OUTPUT_STAGE > 0
        assert(s.state == P3B_PRIMING_REMOTE && s.enabled);
#else
        assert(s.state == P3B_WAIT_REMOTE && !s.enabled);
#endif
    }
    if (Is("duplicate") && t == 100U) { assert(s.epoch == 1U && s.action_accepted); }
    if (Is("mode_errors") && t == 100U) { assert(!s.action_accepted && s.epoch == 1U); }
    if (Is("busy") && t == 20U) { assert(!s.action_accepted && s.last_action == 2U); }
    if (Is("overflow") && t == 120U)
    { assert(s.state == P3B_DISABLED && s.reason == 4U && s.epoch == 2U && apply_at_tick != t); }
    if ((Is("timeout") || Is("invalid")) && t == 180U)
    { assert(s.state == P3B_DISABLED && s.reason == 3U && s.epoch == 2U && apply_at_tick != t); }
    if (Is("stop") && t == 140U)
    { assert(s.state == P3B_DISABLED && s.reason == 2U && s.epoch == 2U && apply_at_tick != t); }
    if (Is("switch") && t == 200U)
    { assert(s.state == P3B_WAIT_LOCAL && s.epoch == 2U && !s.enabled); }
    if (Is("switch") && t == 740U)
    { assert(s.state == P3B_DISABLED && s.reason == 1U && apply_at_tick != t); }
    if (Is("fault") && t >= 120U)
    { assert(s.state == P3B_FAULT && actuator.fault_latched && !s.enabled); }
}
static void Inject(void)
{
    uint32_t t = Age();
    if (Is("local") || Is("local_fault"))
    { if (t == 200U || (Is("local_fault") && t == 160U)) { Control_PostStop(0U); } return; }
    if (Is("boot_hold")) { return; }
    if (t == 20U)
    {
        /* A predicted new-epoch frame received before the mode boundary. */
        Control_PostRemote(1U, 100U, 1800U, 1500U, tick - 2U);
        Mode(2U, 1U, 0U);
        if (Is("busy")) { Mode(1U, 2U, 0U); }
    }
    if (Is("switch"))
    {
        if (t >= 40U && t < 200U) { Remote(1U, 1510U); }
        if (t == 200U) { Mode(1U, 2U, 1U); }
        if (t > 200U && t < 600U) { Remote(1U, 2000U); }
        if (t == 600U) { Mode(2U, 3U, 2U); }
        if (t >= 640U) { Remote(3U, 1510U); }
        return;
    }
    if (Is("invalid"))
    {
        if (t >= 40U && t <= 80U) { Remote(1U, 1510U); }
        if (t == 100U) { Control_PostRemote(0U, seq + 1U, 1510U, 1500U, tick - 1U); }
        if (t == 120U) { Control_PostRemote(1U, seq + 1U, 2001U, 1500U, tick - 1U); }
        if (t == 140U) { Control_PostRemote(1U, seq + 1U, 1510U, 1601U, tick - 1U); }
        if (t == 160U) { Control_PostRemote(1U, seq + 1U, 1510U, 1500U, tick + 1U); }
        if (t >= 180U) { Control_PostRemote(1U, seq, 1510U, 1500U, tick - 1U); }
        return;
    }
    if (Is("timeout"))
    {
        if (t >= 40U && t <= 80U) { Remote(1U, 1510U); }
        if (t == 180U) { Mode(2U, 2U, 1U); } /* expiry wins same cycle */
        if (t >= 200U && t < 240U) { Remote(1U, 1510U); }
        if (t == 240U) { Mode(2U, 3U, 2U); }
        if (t >= 260U) { Remote(3U, 1510U); }
        return;
    }
    if (Is("stop"))
    {
        if (t >= 40U && t < 140U) { Remote(1U, 1510U); }
        if (t == 140U) { Mode(2U, 2U, 1U); Control_PostStop(0U); }
        if (t >= 160U && t < 240U) { Remote(1U, 1510U); Mode(2U, 1U, 1U); }
        if (t == 240U) { Mode(2U, 3U, 2U); }
        if (t >= 260U) { Remote(3U, 1510U); }
        return;
    }
    if (t >= 40U)
    {
        if (!Is("duplicate") || t <= 80U)
        { Remote(1U, Is("limits") ? 2000U : 1510U); }
        else { Control_PostRemote(1U, seq, 1510U, 1490U, tick - 1U); }
    }
    if (Is("duplicate") && t == 100U) { Mode(2U, 1U, 0U); }
    if (Is("mode_errors"))
    {
        if (t == 100U) { Mode(1U, 1U, 0U); }
        if (t == 120U) { Mode(1U, 2U, 99U); }
        if (t == 140U) { Mode(1U, 2U, 1U); }
    }
    if (Is("overflow") && t == 120U) { overflow = true; Mode(1U, 2U, 1U); }
    if (Is("fault") && t >= 140U) { Control_PostStop(0U); Mode(2U, 2U, 1U); }
}
BaseType_t xTaskDelayUntil(TickType_t *previous, TickType_t period)
{
    uint32_t delta;
    CheckCycle();
    if (Age() >= (Is("switch") ? 800U : Is("limits") ? 5200U : 500U)) { longjmp(finish, 1); }
    delta = *previous + period - tick; *previous += period;
    tick += delta; host_dwt.CYCCNT += delta * 72000U;
    Inject();
    return Is("fault") && Age() == 120U ? pdFALSE : pdTRUE;
}
static void PolicyChecks(void)
{
    ControlSession s = {0};
    assert(ControlSession_Authorize(&s));
    assert(ControlSession_DisarmNormal(&s));
    assert(!ControlSession_Authorize(&s)); /* TEST remains once per boot */
    assert(ControlSession_AuthorizeRemote(&s));
    assert(!ControlSession_AuthorizeLocal(&s));
    assert(ControlSession_DisarmNormal(&s));
    assert(ControlSession_AuthorizeLocal(&s));
    ControlSession_Close(&s, CONTROL_FAULT_DRIVER);
    assert(!ControlSession_DisarmNormal(&s));
    assert(!ControlSession_AuthorizeRemote(&s) && s.terminal && s.fault == CONTROL_FAULT_DRIVER);
}
int main(int argc, char **argv)
{
    ControlStatusSnapshot s;
    scenario = argc > 1 ? argv[1] : "nominal";
    PolicyChecks();
    tick = origin = Is("rollover") ? 0xffffff80U : 0U;
    actuator.initialized = true;
    assert(Control_Init()); assert(!Control_Init());
    if (setjmp(finish) == 0) { control_task(NULL); }
    Control_GetStatus(&s);
#if P3B_OUTPUT_STAGE > 0
    if (Is("boot_hold")) { assert(enables == 0U); }
    else { assert(enables > 0U && applies > 0U && first_apply >= 40U); }
    if (Is("local")) { assert(enables == 2U && enable_times[0] == 80U && enable_times[1] == 380U); }
    if (Is("stop") || Is("timeout")) { assert(enables == 2U && enable_times[1] == 260U); }
    if (Is("switch")) { assert(enables == 3U && enable_times[1] == 340U && enable_times[2] == 640U); }
    if (Is("fault") || Is("local_fault")) { assert(forces == 1U && s.state == P3B_FAULT); }
    else { assert(forces == 0U); }
    if (Is("duplicate")) { assert(s.state == P3B_DISABLED && s.reason == 3U); }
    if (Is("limits")) { assert(pan == 2000); }
#else
    assert(enables == 0U && applies == 0U && !s.enabled);
#endif
    printf("HOST PASS stage%d %s (synthetic only)\n", P3B_OUTPUT_STAGE, scenario);
    return 0;
}
