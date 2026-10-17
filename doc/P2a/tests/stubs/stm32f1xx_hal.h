#ifndef P2A_HOST_HAL_H
#define P2A_HOST_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef struct {
    uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS;
    uint32_t BaudRatePrescaler, FirstBit, TIMode, CRCCalculation, CRCPolynomial;
} SPI_InitTypeDef;
typedef struct { void *Instance; SPI_InitTypeDef Init; } SPI_HandleTypeDef;
#define SPI1 ((void *)1)
#define GPIOA ((void *)2)
#define GPIOC ((void *)3)
#define GPIO_PIN_4 16U
#define GPIO_PIN_5 32U
#define GPIO_PIN_6 64U
#define GPIO_PIN_7 128U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_MODE_INPUT 3U
#define GPIO_SPEED_FREQ_HIGH 3U
#define GPIO_NOPULL 0U
#define SPI_MODE_MASTER 1U
#define SPI_DIRECTION_2LINES 2U
#define SPI_DATASIZE_8BIT 8U
#define SPI_POLARITY_HIGH 1U
#define SPI_PHASE_2EDGE 2U
#define SPI_NSS_SOFT 1U
#define SPI_BAUDRATEPRESCALER_128 128U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_TIMODE_DISABLE 0U
#define SPI_CRCCALCULATION_DISABLE 0U
#define __get_IPSR() 0U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SPI1_CLK_ENABLE() ((void)0)
void HostSpiEnable(int enabled);
#define __HAL_SPI_ENABLE(x) HostSpiEnable(1)
#define __HAL_SPI_DISABLE(x) HostSpiEnable(0)
void HAL_SPI_MspInit(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
    uint8_t *tx, uint8_t *rx, uint16_t size, uint32_t timeout);
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState value);
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
#endif
