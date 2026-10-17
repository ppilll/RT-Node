#include "imu_fusion.h"
#include <math.h>

#define RAD_TO_DEG 57.29577951308232f

static bool Bounded(float value, float bound)
{
    /* Also rejects NaN/Inf on ARMCC5 without nonportable isfinite macros. */
    return (value >= -bound) && (value <= bound);
}

static bool SampleValid(const ImuSample *s, float min_norm, float max_norm)
{
    float norm2;
    if (!Bounded(s->ax_g, 2.1f) || !Bounded(s->ay_g, 2.1f) ||
        !Bounded(s->az_g, 2.1f) || !Bounded(s->gx_dps, 251.0f) ||
        !Bounded(s->gy_dps, 251.0f) || !Bounded(s->gz_dps, 251.0f))
    { return false; }
    norm2 = s->ax_g*s->ax_g + s->ay_g*s->ay_g + s->az_g*s->az_g;
    return (norm2 >= min_norm*min_norm) && (norm2 <= max_norm*max_norm);
}

static void AccelAngles(const ImuSample *s, float *roll, float *pitch)
{
    *roll = atan2f(s->ay_g, s->az_g) * RAD_TO_DEG;
    *pitch = atan2f(-s->ax_g,
                   sqrtf(s->ay_g*s->ay_g + s->az_g*s->az_g)) * RAD_TO_DEG;
}

void ImuFusion_Reset(ImuFusion *f)
{
    ImuFusion empty = {0};
    *f = empty;
}

bool ImuFusion_Calibrate(ImuFusion *f, const ImuSample *s)
{
    if (f->calibrated) { return true; }
    if (!SampleValid(s, 0.8f, 1.2f) ||
        !Bounded(s->gx_dps, 5.0f) || !Bounded(s->gy_dps, 5.0f) ||
        !Bounded(s->gz_dps, 5.0f))
    {
        ImuFusion_Reset(f);
        return false;
    }
    f->sum_x += s->gx_dps;
    f->sum_y += s->gy_dps;
    f->sum_z += s->gz_dps;
    f->stationary_count++;
    if (f->stationary_count == IMU_CALIBRATION_SAMPLES)
    {
        f->bias_x_dps = f->sum_x / (float)IMU_CALIBRATION_SAMPLES;
        f->bias_y_dps = f->sum_y / (float)IMU_CALIBRATION_SAMPLES;
        f->bias_z_dps = f->sum_z / (float)IMU_CALIBRATION_SAMPLES;
        AccelAngles(s, &f->roll_deg, &f->pitch_deg);
        f->calibrated = true;
    }
    return f->calibrated;
}

bool ImuFusion_Update(ImuFusion *f, const ImuSample *s, float dt_s)
{
    float roll_acc, pitch_acc, roll, pitch, alpha;
    if (!f->calibrated || !(dt_s > 0.0f && dt_s <= 0.05f) ||
        !SampleValid(s, 0.5f, 1.5f))
    { return false; }
    AccelAngles(s, &roll_acc, &pitch_acc);
    alpha = IMU_FUSION_TAU_S / (IMU_FUSION_TAU_S + dt_s);
    roll = alpha * (f->roll_deg + (s->gx_dps-f->bias_x_dps)*dt_s) +
           (1.0f-alpha)*roll_acc;
    pitch = alpha * (f->pitch_deg + (s->gy_dps-f->bias_y_dps)*dt_s) +
            (1.0f-alpha)*pitch_acc;
    if (!Bounded(roll, 360.0f) || !Bounded(pitch, 360.0f)) { return false; }
    f->roll_deg = roll;
    f->pitch_deg = pitch;
    return true;
}
