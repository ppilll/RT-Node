/* CAN-only native scaffolding. Actual can_app.c callbacks remain under test. */
#ifndef P7_CAN_QUEUE_STUB_H
#define P7_CAN_QUEUE_STUB_H
typedef int StaticQueue_t;
typedef void *QueueHandle_t;
#define pdPASS 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define portYIELD_FROM_ISR(woken) ((void)0)
static bool queue_fail, read_fail;
static uint32_t fifo_remaining, can_error;
static QueueHandle_t xQueueCreateStatic(uint32_t n, uint32_t size, uint8_t *mem, StaticQueue_t *q)
{ (void)n; (void)size; (void)mem; return q; }
static int xQueueReceive(QueueHandle_t q, void *frame, uint32_t wait)
{ (void)q; (void)frame; (void)wait; return 0; }
static int xQueueSendFromISR(QueueHandle_t q, const void *frame, BaseType_t *woken)
{ (void)q; (void)frame; (void)woken; return queue_fail ? 0 : pdPASS; }
typedef struct { uint32_t ESR; } HostCan;
static HostCan host_can_regs;
typedef struct {
    uint32_t Prescaler, Mode, SyncJumpWidth, TimeSeg1, TimeSeg2;
    uint32_t AutoBusOff, AutoRetransmission, ReceiveFifoLocked, TransmitFifoPriority;
} HostCanInit;
typedef struct { HostCan *Instance; HostCanInit Init; } CAN_HandleTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;
typedef CAN_RxHeaderTypeDef CAN_TxHeaderTypeDef;
typedef struct {
    uint32_t FilterBank, FilterMode, FilterScale, FilterIdHigh, FilterMaskIdHigh;
    uint32_t FilterMaskIdLow, FilterFIFOAssignment, FilterActivation;
} CAN_FilterTypeDef;
#define CAN1 (&host_can_regs)
#define CAN_ID_STD 0U
#define CAN_RTR_DATA 0U
#define CAN_ESR_BOFF 4U
#define CAN_RX_FIFO0 0U
#define CAN_FILTER_FIFO0 0U
#define CAN_MODE_NORMAL 0U
#define CAN_FILTERMODE_IDMASK 0U
#define CAN_FILTERSCALE_32BIT 0U
#define CAN_IT_RX_FIFO0_MSG_PENDING 1U
#define CAN_IT_RX_FIFO0_OVERRUN 2U
#define CAN_IT_BUSOFF 4U
#define CAN_IT_ERROR 8U
#define HAL_CAN_ERROR_RX_FOV0 0x200U
#define CAN_BTR_SJW_Pos 24U
#define CAN_BTR_TS1_Pos 16U
#define CAN_BTR_TS2_Pos 20U
#define ENABLE 1U
#define AFIO_MAPR_CAN_REMAP 0x6000U
#define GPIO_PIN_11 0x800U
#define GPIO_PIN_12 0x1000U
#define GPIOA ((void *)3)
#define GPIO_NOPULL 0U
#define GPIO_MODE_AF_PP 1U
#define GPIO_SPEED_FREQ_HIGH 1U
#define Speed Pull
#define USB_LP_CAN1_RX0_IRQn 1U
#define CAN1_SCE_IRQn 2U
#define HAL_NVIC_DisableIRQ(irq) ((void)0)
#define HAL_NVIC_EnableIRQ(irq) ((void)0)
#define HAL_NVIC_ClearPendingIRQ(irq) ((void)0)
#define HAL_NVIC_SetPriority(irq, preempt, sub) ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_CAN1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_CAN1_FORCE_RESET() ((void)0)
#define __HAL_RCC_CAN1_RELEASE_RESET() ((void)0)
#define HAL_RCC_GetPCLK1Freq() 36000000U
#define HAL_CAN_Init(can) HAL_OK
#define HAL_CAN_Start(can) HAL_OK
#define HAL_CAN_ConfigFilter(can, filter) HAL_OK
#define HAL_CAN_ActivateNotification(can, flags) HAL_OK
#define HAL_CAN_GetError(can) can_error
#define HAL_CAN_GetTxMailboxesFreeLevel(can) 1U
#define HAL_CAN_AddTxMessage(can, header, data, mailbox) HAL_OK
#define HAL_CAN_IRQHandler(can) ((void)0)
#define HAL_CAN_GetRxFifoFillLevel(can, fifo) fifo_remaining
#define xTaskGetTickCountFromISR() host_tick
static int HAL_CAN_GetRxMessage(CAN_HandleTypeDef *can, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    (void)can; (void)fifo;
    if (read_fail) { return 1; }
    --fifo_remaining;
    memset(header, 0, sizeof(*header)); memset(data, 0, 8U);
    header->StdId = 0x183U; header->DLC = 8U; data[0] = 1U;
    return HAL_OK;
}
#endif
