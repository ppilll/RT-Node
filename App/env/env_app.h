#ifndef ENV_APP_H
#define ENV_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t temperature_centi_c;  /* 0.01 deg C */
    uint32_t humidity_centi_pct;  /* 0.01 %RH, 0..10000 */
    uint32_t pressure_pa;         /* Pa, 30000..110000 */
    uint32_t sample_tick;         /* Last successful sample, 1 kHz RTOS tick */
    bool valid;                  /* Latest acquisition succeeded */
} EnvSnapshot;

/* Startup only: software/static task creation, no hardware IO. */
bool EnvApp_Init(void);
/* Task context only. False for NULL or before the first publication. */
bool EnvApp_GetSnapshot(EnvSnapshot *out);

#endif
