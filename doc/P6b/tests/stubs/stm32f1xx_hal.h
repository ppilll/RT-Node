#ifndef P6B_HOST_HAL_H
#define P6B_HOST_HAL_H
#include "../../../P6a/tests/stubs/stm32f1xx_hal.h"
typedef struct { uint32_t Prescaler, Reload; } HostIwdgInit;
typedef struct { void *Instance; HostIwdgInit Init; } IWDG_HandleTypeDef;
#define IWDG ((void *)4)
#define IWDG_PRESCALER_64 4U
int HAL_IWDG_Init(IWDG_HandleTypeDef *wdg);
int HAL_IWDG_Refresh(IWDG_HandleTypeDef *wdg);
#endif
