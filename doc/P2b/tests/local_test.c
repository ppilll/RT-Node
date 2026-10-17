#include "local_follow.h"
#include "p2b_config.h"
#include "control_policy.h"
#include <assert.h>
#include <math.h>
#include <float.h>
#include <stdio.h>

static ControlSession Open(void)
{
    ControlSession s = {0};
    assert(ControlSession_AuthorizeLocal(&s));
    assert(!ControlSession_Authorize(&s));
    return s;
}

int main(void)
{
    LocalFollowState f = {0}, old;
    ControlSession s;
    ControlCommandSnapshot c = {CONTROL_SOURCE_LOCAL_IMU, 0xffffffffU,
                                0xfffffff0U, 40U, true, 1500, 1500};
    int32_t pan = 0, tilt = 0, i;
    assert(!LocalFollow_Target(&f, 0, 0, &pan, &tilt));
    assert(!LocalFollow_Start(&f, NAN, 0));
    assert(!LocalFollow_Start(&f, 0, INFINITY));
    assert(LocalFollow_Start(&f, 10, -5));
    assert(LocalFollow_Target(&f, 12, -7, &pan, &tilt));
    assert(pan == 1500 && tilt == 1500); /* inclusive 2 degree deadband */
    assert(LocalFollow_Target(&f, 13, -8, &pan, &tilt));
    assert(pan == 1500 + 2 * P2B_PAN_SIGN && tilt == 1500 - 2 * P2B_TILT_SIGN);
    for (i = 0; i < 30; i++)
    {
        old = f;
        assert(LocalFollow_Target(&f, 110, -105, &pan, &tilt));
        assert(pan - old.last_pan_us <= 2 && old.last_pan_us - pan <= 2);
        assert(tilt - old.last_tilt_us <= 2 && old.last_tilt_us - tilt <= 2);
    }
    assert(pan == 1500 + 45 * P2B_PAN_SIGN && tilt == 1500 - 45 * P2B_TILT_SIGN);
    for (i = 0; i < 50; i++)
    { assert(LocalFollow_Target(&f, -90, 95, &pan, &tilt)); }
    assert(pan == 1500 - 45 * P2B_PAN_SIGN && tilt == 1500 + 45 * P2B_TILT_SIGN);
    old = f;
    assert(!LocalFollow_Target(&f, NAN, 0, &pan, &tilt));
    assert(!LocalFollow_Target(&f, 0, -INFINITY, &pan, &tilt));
    assert(f.last_pan_us == old.last_pan_us && f.last_tilt_us == old.last_tilt_us);
    assert(LocalFollow_Start(&f, FLT_MAX, 0));
    assert(!LocalFollow_Target(&f, -FLT_MAX, 0, &pan, &tilt));
    assert(LocalFollow_Start(&f, 10, -5));
    for (i = 0; i < 10; i++)
    { assert(LocalFollow_Target(&f, 13, -5, &pan, &tilt)); }
    assert(pan == 1500 + 9 * P2B_PAN_SIGN && tilt == 1500); /* absolute, not integrated */
    for (i = 0; i < 10; i++)
    { assert(LocalFollow_Target(&f, 10, -2, &pan, &tilt)); }
    assert(pan == 1500 && tilt == 1500 + 9 * P2B_TILT_SIGN);
    s = Open();
    assert(ControlSession_Accept(&s, &c, 0x17U)); /* age39 */
    c.generation = 0U;
    assert(ControlSession_Accept(&s, &c, 0x17U));
    assert(!ControlSession_Accept(&s, &c, 0x17U));
    assert(s.fault == CONTROL_FAULT_GENERATION);
    assert(!ControlSession_AuthorizeLocal(&s));
    s = Open();
    assert(!ControlSession_Accept(&s, &c, 0x18U)); /* age40 */
    assert(s.fault == CONTROL_FAULT_EXPIRED && s.has_generation);
    s = Open(); c.issued_tick = 100U;
    assert(!ControlSession_Accept(&s, &c, 99U));
    s = Open(); c.source = CONTROL_SOURCE_TEST;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = (ControlSession){0}; assert(ControlSession_Authorize(&s));
    c.source = CONTROL_SOURCE_LOCAL_IMU;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.source = (ControlSource)99;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.source = CONTROL_SOURCE_LOCAL_IMU; c.generation = 20U;
    assert(ControlSession_Accept(&s, &c, 100U));
    c.generation = 19U;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.generation = 20U;
    assert(ControlSession_Accept(&s, &c, 100U));
    c.generation += 0x80000000U;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.ttl_ticks = 101U;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.ttl_ticks = 0U;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.ttl_ticks = 40U; c.pan_us = 2001;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); c.pan_us = 1500; c.tilt_us = 1199;
    assert(!ControlSession_Accept(&s, &c, 100U));
    s = Open(); ControlSession_Close(&s, CONTROL_FAULT_NONE);
    assert(!ControlSession_AuthorizeLocal(&s));
    puts("PASS U01/U02: real mapping, signs, absolute reference, deadband, clamp/slew, NaN/Inf; explicit sources, TTL, generation, terminal");
    return 0;
}
