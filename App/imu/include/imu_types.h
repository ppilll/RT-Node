#ifndef RT_NODE_IMU_TYPES_H
#define RT_NODE_IMU_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMU_NOT_STARTED = 0, IMU_INITIALIZING, IMU_CALIBRATING,
    IMU_READY, IMU_FAILED
} ImuState;

typedef struct {
    float roll_deg;
    float pitch_deg;
    uint32_t sample_tick; /* Last successful burst completion, 1 kHz modulo ticks. */
    uint32_t generation;  /* Every publication, including invalid state changes. */
    bool valid;
    ImuState state;
} ImuAttitudeSnapshot;

#endif
