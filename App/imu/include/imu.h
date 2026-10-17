#ifndef RT_NODE_IMU_H
#define RT_NODE_IMU_H

#include "imu_types.h"

/* Once, before scheduler start, after Control_Init. Creates no output. */
bool Imu_Init(void);
/* Task context only. false for NULL or no publication. Copy even if invalid;
 * consumers MUST check state, valid and (in a later phase) freshness. */
bool Imu_GetSnapshot(ImuAttitudeSnapshot *out);

#endif
