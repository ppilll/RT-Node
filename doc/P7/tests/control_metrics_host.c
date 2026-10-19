/* Actual P6 control/policy/display/Flash-RAM regression plus P7 snapshots.
 * Host threads model a single-core task/critical-section exclusion; no MCU IO. */
#include <windows.h>
#include "FreeRTOS.h"
static CRITICAL_SECTION exclusion;
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL() EnterCriticalSection(&exclusion)
#define taskEXIT_CRITICAL() LeaveCriticalSection(&exclusion)
#include "legacy_p6.c"

static DWORD WINAPI Writer(void *unused)
{
    uint32_t i;
    (void)unused;
    for (i = 1U; i <= 50000U; ++i)
    {
        taskENTER_CRITICAL(); /* Scheduler exclusion of lower-priority reader. */
        P7Entry(&s_p7, 10000U + i * 20U, i * 1440000U, true);
        P7Elapsed(&s_p7, i);
        taskEXIT_CRITICAL();
    }
    return 0;
}
static void Boundaries(void)
{
    P7ControlMetrics out;
    uint32_t v;
    memset(&s_p7, 0, sizeof(s_p7));
    Control_GetP7Metrics(NULL, &v); Control_GetP7Metrics(&out, NULL);
    P7Entry(&s_p7, 9999U, 100U, true); P7Elapsed(&s_p7, 20U);
    Control_GetP7Metrics(&out, &v);
    assert(out.flags == 0U && out.period_samples == 0U && out.elapsed_max_cycles == 0U);
    P7Entry(&s_p7, 10000U, 0xfffffff0U, true);
    assert(s_p7.value.period_samples == 0U && s_p7.value.window_start_tick == 10000U);
    P7Entry(&s_p7, 10019U, 0xfffffff0U + 1368000U, true);
    P7Entry(&s_p7, 10040U, 0xfffffff0U + 2880000U, true);
    assert(s_p7.value.period_samples == 2U && s_p7.period_violation_count == 0U);
    assert(s_p7.value.period_min_cycles == 1368000U && s_p7.value.period_max_cycles == 1512000U);
    P7Entry(&s_p7, 10059U, s_p7.previous_cycle + 1367999U, true);
    P7Entry(&s_p7, 10080U, s_p7.previous_cycle + 1512001U, true);
    assert(s_p7.period_violation_count == 2U);
    P7Elapsed(&s_p7, 100U); P7Elapsed(&s_p7, 1U);
    assert(s_p7.value.elapsed_max_cycles == 100U);
    s_p7.value.period_samples = UINT32_MAX; s_p7.period_violation_count = UINT32_MAX;
    P7Entry(&s_p7, 10100U, s_p7.previous_cycle + 1U, true);
    assert(s_p7.value.period_samples == UINT32_MAX && s_p7.period_violation_count == UINT32_MAX);
    assert(s_p7.value.flags & 2U);
    P7Entry(&s_p7, 69100U, s_p7.previous_cycle + 1440000U, true);
    assert(s_p7.value.invalid_samples == 1U && s_p7.value.flags & 4U);
    P7Entry(&s_p7, 69120U, s_p7.previous_cycle, true);
    assert(s_p7.value.invalid_samples == 2U);
    P7Entry(&s_p7, 69140U, 42U, false);
    assert(s_p7.value.invalid_samples == 3U);
    s_p7.value.invalid_samples = UINT32_MAX;
    P7Entry(&s_p7, 69160U, 42U, false);
    assert(s_p7.value.invalid_samples == UINT32_MAX);
    memset(&s_p7, 0, sizeof(s_p7)); s_p7.boot_tick = 0xfffffff0U;
    P7Entry(&s_p7, 9983U, 1U, true); assert(!s_p7.value.flags);
    P7Entry(&s_p7, 9984U, 2U, true); assert(s_p7.value.flags == 1U);
}
int main(void)
{
    HANDLE writer;
    uint32_t i, violations;
    P7ControlMetrics out;
    InitializeCriticalSection(&exclusion);
    assert(LegacyP6Main() == 0);
    Boundaries();
    memset(&s_p7, 0, sizeof(s_p7));
    P7Entry(&s_p7, 10000U, 0U, true);
    writer = CreateThread(NULL, 0, Writer, NULL, 0, NULL); assert(writer != NULL);
    for (i = 0U; i < 50000U; ++i)
    {
        Control_GetP7Metrics(&out, &violations);
        assert(out.flags == 1U && violations == 0U);
        if (out.period_samples)
        {
            assert(out.period_min_cycles == 1440000U && out.period_max_cycles == 1440000U);
            assert(out.elapsed_max_cycles == out.period_samples);
        }
    }
    assert(WaitForSingleObject(writer, INFINITE) == WAIT_OBJECT_0); CloseHandle(writer);
    Control_GetP7Metrics(&out, &violations); assert(out.period_samples == 50000U);
    DeleteCriticalSection(&exclusion);
    puts("P7 metrics PASS: warmup/zero/boundaries/wrap/invalid/saturation, 50000 concurrent coherent copies; simulated only");
    return 0;
}
