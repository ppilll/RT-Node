/* Executes the production control state machine and display helpers with
 * simulated HAL/RTOS IO. Never generates or loads a Stage1/Stage2 firmware. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
uint32_t host_tick;
bool host_key0, host_key1, host_i2c_fail;
uint32_t host_tx_calls;
#include "stm32f1xx_hal.h"
HostDwt host_dwt;
HostDebug host_debug;
HostSysTick host_systick;
HostAfio host_afio;
#include "../../../App/control/control.c"
#include "../../../App/control/control_policy.c"
#include "../../../App/control/local_follow.c"
#include "../../../App/display/display_app.c"

static ActuatorSnapshot host_actuator;
static ImuAttitudeSnapshot host_imu;
static bool host_imu_available, host_overflow;
static EnvSnapshot host_env;
static bool host_env_available;
static CanDisplaySnapshot host_can;
static UartDisplaySnapshot host_uart;
static AdcApp_Result host_adc;
static uint32_t host_enables, host_applies;

bool Actuator_Enable(void)
{ assert(!host_key0 && !s_run.status.key0_stop_latched); host_actuator.enabled = true; host_enables++; return true; }
void Actuator_Disable(void) { host_actuator.enabled = false; }
void Actuator_ForceSafeOutput(void)
{ host_actuator.enabled = false; host_actuator.fault_latched = true; }
void Actuator_GetSnapshot(ActuatorSnapshot *out) { *out = host_actuator; }
bool Actuator_Apply(const ActuatorCommand *command, ActuatorApplyResult *out)
{
    assert(host_actuator.enabled && !host_key0 && !s_run.status.key0_stop_latched);
    assert(command->pan_position_us >= 1000 && command->pan_position_us <= 2000);
    assert(command->tilt_position_us >= 1200 && command->tilt_position_us <= 1600);
    (void)out; host_applies++; return true;
}
bool Imu_GetSnapshot(ImuAttitudeSnapshot *out) { *out = host_imu; return host_imu_available; }
bool CanApp_TakeOverflow(void) { bool result = host_overflow; host_overflow = false; return result; }
bool EnvApp_GetSnapshot(EnvSnapshot *out) { *out = host_env; return host_env_available; }
bool CanApp_GetDisplaySnapshot(CanDisplaySnapshot *out) { *out = host_can; return out->valid; }
bool UartApp_GetDisplaySnapshot(UartDisplaySnapshot *out) { *out = host_uart; return out->valid; }
bool AdcApp_GetLatest(AdcApp_Result *out) { *out = host_adc; return out->valid; }

static ControlSession session;
static void ResetBoot(void)
{
    memset(&session, 0, sizeof(session)); memset(&s_run, 0, sizeof(s_run));
    memset(&s_mail, 0, sizeof(s_mail)); memset(&s_status, 0, sizeof(s_status));
    memset(&host_actuator, 0, sizeof(host_actuator));
    memset(&host_imu, 0, sizeof(host_imu));
    host_tick = 100U; host_key0 = false; host_overflow = false;
    host_enables = 0U; host_applies = 0U;
    host_imu_available = true; host_imu.state = IMU_READY; host_imu.valid = true;
    host_actuator.initialized = true;
    s_run.boot_local_pending = true; s_run.status.boundary_tick = 100U;
}
static void Step(uint32_t elapsed)
{
    host_tick += elapsed; host_dwt.CYCCNT = host_tick * 72000U;
    host_imu.sample_tick = host_tick; host_imu.generation++;
    P3bCycle(&session); PublishStatus(&session);
}
static void Mode(uint8_t mode, uint16_t action)
{
    host_tick++; Control_PostMode(mode, action, s_status.epoch, host_tick); Step(19U);
}
static void RunControlTests(void)
{
    unsigned i;
    ResetBoot(); host_key0 = true; Step(20U);
    assert(s_status.key0_stop_latched && host_enables == 0U);
    host_key0 = false;
    for (i = 0U; i < 10U; i++) { Mode(1U + i % 2U, (uint16_t)(i + 1U)); }
    assert(host_enables == 0U && !s_status.enabled && s_status.key0_stop_latched);

    ResetBoot(); host_imu.valid = false; Step(20U); assert(host_enables == 0U);
    host_imu.valid = true; host_imu.roll_deg = (float)NAN; Step(20U); assert(host_enables == 0U);
    host_imu.roll_deg = 12.0f; host_imu.pitch_deg = -3.0f;
    host_tick += 100U; host_imu.sample_tick = host_tick - 40U;
    P3bCycle(&session); assert(host_enables == 0U); /* stale at the exact TTL */
    Step(20U);
