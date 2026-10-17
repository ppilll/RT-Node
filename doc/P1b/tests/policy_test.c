/* Executes the REAL production policy C; no separate Python policy model. */
#include "control_policy.h"
#include <assert.h>
#include <stdio.h>

static ControlSession Open(void)
{
    ControlSession s = {0};
    assert(ControlSession_Authorize(&s));
    return s;
}

int main(void)
{
    ControlCommandSnapshot c = {CONTROL_SOURCE_TEST, 1U, 100U, 40U, true, 1500, 1500};
    ControlSession s = {0};
    assert(!ControlSession_Accept(&s, &c, 100U)); /* no authorization */
    assert(!ControlSession_Authorize(&s));
    s = Open();
    assert(ControlSession_Accept(&s, &c, 139U)); /* last fresh tick */
    assert(!ControlSession_Accept(&s, &c, 139U)); /* same generation */
    assert(s.fault == CONTROL_FAULT_GENERATION);
    c.generation++;
    assert(!ControlSession_Accept(&s, &c, 139U)); /* newer cannot clear terminal */
    assert(!ControlSession_Authorize(&s));
    s = Open();
    assert(!ControlSession_Accept(&s, &c, 140U)); /* exact expiry */
    assert(s.fault == CONTROL_FAULT_EXPIRED && s.last_generation == 2U);
    s = Open();
    assert(!ControlSession_Accept(&s, &c, 99U)); /* future timestamp */
    s = Open();
    c.issued_tick = 0xfffffff0U;
    assert(ControlSession_Accept(&s, &c, 0x10U)); /* tick wrap, age 32 */
    c.generation = 3U;
    assert(!ControlSession_Accept(&s, &c, 0x18U)); /* tick wrap, age 40 */
    s = Open();
    c.issued_tick = 100U;
    c.generation = 0xffffffffU;
    assert(ControlSession_Accept(&s, &c, 100U));
    c.generation = 0U;
    assert(ControlSession_Accept(&s, &c, 100U)); /* generation wrap */
    c.generation = 0x80000000U;
    assert(!ControlSession_Accept(&s, &c, 100U)); /* ambiguous half range */
    s = Open();
    assert(!ControlSession_Accept(&s, NULL, 100U));
    s = Open(); c.generation = 1U; c.ttl_ticks = 0U;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.ttl_ticks = 101U;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.ttl_ticks = 40U; c.valid = false;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.valid = true; c.source = CONTROL_SOURCE_NONE;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.source = CONTROL_SOURCE_TEST; c.pan_us = -1;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.pan_us = 1000; c.tilt_us = 1200;
    assert(ControlSession_Accept(&s, &c, 100U));
    c.generation++; c.pan_us = 2000; c.tilt_us = 1600;
    assert(ControlSession_Accept(&s, &c, 100U));
    c.generation++; c.tilt_us = 1601;
    assert(!ControlSession_Accept(&s, &c, 100U));
    assert(s.last_generation == c.generation); /* rejected generation consumed */
    s = Open();
    ControlSession_Close(&s, CONTROL_FAULT_NONE);
    assert(!ControlSession_Authorize(&s)); /* normal test end cannot rearm */
    assert(!ControlSession_Accept(&s, &c, 100U));
    puts("PASS: policy authorization, TTL/generation boundaries, wraps, ranges, terminal replay");
    return 0;
}
