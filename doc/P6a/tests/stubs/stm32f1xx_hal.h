#ifndef P6A_HOST_HAL_H
#define P6A_HOST_HAL_H
#include "../../../P5b/tests/stubs/stm32f1xx_hal.h"
typedef struct { uint32_t CSR; } HostRcc;
typedef struct { uint32_t CR; } HostPwr;
typedef struct { uint32_t DR1, DR2; } HostBkp;
typedef struct { uint32_t CR; } HostFlash;
extern HostRcc host_rcc;
extern HostPwr host_pwr;
extern HostBkp host_bkp;
extern HostFlash host_flash;
#define RCC (&host_rcc)
#define PWR (&host_pwr)
#define BKP (&host_bkp)
#define FLASH (&host_flash)
#define RCC_CSR_PINRSTF (1UL << 26)
#define RCC_CSR_PORRSTF (1UL << 27)
#define RCC_CSR_SFTRSTF (1UL << 28)
#define RCC_CSR_IWDGRSTF (1UL << 29)
#define RCC_CSR_WWDGRSTF (1UL << 30)
#define RCC_CSR_LPWRRSTF (1UL << 31)
#define __HAL_RCC_CLEAR_RESET_FLAGS() (host_rcc.CSR = 0U)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_BKP_CLK_ENABLE() ((void)0)
#define __DMB() ((void)0)
#define PWR_CR_DBP 0x100U
#define FLASH_CR_LOCK 0x80U
#define FLASH_TYPEPROGRAM_HALFWORD 1U
typedef int HAL_StatusTypeDef;
#define HAL_ERROR 1
void HAL_PWR_EnableBkUpAccess(void);
void HAL_PWR_DisableBkUpAccess(void);
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data);
#ifdef P6A_UART_HOST
/* Hardware-only UART/DMA scaffolding; parser, reply and reset handling remain
 * the actual production C. No peripheral initialization is tested here. */
#define Speed Pull
#define GPIO_PIN_9 512U
#define GPIO_PIN_10 1024U
#define GPIO_MODE_AF_PP 1U
#define GPIO_SPEED_FREQ_HIGH 1U
#define GPIO_NOPULL 0U
#define GPIOA ((void *)3)
#define AFIO_MAPR_USART1_REMAP 4U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
typedef struct { uint32_t SR, CR1, CR3; } HostUsart;
typedef struct { uint32_t CCR, CNDTR; } HostDmaChannel;
typedef struct { uint32_t ISR; } HostDma;
typedef struct { uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling; } HostUartInit;
typedef struct { HostUsart *Instance; HostUartInit Init; uint32_t RxState; } UART_HandleTypeDef;
typedef struct { uint32_t Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority; } HostDmaInit;
typedef struct { HostDmaChannel *Instance; HostDmaInit Init; uint32_t State; } DMA_HandleTypeDef;
extern HostUsart host_usart;
extern HostDmaChannel host_dma_channel;
extern HostDma host_dma;
extern unsigned host_resets;
#define USART1 (&host_usart)
#define DMA1 (&host_dma)
#define DMA1_Channel5 (&host_dma_channel)
#define USART1_IRQn 1U
#define DMA1_Channel5_IRQn 2U
#define USART_SR_PE 1U
#define USART_SR_FE 2U
#define USART_SR_NE 4U
#define USART_SR_ORE 8U
#define USART_SR_IDLE 16U
#define USART_SR_RXNE 32U
#define USART_CR1_IDLEIE 16U
#define USART_CR1_PEIE 32U
#define USART_CR3_DMAR 64U
#define USART_CR3_EIE 128U
#define DMA_ISR_TCIF5 1U
#define DMA_ISR_TEIF5 2U
#define DMA_CCR_EN 1U
#define UART_IT_IDLE 16U
#define UART_WORDLENGTH_8B 0U
#define UART_STOPBITS_1 0U
#define UART_PARITY_NONE 0U
#define UART_MODE_TX_RX 0U
#define UART_HWCONTROL_NONE 0U
#define UART_OVERSAMPLING_16 0U
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 0U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_BYTE 0U
#define DMA_CIRCULAR 0U
#define DMA_PRIORITY_HIGH 0U
#define HAL_DMA_STATE_READY 0U
#define HAL_DMA_STATE_BUSY 1U
#define HAL_UART_STATE_READY 0U
#define CLEAR_BIT(reg, bit) ((reg) &= ~(bit))
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->Instance->CNDTR)
#define __HAL_DMA_DISABLE(dma) ((dma)->Instance->CCR &= ~DMA_CCR_EN)
#define __HAL_UART_CLEAR_IDLEFLAG(uart) ((uart)->Instance->SR &= ~USART_SR_IDLE)
#define __HAL_UART_DISABLE_IT(uart, it) ((uart)->Instance->CR1 &= ~(it))
#define __HAL_UART_ENABLE_IT(uart, it) ((uart)->Instance->CR1 |= (it))
#define __HAL_UART_GET_IT_SOURCE(uart, it) ((uart)->Instance->CR1 & (it))
#define __HAL_LINKDMA(uart, field, dma) ((void)0)
#define HAL_NVIC_DisableIRQ(irq) ((void)0)
#define HAL_NVIC_EnableIRQ(irq) ((void)0)
#define HAL_NVIC_ClearPendingIRQ(irq) ((void)0)
#define HAL_NVIC_SetPriority(irq, preempt, sub) ((void)0)
#define HAL_UART_Init(uart) HAL_OK
#define HAL_DMA_Init(dma) HAL_OK
#define HAL_UART_Receive_DMA(uart, data, size) HAL_OK
#define HAL_UART_IRQHandler(uart) ((void)0)
#define HAL_DMA_IRQHandler(dma) ((void)0)
#define NVIC_SystemReset() (++host_resets)
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size, uint32_t timeout);
#endif
#endif
