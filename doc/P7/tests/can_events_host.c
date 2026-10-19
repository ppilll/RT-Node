#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
uint32_t host_tick;
int host_scheduler = 2;
bool host_key0, host_key1, host_i2c_fail;
uint32_t host_tx_calls;
#include "../../../App/can/can_app.c"
HostDwt host_dwt;
HostDebug host_debug;
HostSysTick host_systick;
HostAfio host_afio;
bool Imu_GetSnapshot(ImuAttitudeSnapshot *out) { memset(out, 0, sizeof(*out)); return false; }
void Control_GetStatus(ControlStatusSnapshot *out) { memset(out, 0, sizeof(*out)); }
void Control_PostRemote(uint16_t e,uint16_t s,uint16_t p,uint16_t t,uint32_t tick)
{ (void)e;(void)s;(void)p;(void)t;(void)tick; }
void Control_PostMode(uint8_t m,uint16_t a,uint16_t e,uint32_t tick)
{ (void)m;(void)a;(void)e;(void)tick; }
void Control_PostStop(uint16_t a) { (void)a; }
int main(void)
{
    CanRxEvents events;
    CanDisplaySnapshot display;
    CanRxFrame frame = {0};
    host_scheduler = taskSCHEDULER_NOT_STARTED;
    assert(CanApp_Init()); host_scheduler = taskSCHEDULER_RUNNING;
    queue_fail = true; fifo_remaining = 3U;
    HAL_CAN_RxFifo0MsgPendingCallback(&s_can);
    assert(s_rx_overflow == 1U);
    CanApp_GetRxEvents(NULL); CanApp_GetRxEvents(&events);
    assert(events.queue_fail_events == 3U && s_rx_overflow == 1U);
    assert(CanApp_TakeOverflow() && !CanApp_TakeOverflow());
    read_fail = true; fifo_remaining = 1U;
    HAL_CAN_RxFifo0MsgPendingCallback(&s_can);
    CanApp_GetRxEvents(&events); assert(events.read_fail_events == 1U && s_rx_overflow == 1U);
    can_error = HAL_CAN_ERROR_RX_FOV0;
    HAL_CAN_ErrorCallback(&s_can); HAL_CAN_ErrorCallback(&s_can);
    CanApp_GetRxEvents(&events); assert(events.overrun_callback_events == 2U && s_rx_overflow == 1U);
    can_error = 0U; HAL_CAN_ErrorCallback(&s_can);
    CanApp_GetRxEvents(&events); assert(events.overrun_callback_events == 2U);
    assert(!ProcessFrame(&frame)); /* Even rejected format is processed. */
    CanApp_GetDisplaySnapshot(&display); assert(display.rx_count == 1U);
    s_rx_events.queue_fail_events = UINT32_MAX;
    s_rx_events.read_fail_events = UINT32_MAX;
    s_rx_events.overrun_callback_events = UINT32_MAX;
    read_fail = false; fifo_remaining = 1U; HAL_CAN_RxFifo0MsgPendingCallback(&s_can);
    read_fail = true; fifo_remaining = 1U; HAL_CAN_RxFifo0MsgPendingCallback(&s_can);
    can_error = HAL_CAN_ERROR_RX_FOV0; HAL_CAN_ErrorCallback(&s_can);
    CanApp_GetRxEvents(&events);
    assert(events.queue_fail_events == UINT32_MAX && events.read_fail_events == UINT32_MAX &&
           events.overrun_callback_events == UINT32_MAX && s_rx_overflow == 1U);
    puts("P7 CAN PASS: actual ISR callbacks/getter, queue/read/overrun events, saturation, rejected-frame rx_count; safety flag untouched");
    return 0;
}
