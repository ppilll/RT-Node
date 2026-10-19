#ifndef RT_NODE_P7_METRICS_H
#define RT_NODE_P7_METRICS_H
#include <stdint.h>
#include <stdbool.h>

/* Independent observation only. Flags: active=1, saturated=2, invalid gap=4.
 * At 72 MHz a full DWT lap is ~59.65 s. Tick gaps >=59 s are rejected,
 * rather than treating an ambiguous modulo difference as a short period. */
typedef struct {
    uint32_t window_start_tick, observed_tick, flags, invalid_samples;
    uint32_t period_samples, period_min_cycles, period_max_cycles;
    uint32_t elapsed_max_cycles;
} P7ControlMetrics;
typedef struct {
    P7ControlMetrics value;
    uint32_t boot_tick, previous_tick, previous_cycle;
    uint32_t period_violation_count;
    bool previous_valid;
} P7MetricsState;

#ifdef P7_METRICS_IMPLEMENTATION
static void P7Increment(uint32_t *value, uint32_t *flags)
{
    if (*value != UINT32_MAX) { ++*value; }
    else { *flags |= 2U; }
}
static void P7Entry(P7MetricsState *s, uint32_t tick, uint32_t cycle, bool clock_ok)
{
    uint32_t delta;
    s->value.observed_tick = tick;
    if ((s->value.flags & 1U) == 0U)
    {
        if ((uint32_t)(tick - s->boot_tick) < 10000U) { return; }
        s->value.flags |= 1U;
        s->value.window_start_tick = tick;
    }
    if (s->previous_valid)
    {
        delta = cycle - s->previous_cycle;
        if (!clock_ok || (uint32_t)(tick - s->previous_tick) >= 59000U || delta == 0U)
        {
            s->value.flags |= 4U;
            P7Increment(&s->value.invalid_samples, &s->value.flags);
        }
        else
        {
            if (s->value.period_samples == 0U || delta < s->value.period_min_cycles)
            { s->value.period_min_cycles = delta; }
            if (delta > s->value.period_max_cycles) { s->value.period_max_cycles = delta; }
            P7Increment(&s->value.period_samples, &s->value.flags);
            if (delta < 19000U * 72U || delta > 21000U * 72U)
            { P7Increment(&s->period_violation_count, &s->value.flags); }
        }
    }
    else if (!clock_ok)
    {
        s->value.flags |= 4U;
        P7Increment(&s->value.invalid_samples, &s->value.flags);
    }
    s->previous_cycle = cycle;
    s->previous_tick = tick;
    s->previous_valid = clock_ok;
}
static void P7Elapsed(P7MetricsState *s, uint32_t elapsed)
{
    if ((s->value.flags & 1U) != 0U && elapsed > s->value.elapsed_max_cycles)
    { s->value.elapsed_max_cycles = elapsed; }
}
#endif
#endif
