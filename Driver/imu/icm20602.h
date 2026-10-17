#ifndef RT_NODE_ICM20602_H
#define RT_NODE_ICM20602_H

#include <stdint.h>

typedef struct {
    int16_t ax, ay, az, temp, gx, gy, gz;
} Icm20602Raw;

typedef struct {
    float ax_g, ay_g, az_g;
    float gx_dps, gy_dps, gz_dps;
} ImuSample;

typedef enum {
    ICM20602_OK = 0, ICM20602_IO_ERROR,
    ICM20602_ID_MISMATCH, ICM20602_CONFIG_ERROR
} Icm20602Result;

/* Sole ImuTask owner. Init uses schedulable, finite RTOS waits. No retries
 * after IO error; no automatic recovery. ReadRaw writes out only on success. */
Icm20602Result Icm20602_Init(void);
Icm20602Result Icm20602_ReadRaw(Icm20602Raw *out);
/* Pure C, same implementations used by host tests. Non-NULL arguments. */
void Icm20602_Decode(const uint8_t bytes[14], Icm20602Raw *out);
void Icm20602_ToUnits(const Icm20602Raw *raw, ImuSample *out);

#endif
