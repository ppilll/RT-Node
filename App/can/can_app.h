#ifndef RT_NODE_CAN_APP_H
#define RT_NODE_CAN_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_id, last_rx_tick, rx_count;
    uint32_t request_tick;
    uint16_t requested_pan_us, requested_tilt_us;
    bool valid, request_valid;
} CanDisplaySnapshot;
/* Last frame dequeued by CanTask (including rejected format/content).
 * Requests are decoded STD/DATA/DLC8 0x180 values, NOT control acceptance. */
bool CanApp_GetDisplaySnapshot(CanDisplaySnapshot *out);
typedef struct {
    uint32_t queue_fail_events, overrun_callback_events, read_fail_events;
} CanRxEvents;
/* Saturating event counts, not lost-frame counts. Does not take overflow. */
void CanApp_GetRxEvents(CanRxEvents *out);

/* Once after Imu_Init, before scheduler start. No RX notifications here. */
bool CanApp_Init(void);
/* ControlTask consumes ISR overflow before any normal output in its cycle. */
bool CanApp_TakeOverflow(void);

#endif
