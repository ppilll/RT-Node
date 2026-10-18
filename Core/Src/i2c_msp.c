#include "stm32f1xx_hal.h"

/* Each task initializes only its own controller. Do not reset the shared
 * GPIO bank, remap PB6/PB7, scan the bus, or address the onboard 24C02. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *i2c)
{
    GPIO_InitTypeDef gpio = {0};
    if (i2c->Instance == I2C1)
    {
        __HAL_RCC_I2C1_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    }
    else if (i2c->Instance == I2C2)
    {
        __HAL_RCC_I2C2_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    }
    else { return; }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL; /* External pull-ups to 3.3 V. */
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}
