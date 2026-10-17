#include "imu_fusion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ImuSample Tilt(float roll, float pitch)
{
    ImuSample s = {0};
    float r = roll * 0.0174532925199433f;
    float p = pitch * 0.0174532925199433f;
    s.ax_g = -sinf(p);
    s.ay_g = sinf(r)*cosf(p);
    s.az_g = cosf(r)*cosf(p);
    return s;
}

static void Calibrate(ImuFusion *f, ImuSample s)
{
    unsigned i;
    ImuFusion_Reset(f);
    for (i = 0; i < 99; i++) { assert(!ImuFusion_Calibrate(f, &s)); }
    assert(ImuFusion_Calibrate(f, &s));
}

int main(void)
{
    const uint8_t bytes[14] = {0x80,0, 0xFF,0xFF, 0x40,0,
                              0x7F,0xFF, 0,0x83, 0xFF,0x7D, 0,0};
    const float targets[5][2] = {{0,0},{15,0},{-15,0},{0,15},{0,-15}};
    const float dts[3] = {0.005f,0.010f,0.015f};
    Icm20602Raw raw;
    ImuSample s, level = Tilt(0,0);
    ImuFusion f, before;
    unsigned i, j;
    float t, target, rate, max_error = 0;
    Icm20602_Decode(bytes, &raw);
    assert(raw.ax == -32768 && raw.ay == -1 && raw.az == 16384);
    assert(raw.temp == 32767 && raw.gx == 131 && raw.gy == -131 && raw.gz == 0);
    Icm20602_ToUnits(&raw, &s);
    assert(s.ax_g == -2 && s.az_g == 1 && s.gx_dps == 1 && s.gy_dps == -1);
    puts("U01 PASS: real 14-byte decode, signed endpoints, 16384/g and 131/dps");
    for (i = 0; i < 5; i++)
    {
        Calibrate(&f, level);
        s = Tilt(targets[i][0], targets[i][1]);
        for (j = 0; j < 300; j++) { assert(ImuFusion_Update(&f, &s, 0.01f)); }
        printf("U02 target=(%.1f,%.1f), actual=(%.6f,%.6f) deg\n",
               targets[i][0], targets[i][1], f.roll_deg, f.pitch_deg);
        assert(fabsf(f.roll_deg-targets[i][0]) <= 2);
        assert(fabsf(f.pitch_deg-targets[i][1]) <= 2);
        /* Tilt at boot also initializes with the correct acceleration sign. */
        Calibrate(&f, s);
        assert(fabsf(f.roll_deg-targets[i][0]) < 0.001f);
        assert(fabsf(f.pitch_deg-targets[i][1]) < 0.001f);
    }
    puts("U02 PASS: level and +/-15deg roll/pitch, 3s convergence <=2deg");
    s = level; s.gx_dps = 1.5f; s.gy_dps = -2; s.gz_dps = 0.75f;
    Calibrate(&f, s);
    assert(fabsf(f.bias_x_dps-1.5f) < 0.0001f);
    assert(fabsf(f.bias_y_dps+2) < 0.0001f);
    assert(fabsf(f.bias_z_dps-0.75f) < 0.0001f);
    for (i = 0; i < 6000; i++) { assert(ImuFusion_Update(&f, &s, dts[i%3])); }
    assert(fabsf(f.roll_deg) < 0.001f && fabsf(f.pitch_deg) < 0.001f);
    /* Known trajectory: 0->15deg in 1s, return in 1s, then level 5s.
     * Accel projection follows actual elapsed dt; gyro includes learned bias. */
    t = 0;
    for (i = 0; t < 7; i++)
    {
        float dt = dts[i%3];
        t += dt;
        if (t <= 1) { target = 15*t; rate = 15; }
        else if (t <= 2) { target = 15*(2-t); rate = -15; }
        else { target = 0; rate = 0; }
        s = Tilt(target, -target);
        s.gx_dps = rate+1.5f; s.gy_dps = -rate-2; s.gz_dps = 0.75f;
        assert(ImuFusion_Update(&f, &s, dt));
        if (fabsf(f.roll_deg-target) > max_error) { max_error = fabsf(f.roll_deg-target); }
        assert(fabsf(f.roll_deg-target) <= 3 && fabsf(f.pitch_deg+target) <= 3);
    }
    assert(fabsf(f.roll_deg) < 0.1f && fabsf(f.pitch_deg) < 0.1f);
    printf("U03 PASS: 60s biased level + 7s trajectory, 5/10/15ms; max roll error %.6f deg\n", max_error);
    before = f;
    assert(!ImuFusion_Update(&f, &level, 0));
    assert(!ImuFusion_Update(&f, &level, 0.051f));
    assert(!ImuFusion_Update(&f, &level, NAN));
    s = level; s.ax_g = NAN; assert(!ImuFusion_Update(&f, &s, 0.01f));
    s = level; s.gx_dps = INFINITY; assert(!ImuFusion_Update(&f, &s, 0.01f));
    s = Tilt(0,0); s.az_g = 0; assert(!ImuFusion_Update(&f, &s, 0.01f));
    assert(memcmp(&before, &f, sizeof f) == 0);
    ImuFusion_Reset(&f);
    for (i = 0; i < 50; i++) { assert(!ImuFusion_Calibrate(&f, &level)); }
    s = level; s.gx_dps = 6; assert(!ImuFusion_Calibrate(&f, &s));
    assert(f.stationary_count == 0 && !f.calibrated);
    s = level; s.az_g = 1.3f; assert(!ImuFusion_Calibrate(&f, &s));
    assert(!ImuFusion_Update(&f, &level, 0.01f));
    puts("U04 math PASS: zero/long/NaN dt, NaN/Inf/zero-g, motion calibration reset, unchanged angles");
    return 0;
}
