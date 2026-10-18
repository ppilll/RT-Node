#include "can_app.h"
#include "can_timing.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "../control/control.h"
#include "../control/control_config.h"
#include "imu.h"
#include <string.h>

#if P1A_AUTOTEST_ENABLE || P1A_ACTUATOR_TEST_ENABLE || P1B_AUTOTEST_ENABLE
#error "P3b requires historical P1/P2 output tests OFF"
#endif
#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0) || \
    (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "P3a requires static-only allocation and 32-bit 1kHz ticks"
#endif
#if (6 < configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) || \
    (6 > configLIBRARY_LOWEST_INTERRUPT_PRIORITY)
#error "P3a IRQ priority must permit FreeRTOS FromISR APIs"
#endif

#define CAN_CONTROL_ID       0x180U
#define CAN_MODE_ID          0x181U
#define CAN_STOP_ID          0x182U
#define CAN_HEARTBEAT_ID     0x183U
#define CAN_STATUS_ID        0x280U
#define CAN_QUEUE_DEPTH      8U
#define CAN_STACK_WORDS      384U
#define CAN_STATUS_TICKS     100U
#define CAN_HEARTBEAT_TICKS  500U

typedef char CanTickWidthMustBe32[(sizeof(TickType_t) == sizeof(uint32_t)) ? 1 : -1];
typedef struct
{
    uint32_t id, ide, rtr, dlc;
    TickType_t received_tick;
    uint8_t data[8];
} CanRxFrame;

static CAN_HandleTypeDef s_can;
static StaticQueue_t s_queue_storage;
static uint8_t s_queue_bytes[CAN_QUEUE_DEPTH * sizeof(CanRxFrame)];
static QueueHandle_t s_queue;
static StaticTask_t s_tcb;
static StackType_t s_stack[CAN_STACK_WORDS];
static TaskHandle_t s_task;
/* ISR writes only these aligned words; task consumes overflow under BASEPRI. */
static volatile uint32_t s_rx_overflow;
static volatile uint32_t s_can_error;
static volatile uint32_t s_start_failed;
static CanDisplaySnapshot s_display;

bool CanApp_GetDisplaySnapshot(CanDisplaySnapshot *out)
{
    if (out == NULL) { return false; }
    taskENTER_CRITICAL();
    *out = s_display;
    taskEXIT_CRITICAL();
    return out->valid;
}

/* Heartbeat is independent of the ControlTask command/session lifetime. */
static bool s_has_heartbeat;
static uint16_t s_heartbeat_counter;
static TickType_t s_heartbeat_tick;

bool CanApp_TakeOverflow(void)
{
    bool overflow;
    taskENTER_CRITICAL();
    overflow = s_rx_overflow != 0U;
    s_rx_overflow = 0U;
    taskEXIT_CRITICAL();
    return overflow;
}

void USB_LP_CAN1_RX0_IRQHandler(void);
void CAN1_SCE_IRQHandler(void);
static void CanTask(void *argument);

static uint16_t Read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool IsNew16(uint16_t next, uint16_t previous)
{
    uint16_t delta = (uint16_t)(next - previous);
    return (delta != 0U) && (delta < 0x8000U);
}

static bool ReservedZero(const uint8_t *p)
{
    return (p[4] | p[5] | p[6] | p[7]) == 0U;
}

/* true requests an event STATUS. Invalid frames have no protocol side effect. */
static bool ProcessFrame(const CanRxFrame *frame)
{
    const uint8_t *p = frame->data;
    uint16_t action;
    taskENTER_CRITICAL();
    s_display.last_id = frame->id;
    s_display.last_rx_tick = frame->received_tick;
    s_display.rx_count++;
    s_display.valid = true;
    if (frame->ide == CAN_ID_STD && frame->rtr == CAN_RTR_DATA &&
        frame->dlc == 8U && frame->id == CAN_CONTROL_ID)
    {
        s_display.requested_pan_us = Read16(p + 4);
        s_display.requested_tilt_us = Read16(p + 6);
        s_display.request_tick = frame->received_tick;
        s_display.request_valid = true;
    }
    taskEXIT_CRITICAL();
    if ((frame->ide != CAN_ID_STD) || (frame->rtr != CAN_RTR_DATA) ||
        (frame->dlc != 8U)) { return false; }
    switch (frame->id)
    {
    case CAN_CONTROL_ID:
    {
        uint16_t epoch = Read16(p);
        uint16_t seq = Read16(p + 2);
        uint16_t pan = Read16(p + 4);
        uint16_t tilt = Read16(p + 6);
        Control_PostRemote(epoch, seq, pan, tilt, frame->received_tick);
        return false;
    }
    case CAN_MODE_ID:
        if ((p[0] != 1U) || (p[1] > 2U) || (p[6] != 0U) || (p[7] != 0U))
        { return false; }
        Control_PostMode(p[1], Read16(p + 2), Read16(p + 4), frame->received_tick);
        return false; /* STATUS acceptance comes only from ControlTask. */
    case CAN_STOP_ID:
        if ((p[0] != 1U) || (p[1] != 1U) || !ReservedZero(p)) { return false; }
        Control_PostStop(Read16(p + 2));
        return false;
    case CAN_HEARTBEAT_ID:
        if ((p[0] != 1U) || (p[1] != 0U) || !ReservedZero(p)) { return false; }
        action = Read16(p + 2);
        if (!s_has_heartbeat || IsNew16(action, s_heartbeat_counter))
        {
            s_has_heartbeat = true;
            s_heartbeat_counter = action;
            s_heartbeat_tick = frame->received_tick;
        }
        return false;
    default:
        return false;
    }
}

static uint32_t MakeStatus(uint8_t *data)
{
    ControlStatusSnapshot control;
    ImuAttitudeSnapshot imu;
    bool imu_available;
    TickType_t now;
    Control_GetStatus(&control);
    imu_available = Imu_GetSnapshot(&imu);
    /* Read time AFTER snapshot copies: a preemption cannot make a new IMU
     * sample appear to have a timestamp in the future. */
    now = xTaskGetTickCount();
    memset(data, 0, 8U);
    data[0] = 1U;
    data[1] = (uint8_t)control.state;
    data[2] = control.reason;
    if (control.enabled) { data[3] |= 0x01U; }
    if (control.remote_fresh && (uint32_t)(now - control.remote_tick) < 100U)
    { data[3] |= 0x08U; }
    data[4] = (uint8_t)control.epoch;
    data[5] = (uint8_t)(control.epoch >> 8);
    if (imu_available && (imu.state == IMU_READY) && imu.valid &&
        ((uint32_t)(now - imu.sample_tick) < 40U)) { data[3] |= 0x02U; }
    if (s_has_heartbeat && ((TickType_t)(now - s_heartbeat_tick) < CAN_HEARTBEAT_TICKS))
    { data[3] |= 0x04U; }
    /* ErrorCode is historical/sticky; ESR reflects actual bus-off/recovery. */
    if ((s_can.Instance->ESR & CAN_ESR_BOFF) != 0U) { data[3] |= 0x10U; }
    if (control.action_accepted) { data[3] |= 0x20U; }
    data[6] = (uint8_t)control.last_action;
    data[7] = (uint8_t)(control.last_action >> 8);
    return control.revision;
}

static void SendStatus(uint8_t *data)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;
    if (((s_can.Instance->ESR & CAN_ESR_BOFF) != 0U) ||
        (HAL_CAN_GetTxMailboxesFreeLevel(&s_can) == 0U)) { return; }
    header.StdId = CAN_STATUS_ID;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = 8U;
    /* One non-blocking attempt. Full/error drops this snapshot; next event
     * or 100-tick deadline retries the latest state. No TX interrupt/queue. */
    (void)HAL_CAN_AddTxMessage(&s_can, &header, data, &mailbox);
}

