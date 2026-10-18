#ifndef RT_NODE_CAN_APP_H
#define RT_NODE_CAN_APP_H

#include <stdbool.h>

/* Once after Imu_Init, before scheduler start. No RX notifications here. */
bool CanApp_Init(void);
/* ControlTask consumes ISR overflow before any normal output in its cycle. */
bool CanApp_TakeOverflow(void);

#endif
