#include "local_follow.h"
#include "p2b_config.h"
#include <float.h>
#include <stddef.h>

static bool Finite(float value)
{
    return (value >= -FLT_MAX) && (value <= FLT_MAX);
}

static int32_t Axis(float delta, int32_t sign, int32_t previous)
{
    int32_t target;
    if ((delta >= -P2B_DEADBAND_DEG) && (delta <= P2B_DEADBAND_DEG))
    { delta = 0.0f; }
    if (delta > P2B_MAX_ANGLE_DEG) { delta = P2B_MAX_ANGLE_DEG; }
    if (delta < -P2B_MAX_ANGLE_DEG) { delta = -P2B_MAX_ANGLE_DEG; }
    /* Truncate offset toward zero; deadband does not subtract 2 degrees. */
    target = P2B_CENTER_US + sign * (int32_t)(delta * P2B_US_PER_DEG);
    if (target > P2B_CENTER_US + P2B_MAX_OFFSET_US)
    { target = P2B_CENTER_US + P2B_MAX_OFFSET_US; }
    if (target < P2B_CENTER_US - P2B_MAX_OFFSET_US)
    { target = P2B_CENTER_US - P2B_MAX_OFFSET_US; }
    if (target > previous + P2B_SLEW_US) { target = previous + P2B_SLEW_US; }
    if (target < previous - P2B_SLEW_US) { target = previous - P2B_SLEW_US; }
    return target;
}

bool LocalFollow_Start(LocalFollowState *s, float roll0_deg, float pitch0_deg)
{
    if (s == NULL || !Finite(roll0_deg) || !Finite(pitch0_deg)) { return false; }
    s->roll0_deg = roll0_deg;
    s->pitch0_deg = pitch0_deg;
    s->last_pan_us = P2B_CENTER_US;
    s->last_tilt_us = P2B_CENTER_US;
    s->initialized = true;
    return true;
}

bool LocalFollow_Target(LocalFollowState *s, float roll_deg, float pitch_deg,
                        int32_t *pan_us, int32_t *tilt_us)
{
    float dr, dp;
    if (s == NULL || pan_us == NULL || tilt_us == NULL || !s->initialized ||
        !Finite(roll_deg) || !Finite(pitch_deg)) { return false; }
    dr = roll_deg - s->roll0_deg;
    dp = pitch_deg - s->pitch0_deg;
    if (!Finite(dr) || !Finite(dp)) { return false; }
    s->last_pan_us = Axis(dr, P2B_PAN_SIGN, s->last_pan_us);
    s->last_tilt_us = Axis(dp, P2B_TILT_SIGN, s->last_tilt_us);
    *pan_us = s->last_pan_us;
    *tilt_us = s->last_tilt_us;
    return true;
}
