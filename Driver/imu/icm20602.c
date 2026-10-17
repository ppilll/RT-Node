/* Independently written for RT-Node. DS-000176 Rev 1.1 register protocol. */
#include "icm20602.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>

#define SPI_TIMEOUT_MS 3U
#define WHO_AM_I 0x75U
#define PWR_MGMT_1 0x6BU

static SPI_HandleTypeDef s_spi;
static bool s_bus_ok;
static bool s_ready;

/* Sole SPI1 MSP definition. Never change AFIO MAPR or PC6/PC7.
 * Protect GPIO RMW against higher-priority ControlTask safe-output writes. */
void HAL_SPI_MspInit(SPI_HandleTypeDef *spi);
void HAL_SPI_MspInit(SPI_HandleTypeDef *spi)
{
    GPIO_InitTypeDef gpio = {0};
    if (spi->Instance != SPI1) { return; }
    taskENTER_CRITICAL();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    taskEXIT_CRITICAL();
}

static void WaitTicks(TickType_t ticks)
{
    TickType_t start = xTaskGetTickCount();
    (void)xTaskDelayUntil(&start, ticks);
}

static Icm20602Result Transfer(uint8_t *tx, uint8_t *rx, uint16_t size)
{
    HAL_StatusTypeDef result;
    if (!s_bus_ok) { return ICM20602_IO_ERROR; }
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET);
    /* ONE HAL call, ONE shared timeout budget including final BSY wait. */
    result = HAL_SPI_TransmitReceive(&s_spi, tx, rx, size, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET);
    if (result != HAL_OK)
    {
        /* No abort/DMA waits or recovery. Stop clock generation on failure. */
        __HAL_SPI_DISABLE(&s_spi);
        s_bus_ok = false;
        s_ready = false;
        return ICM20602_IO_ERROR;
    }
    return ICM20602_OK;
}

static Icm20602Result ReadReg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = {0}, rx[2] = {0};
    Icm20602Result result;
    tx[0] = reg | 0x80U;
    result = Transfer(tx, rx, 2U);
    if (result == ICM20602_OK) { *value = rx[1]; }
    return result;
}

static Icm20602Result WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2], rx[2];
    tx[0] = reg & 0x7FU;
    tx[1] = value;
    return Transfer(tx, rx, 2U);
}

Icm20602Result Icm20602_Init(void)
{
    /* FCHOICE_B=00 + DLPF_CFG=3 => 1kHz / (9+1) = 100Hz.
     * ACCEL_FCHOICE_B=0 + A_DLPF_CFG=3 => 44.8Hz bandwidth.
     * No self-test, FSYNC, FIFO, DMP or data-ready interrupt. */
    static const uint8_t config[][2] = {
        {0x70U, 0x40U}, /* I2C_IF_DIS after reset */
        {PWR_MGMT_1, 0x01U}, {0x6CU, 0x00U},
        {0x1AU, 0x03U}, {0x19U, 9U}, {0x1BU, 0x00U},
        {0x1CU, 0x00U}, {0x1DU, 0x03U},
        {0x23U, 0x00U}, {0x38U, 0x00U}, {0x6AU, 0x00U}
    };
    uint32_t n;
    uint8_t value = 0U;
    Icm20602Result result;
    s_ready = false;
    s_bus_ok = false;
    s_spi.Instance = SPI1;
    s_spi.Init.Mode = SPI_MODE_MASTER;
    s_spi.Init.Direction = SPI_DIRECTION_2LINES;
    s_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_spi.Init.CLKPolarity = SPI_POLARITY_HIGH;
    s_spi.Init.CLKPhase = SPI_PHASE_2EDGE;
    s_spi.Init.NSS = SPI_NSS_SOFT;
    s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    s_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_spi.Init.CRCPolynomial = 7U;
    if (HAL_SPI_Init(&s_spi) != HAL_OK) { return ICM20602_IO_ERROR; }
    __HAL_SPI_ENABLE(&s_spi); /* Establish Mode3 idle clock before first CS. */
    s_bus_ok = true;
    WaitTicks(100U);
    for (n = 0U; n < 2U; n++)
    {
        result = ReadReg(WHO_AM_I, &value);
        if (result != ICM20602_OK) { return result; }
        if (value == 0x12U) { break; }
        if (n == 0U) { WaitTicks(10U); }
    }
    if (value != 0x12U) { return ICM20602_ID_MISMATCH; }
    result = WriteReg(PWR_MGMT_1, 0x80U);
    if (result != ICM20602_OK) { return result; }
    WaitTicks(100U);
    for (n = 0U; n < 10U; n++)
    {
        result = ReadReg(PWR_MGMT_1, &value);
        if (result != ICM20602_OK) { return result; }
        if ((value & 0x80U) == 0U) { break; }
        if (n < 9U) { WaitTicks(10U); }
    }
    if ((value & 0x80U) != 0U) { return ICM20602_CONFIG_ERROR; }
    result = ReadReg(WHO_AM_I, &value);
    if (result != ICM20602_OK) { return result; }
    if (value != 0x12U) { return ICM20602_ID_MISMATCH; }
    for (n = 0U; n < sizeof(config)/sizeof(config[0]); n++)
    {
        result = WriteReg(config[n][0], config[n][1]);
        if (result != ICM20602_OK) { return result; }
    }
    /* Conservative gyro startup settling, yielding CPU to ControlTask. */
    WaitTicks(100U);
    for (n = 0U; n < sizeof(config)/sizeof(config[0]); n++)
    {
        result = ReadReg(config[n][0], &value);
        if (result != ICM20602_OK) { return result; }
        if (value != config[n][1]) { return ICM20602_CONFIG_ERROR; }
    }
    s_ready = true;
    return ICM20602_OK;
}

Icm20602Result Icm20602_ReadRaw(Icm20602Raw *out)
{
    uint8_t tx[15] = {0}, rx[15];
    Icm20602Result result;
    if (!s_ready || out == NULL) { return ICM20602_IO_ERROR; }
    tx[0] = 0xBBU;
    result = Transfer(tx, rx, 15U);
    if (result == ICM20602_OK) { Icm20602_Decode(rx + 1, out); }
    return result;
}
