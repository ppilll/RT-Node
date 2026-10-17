#ifndef RT_NODE_IMU_FUSION_H
#define RT_NODE_IMU_FUSION_H

#include <stdbool.h>
#include <stdint.h>
#include "icm20602.h" /* Pure data types; no HAL/RTOS dependency. */

#define IMU_CALIBRATION_SAMPLES 100U
#define IMU_FUSION_TAU_S 0.5f

typedef struct {
    float bias_x_dps, bias_y_dps, bias_z_dps;
    float sum_x, sum_y, sum_z;
    float roll_deg, pitch_deg;
    uint32_t stationary_count;
    bool calibrated;
} ImuFusion;

void ImuFusion_Reset(ImuFusion *f);
/* 100 consecutive samples, norm 0.8..1.2g, each gyro <=5dps.
 * Motion resets the window. Caller enforces 3 second total deadline. */
bool ImuFusion_Calibrate(ImuFusion *f, const ImuSample *s);
/* True only for calibrated, finite sample and 0 < measured dt <=50ms.
 * Failure leaves angles unchanged; caller publishes invalid/FAILED. */
bool ImuFusion_Update(ImuFusion *f, const ImuSample *s, float dt_s);

#endif
