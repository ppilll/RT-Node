
/* Appended to the reused actual-UART fixture by run_host_tests.py. */
static P7ControlMetrics p7;
static CanDisplaySnapshot can_display;
static CanRxEvents can_events;
static uint32_t p7_violations;
void Control_GetP7Metrics(P7ControlMetrics *out, uint32_t *v) { *out = p7; *v = p7_violations; }
bool CanApp_GetDisplaySnapshot(CanDisplaySnapshot *out) { *out = can_display; return out->valid; }
void CanApp_GetRxEvents(CanRxEvents *out) { *out = can_events; }
int main(void)
{
    struct { unsigned char before[8]; char line[64]; unsigned char after[8]; } box;
    const char *commands[] = {"PERF?\n", "PERF2?\n", "PERF3?\n", "PERF4?\n", "CANC?\n"};
    const char *prefixes[] = {"P7", "P7N", "P7W", "P7S", "CAN"};
    uint32_t values[4] = {UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    unsigned i, j, r, p, c, w; uint16_t n;
    assert(LegacyUartMain() == 0);
    Fixture(); memset(&p7, 0xff, sizeof(p7)); p7_violations = UINT32_MAX;
    memset(&can_display, 0xff, sizeof(can_display)); memset(&can_events, 0xff, sizeof(can_events));
    memset(&diagnostics, 0xff, sizeof(diagnostics)); memset(&runtime, 0xff, sizeof(runtime));
    for (i = 0U; i < 5U; ++i)
    {
        memset(&box, 0xa5, sizeof(box));
        n = P7Format(box.line, prefixes[i], "abcd", values);
        assert(n == (i == 0U ? 56U : 57U));
        assert(box.line[n-2U] == '\r' && box.line[n-1U] == '\n');
        for (j = n; j < 64U; ++j) { assert((unsigned char)box.line[j] == 0xa5U); }
        for (j = 0U; j < 8U; ++j) { assert(box.before[j] == 0xa5U && box.after[j] == 0xa5U); }
        n = Feed(commands[i], box.line); assert(n == (i == 0U ? 56U : 57U));
        assert(strstr(box.line, "4294967295") != NULL);
        values[0] = values[1] = values[2] = values[3] = 0U;
        assert(P7Format(box.line, prefixes[i], "abcd", values) == (i == 0U ? 20U : 21U));
        values[0] = values[1] = values[2] = values[3] = UINT32_MAX;
    }
    Fixture(); memset(&p7, 0, sizeof(p7)); p7_violations = 0U;
    Command("PERF?\r\n", "P7 a=0 b=0 x=0 v=0\r\n");
    s_rx_base = 0U; s_wraps = 0U; s_overflow = 0U;
    host_dma.ISR = 0U; host_dma_channel.CNDTR = 256U;
    Command("PING\n", "PONG\r\n"); Command("COUNT?\n", "RX=0,OVF=0\r\n");
    Command("ADC?\n", "ADC ERROR INIT\r\n"); Command("ENV?\n", "ENV NOT READY\r\n");
    /* Reuse the P6b exhaustive HEALTH formatter contract on actual UART C. */
    for (r = 0U; r <= HEALTH_RESET_OTHER; ++r)
    for (p = 0U; p <= HEALTH_STORE_BAD_LOCKED; ++p)
    for (c = 0U; c <= HEALTH_CTRL_STALLED; ++c)
    for (w = 0U; w <= HEALTH_WDG_STARTING; ++w)
    {
        Fixture(); boot.reset_reason = (HealthResetReason)r;
        boot.persistent_state = (HealthPersistentState)p;
        runtime.control_progress = (HealthControlProgress)c;
        runtime.watchdog = (HealthWatchdogState)w; runtime.test_stop_active = true;
        n = Feed("HEALTH?\n", box.line); assert(n <= 58U);
        assert(strstr(box.line, " T=1\r\n") != NULL);
        if (c == HEALTH_CTRL_STALLED) { assert(strstr(box.line, "C=STALL") != NULL); }
        if (w == HEALTH_WDG_START_ERROR) { assert(strstr(box.line, "W=STARTERR") != NULL); }
    }
    puts("P7 actual UART PASS: max56/57 bytes, zero/CRLF/canaries, legacy recovery/PING/COUNT/ADC/ENV preserved");
    return 0;
}