static void CanTask(void *argument)
{
    CanRxFrame frame;
    uint8_t data[8], previous[8] = {0};
    TickType_t periodic_tick, now, elapsed, wait_ticks;
    bool event = true;
    uint32_t revision, previous_revision = 0U;
    bool started;
    (void)argument;
    configASSERT(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    /* NVIC and peripheral RX notification become live ONLY in this task. */
    started = HAL_CAN_Start(&s_can) == HAL_OK;
    if (started)
    {
        HAL_NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
        HAL_NVIC_ClearPendingIRQ(CAN1_SCE_IRQn);
        HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
        HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
        started = HAL_CAN_ActivateNotification(&s_can,
            CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN |
            CAN_IT_BUSOFF | CAN_IT_ERROR) == HAL_OK;
    }
    if (!started)
    {
        HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
        HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
        s_can_error = HAL_CAN_GetError(&s_can);
        s_start_failed = 1U;
    }
    periodic_tick = xTaskGetTickCount();
    for (;;)
    {
        revision = MakeStatus(data);
        event = event || revision != previous_revision;
        previous_revision = revision;
        now = xTaskGetTickCount();
        elapsed = (TickType_t)(now - periodic_tick);
        if (event || (elapsed >= CAN_STATUS_TICKS) || (memcmp(data, previous, 8U) != 0))
        {
            if (started) { SendStatus(data); }
            memcpy(previous, data, 8U);
            event = false;
        }
        if (elapsed >= CAN_STATUS_TICKS) { periodic_tick = now; elapsed = 0U; }
        wait_ticks = CAN_STATUS_TICKS - elapsed;
        if (wait_ticks > 20U) { wait_ticks = 20U; }
        if (xQueueReceive(s_queue, &frame, wait_ticks) == pdPASS)
        {
            event = ProcessFrame(&frame);
        }
    }
}

bool CanApp_Init(void)
{
    CAN_FilterTypeDef filter = {0};
    if ((s_queue != NULL) || (s_task != NULL) ||
        (HAL_RCC_GetPCLK1Freq() != 36000000U) ||
        ((AFIO->MAPR & AFIO_MAPR_CAN_REMAP) != 0U) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) { return false; }
    s_queue = xQueueCreateStatic(CAN_QUEUE_DEPTH, sizeof(CanRxFrame),
                                 s_queue_bytes, &s_queue_storage);
    if (s_queue == NULL) { return false; }
    s_can.Instance = CAN1;
    s_can.Init.Prescaler = P3A_CAN_PRESCALER;
    s_can.Init.Mode = CAN_MODE_NORMAL;
    s_can.Init.SyncJumpWidth = (P3A_CAN_SJW_TQ - 1UL) << CAN_BTR_SJW_Pos;
    s_can.Init.TimeSeg1 = (P3A_CAN_BS1_TQ - 1UL) << CAN_BTR_TS1_Pos;
    s_can.Init.TimeSeg2 = (P3A_CAN_BS2_TQ - 1UL) << CAN_BTR_TS2_Pos;
    s_can.Init.AutoBusOff = ENABLE;
    s_can.Init.AutoRetransmission = ENABLE;
    s_can.Init.ReceiveFifoLocked = ENABLE;
    s_can.Init.TransmitFifoPriority = ENABLE;
    if (HAL_CAN_Init(&s_can) != HAL_OK) { return false; }
    filter.FilterBank = 0U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = CAN_CONTROL_ID << 5;
    filter.FilterMaskIdHigh = 0x7FCU << 5;
    /* Standard ID occupies bits31:21. Low-word IDE bit2/RTR bit1 must be 0. */
    filter.FilterMaskIdLow = 0x0006U;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(&s_can, &filter) != HAL_OK) { return false; }
    s_task = xTaskCreateStatic(CanTask, "CanTask", CAN_STACK_WORDS, NULL,
                              tskIDLE_PRIORITY, s_stack, &s_tcb);
    return s_task != NULL;
}