#if P3B_OUTPUT_STAGE > 0
    assert(host_enables == 1U && s_status.state == P3B_PRIMING_LOCAL);
    assert(s_run.follow.roll0_deg == 12.0f && s_run.follow.pitch0_deg == -3.0f);
    Step(20U); assert(host_applies == 0U); Step(20U); assert(host_applies == 1U);
#else
    assert(host_enables == 0U && !s_status.enabled);
#endif
    host_key0 = true; Step(20U); host_key0 = false;
    assert(s_status.key0_stop_latched && !s_status.enabled);
    for (i = 0; i < 10U; i++)
    {
        Mode(2U, (uint16_t)(i + 1U));
        host_tick++; Control_PostRemote(s_status.epoch, (uint16_t)i, 1800U, 1400U, host_tick);
        Step(19U); assert(!s_status.enabled && s_status.key0_stop_latched);
    }
    assert(!s_mail.remote.valid);
    Invalidate(host_tick); Disarm(&session, 2U, host_tick);
    assert(s_run.status.key0_stop_latched);

    ResetBoot(); Control_PostStop(1U); Step(20U);
    for (i = 0U; i < 10U; i++) { Step(20U); }
    assert(!s_status.enabled && !s_run.boot_local_pending && host_enables == 0U);
    Mode(1U, 2U); Step(20U);
#if P3B_OUTPUT_STAGE > 0
    assert(s_status.enabled); /* Explicit CAN LOCAL can reauthorize ordinary STOP. */
#endif
    Control_PostStop(3U); Step(20U); Step(20U);
    assert(!s_status.enabled && !s_status.key0_stop_latched);

    ResetBoot(); Mode(0U, 1U); Step(20U);
    assert(!s_run.boot_local_pending && host_enables == 0U);

    ResetBoot(); Mode(2U, 1U); host_tick++;
    Control_PostRemote(s_status.epoch, 1U, 1900U, 1300U, host_tick); Step(19U);
#if P3B_OUTPUT_STAGE > 0
    assert(s_status.state == P3B_PRIMING_REMOTE);
    host_tick++; Control_PostRemote(s_status.epoch, 2U, 1900U, 1300U, host_tick);
    Step(39U); assert(host_applies == 1U && s_run.pan == 1502 && s_run.tilt == 1498);
#endif
    Step(100U); Step(20U);
    assert(!s_status.enabled && !s_run.boot_local_pending && !s_status.key0_stop_latched);

    ResetBoot(); host_overflow = true; Step(20U); Step(20U);
    assert(host_enables == 0U && !s_run.boot_local_pending);
#if P3B_OUTPUT_STAGE > 0
    ResetBoot(); Step(20U); host_imu.valid = false; Step(20U);
    assert(session.terminal && s_status.state == P3B_FAULT);
    Mode(2U, 1U); assert(!s_status.enabled);
    /* Final output wrappers also reject a low appearing after cycle entry. */
    ResetBoot(); host_key0 = true; assert(!Enable(&session, host_tick));
    assert(s_run.status.key0_stop_latched && host_enables == 0U);
    ResetBoot(); Step(20U); host_key0 = true; Apply(&session, 1500, 1500, host_tick);
    assert(s_run.status.key0_stop_latched && host_applies == 0U);
