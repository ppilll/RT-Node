#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define P6A_UART_HOST 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
uint32_t host_tick;
int host_scheduler = 2;
bool host_key0, host_key1, host_i2c_fail;
uint32_t host_tx_calls;
#include "../../../App/uart/uart_app.c"
HostDwt host_dwt;
HostDebug host_debug;
HostSysTick host_systick;
HostAfio host_afio;
HostUsart host_usart;
HostDmaChannel host_dma_channel;
HostDma host_dma;
unsigned host_resets;
static HealthBootSnapshot boot;
static ControlStatusSnapshot control;
static ControlDiagnostics diagnostics;
static ActuatorSnapshot actuator;
static bool tx_fail, stage_fail;
static unsigned staged, canceled, adc_services, transmitted;
void Health_GetBootSnapshot(HealthBootSnapshot *out) { *out = boot; }
bool Health_ControlAuthorizationAllowed(void) { return boot.authorization_allowed; }
bool Health_BootAutoLocalAllowed(void) { return boot.auto_local_allowed; }
bool Health_StageLocalRecovery(void) { staged++; return !stage_fail; }
bool Health_CancelLocalRecovery(void) { canceled++; return true; }
void Control_GetStatus(ControlStatusSnapshot *out) { *out = control; }
void Control_GetDiagnostics(ControlDiagnostics *out) { *out = diagnostics; }
void Actuator_GetSnapshot(ActuatorSnapshot *out) { *out = actuator; }
bool AdcApp_GetLatest(AdcApp_Result *out) { (void)out; return false; }
AdcApp_Status AdcApp_GetStatus(void) { return ADC_APP_INIT_ERROR; }
void AdcApp_Process(void) { adc_services++; }
bool AdcApp_Start(void) { return true; }
bool EnvApp_GetSnapshot(EnvSnapshot *out) { (void)out; return false; }
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(uart == &s_uart && data && size <= 30U && timeout == 10U);
    assert(host_resets == 0U); /* Reset cannot precede successful TX. */
    transmitted += size;
    return tx_fail ? HAL_ERROR : HAL_OK;
}
static void Fixture(void)
{
    memset(&boot, 0, sizeof(boot)); memset(&control, 0, sizeof(control));
    memset(&diagnostics, 0, sizeof(diagnostics)); memset(&actuator, 0, sizeof(actuator));
    boot.persistent_state = HEALTH_STORE_EMPTY_LOCKED;
    actuator.initialized = true; host_key0 = false;
    s_length = 0U; s_discard = false; s_cr_seen = false; s_safe_ready = false; s_reboot_pending = false;
    s_fault = 0U; s_error_pending = 0U;
    staged = 0U; canceled = 0U; host_resets = 0U; transmitted = 0U; adc_services = 0U;
    tx_fail = false; stage_fail = false; host_tick = 100U;
    s_uart.Instance = USART1; s_dma.Instance = DMA1_Channel5;
}
static uint16_t Feed(const char *line, char *reply)
{
    uint16_t size = 0U;
    while (*line) { size = ParseByte((uint8_t)*line++, reply); }
    assert(size < 64U); reply[size] = '\0'; return size;
}
static void Command(const char *line, const char *expected)
{
    char reply[64]; Feed(line, reply); assert(strcmp(reply, expected) == 0);
}
int main(void)
{
    char reply[64]; uint16_t size; unsigned reason, policy;
    Fixture(); Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n"); assert(staged == 0U);
    Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n"); host_tick += 10000U;
    Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n"); assert(staged == 0U);
    Fixture(); host_tick = 0xFFFFFF00U;
    Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n"); host_tick += 9999U;
    size = Feed("SAFE CLEAR YES\n", reply); assert(strcmp(reply, "SAFE REBOOT\r\n") == 0);
    assert(staged == 1U && host_resets == 0U);
    ReplyAndMaybeReboot(reply, size); assert(host_resets == 1U && transmitted == size);
    /* Stub reset returns, allowing the defensive cancellation to execute. */
    Fixture(); Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n");
    size = Feed("SAFE CLEAR YES\n", reply); tx_fail = true;
    ReplyAndMaybeReboot(reply, size); assert(host_resets == 0U && canceled == 1U && s_fault);
    Fixture(); Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n");
    size = Feed("SAFE CLEAR YES\n", reply); s_error_pending = 1U;
    ReplyAndMaybeReboot(reply, size); assert(host_resets == 0U && canceled == 1U && !transmitted);
    Fixture(); Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n");
    size = Feed("SAFE CLEAR YES\n", reply); host_key0 = true;
    ReplyAndMaybeReboot(reply, size); assert(host_resets == 0U && canceled == 1U);
    Fixture(); Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n"); stage_fail = true;
    Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n"); assert(!s_reboot_pending);
    Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n"); assert(staged == 1U);
    for (policy = 0U; policy < 9U; policy++)
    {
        Fixture(); Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n");
        switch (policy)
        {
            case 0: host_key0 = true; break;
            case 1: control.key0_stop_latched = true; break;
            case 2: control.state = P3B_WAIT_REMOTE; break;
            case 3: control.enabled = true; break;
            case 4: actuator.initialized = false; break;
            case 5: actuator.enabled = true; break;
            case 6: actuator.fault_latched = true; break;
            case 7: diagnostics.fault = CONTROL_FAULT_DEADLINE; break;
            case 8: boot.persistent_state = HEALTH_STORE_BAD_LOCKED; break;
        }
        Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n"); assert(staged == 0U);
        Command("SAFE CLEAR?\n", "SAFE DENIED\r\n");
    }
    Fixture(); Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n");
    Command("PING\n", "PONG\r\n"); Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n");
    Command("SAFE CLEAR YES extra\n", "ERR\r\n");
    Command("safe clear?\n", "ERR\r\n");
    Command("SAFE CLEAR?\r\n", "SAFE READY 10S\r\n");
    Command("SAFE CL\rEAR YES\n", "ERR\r\n");
    Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n");
    Command("SAFE CLEAR?\n", "SAFE READY 10S\r\n");
    Command("123456789012345678901234567890123456\n", "ERR\r\n");
    Command("SAFE CLEAR YES\n", "SAFE DENIED\r\n");
    for (reason = 0U; reason <= HEALTH_RESET_OTHER; reason++)
    {
        for (policy = 0U; policy <= HEALTH_STORE_BAD_LOCKED; policy++)
        {
            Fixture(); boot.reset_reason = (HealthResetReason)reason;
            boot.persistent_state = (HealthPersistentState)policy;
            boot.authorization_allowed = policy == HEALTH_STORE_AUTO || policy == HEALTH_STORE_MANUAL_ONLY;
            size = Feed("HEALTH?\n", reply); assert(size <= 58U);
            assert(strstr(reply, "WDG=OFF\r\n") != NULL);
            ReplyAndMaybeReboot(reply, size); assert(!host_resets && adc_services >= 1U);
            if (boot.authorization_allowed)
            { Command("SAFE CLEAR?\n", "SAFE DENIED\r\n"); }
        }
    }
    puts("P6a UART assertions PASS (parser, safety gates, timeout/wrap, TX/reset/cancel); hardware NOT RUN");
    return 0;
}