void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    GPIO_InitTypeDef gpio = {0};
    if (hcan != &s_can) { return; }
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_CAN1_FORCE_RESET();
    __HAL_RCC_CAN1_RELEASE_RESET();
    /* Preserve the existing TIM3/SWJ MAPR configuration: never write/remap
     * AFIO here. Reject a non-default CAN mapping rather than guessing. */
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 6U, 0U);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 6U, 0U);
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&s_can);
}

void CAN1_SCE_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&s_can);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t woken = pdFALSE;
    uint32_t n;
    if (hcan != &s_can) { return; }
    /* bxCAN FIFO depth is three. Bound work even under continuous traffic. */
    for (n = 0U; n < 3U; n++)
    {
        CAN_RxHeaderTypeDef header;
        CanRxFrame frame;
        if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) == 0U) { break; }
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, frame.data) != HAL_OK)
        { s_rx_overflow = 1U; break; }
        frame.received_tick = xTaskGetTickCountFromISR();
        frame.id = (header.IDE == CAN_ID_STD) ? header.StdId : header.ExtId;
        frame.ide = header.IDE;
        frame.rtr = header.RTR;
        frame.dlc = header.DLC;
        if (xQueueSendFromISR(s_queue, &frame, &woken) != pdPASS)
        { s_rx_overflow = 1U; }
    }
    portYIELD_FROM_ISR(woken);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan != &s_can) { return; }
    s_can_error = HAL_CAN_GetError(hcan);
    if ((s_can_error & HAL_CAN_ERROR_RX_FOV0) != 0U) { s_rx_overflow = 1U; }
}