#endif
    /* Simulated MCU reset alone restores the one boot opportunity. */
    ResetBoot(); Step(20U); assert(!s_status.key0_stop_latched);
}

static void ExpectRow(unsigned row, const char *expected)
{
    unsigned i;
    assert(strlen(expected) <= 21U);
    for (i = 0U; i < 21U; i++)
    {
        char c = i < strlen(expected) ? expected[i] : ' ';
        assert(memcmp(s_fb + row * 128U + i * 6U, s_font[c - 32], 6U) == 0);
    }
    assert(s_fb[row * 128U + 126U] == 0U && s_fb[row * 128U + 127U] == 0U);
}
static void RunDisplayTests(void)
{
    uint16_t offset;
    s_page = 0U; host_env_available = false; BuildFrame(); ExpectRow(0, "ENV WAIT");
    host_env_available = true; host_env.valid = true; host_env.sample_tick = host_tick;
    host_env.temperature_centi_c = -4000; host_env.humidity_centi_pct = 10000U;
    host_env.pressure_pa = 101325U;
    BuildFrame(); ExpectRow(1, "T:-40.00C H:100.00%"); ExpectRow(2, "P:1013.25hPa");
    host_tick += 2001U; BuildFrame(); ExpectRow(0, "ENV STALE"); ExpectRow(1, "T:--C H:--%");
    host_env.valid = false; BuildFrame(); ExpectRow(0, "ENV ERR");
    s_page = 1U; s_status.key0_stop_latched = true; host_actuator.enabled = false;
    BuildFrame(); ExpectRow(0, "IMU/PWM KEY0 LOCK");
    s_page = 2U; memset(&host_can, 0, sizeof(host_can)); BuildFrame(); ExpectRow(1, "REQ:--/--");
    host_can.valid = true; host_can.last_id = 0x180U; host_can.request_valid = true;
    host_can.requested_pan_us = 65535U; host_can.requested_tilt_us = 65535U;
    host_can.request_tick = host_tick - 100U; BuildFrame(); ExpectRow(1, "REQ:65535/65535 OLD");
    s_page = 3U; host_uart.rx_bytes = UINT32_MAX; host_uart.ovf_count = UINT32_MAX;
    host_uart.valid = true; memcpy(host_uart.last_command, "ABCDEFGHIJKLMNO", 16U);
    host_uart.last_command_tick = host_tick; BuildFrame();
    ExpectRow(0, "UART RX:4294967295"); ExpectRow(1, "CMD:ABCDEFGHIJKLMNO"); ExpectRow(2, "OVF:4294967295");
    host_tx_calls = 0U; assert(InitOled());
    for (offset = 0U; offset < 512U; offset += 16U) { assert(SendBlock(offset)); }
    assert(host_tx_calls == 66U);
    host_i2c_fail = true; assert(!SendBlock(0U)); host_i2c_fail = false;
    s_page = 0U; memset(&s_key, 0, sizeof(s_key)); host_key1 = true;
    InitKey1(); assert(!PollKey1(host_tick + 500U) && s_page == 0U);
    host_key1 = false; assert(!PollKey1(host_tick + 501U)); assert(!PollKey1(host_tick + 541U));
    host_key1 = true; assert(!PollKey1(host_tick + 542U)); assert(!PollKey1(host_tick + 581U));
    assert(PollKey1(host_tick + 582U) && s_page == 1U);
    assert(!PollKey1(host_tick + 2000U) && s_page == 1U);
    host_key1 = false; PollKey1(host_tick + 2001U); PollKey1(host_tick + 2041U);
    host_key1 = true; PollKey1(host_tick + 2042U); assert(PollKey1(host_tick + 2082U) && s_page == 2U);
}
int main(void)
{
    RunControlTests(); RunDisplayTests();
    printf("P5b host assertions PASS (simulated output stage %d)\n", P3B_OUTPUT_STAGE);
    return 0;
}
