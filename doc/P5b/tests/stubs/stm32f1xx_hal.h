#ifndef P5B_HOST_HAL_H
#define P5B_HOST_HAL_H
#include <stdint.h>
#define GPIO_PIN_3 8U
#define GPIO_PIN_4 16U
#define GPIO_MODE_INPUT 0U
#define GPIO_PULLUP 1U
#define GPIO_PIN_RESET 0U
#define GPIOE ((void *)1)
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
typedef struct { uint32_t Pin, Mode, Pull; } GPIO_InitTypeDef;
extern bool host_key0, host_key1;
static void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *init) { (void)port; (void)init; }
static uint32_t HAL_GPIO_ReadPin(void *port, uint32_t pin)
{ (void)port; return (pin == GPIO_PIN_4 ? host_key0 : host_key1) ? 0U : 1U; }
typedef struct { uint32_t CTRL, CYCCNT; } HostDwt;
typedef struct { uint32_t DEMCR; } HostDebug;
typedef struct { uint32_t LOAD; } HostSysTick;
typedef struct { uint32_t MAPR; } HostAfio;
extern HostDwt host_dwt;
extern HostDebug host_debug;
extern HostSysTick host_systick;
extern HostAfio host_afio;
#define DWT (&host_dwt)
#define CoreDebug (&host_debug)
#define SysTick (&host_systick)
#define AFIO (&host_afio)
#define DWT_CTRL_NOCYCCNT_Msk 2U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __NOP() ((void)0)
#define __get_IPSR() 0U
#define __get_PRIMASK() 0U
#define __get_BASEPRI() 0U
#define NVIC_PRIORITYGROUP_4 3U
#define HAL_NVIC_GetPriorityGrouping() 3U
#define SystemCoreClock 72000000U
#define I2C1 ((void *)2)
#define I2C_DUTYCYCLE_2 0U
#define I2C_ADDRESSINGMODE_7BIT 0U
#define I2C_DUALADDRESS_DISABLE 0U
#define I2C_GENERALCALL_DISABLE 0U
#define I2C_NOSTRETCH_DISABLE 0U
#define I2C_FLAG_BUSY 1U
#define I2C1_REMAP 2U
#define AFIO_MAPR_I2C1_REMAP I2C1_REMAP
#define HAL_I2C_STATE_READY 0U
#define RESET 0U
#define HAL_OK 0
typedef struct {
    uint32_t ClockSpeed, DutyCycle, AddressingMode, DualAddressMode;
    uint32_t GeneralCallMode, NoStretchMode;
} HostI2cInit;
typedef struct { void *Instance; HostI2cInit Init; uint32_t State; } I2C_HandleTypeDef;
extern bool host_i2c_fail;
extern uint32_t host_tx_calls;
#define __HAL_I2C_GET_FLAG(handle, flag) 0U
static uint32_t HAL_GetTick(void) { return host_tick; }
static int HAL_I2C_Init(I2C_HandleTypeDef *i2c) { i2c->State = 0U; return HAL_OK; }
static int HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t address,
                                  uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)i2c;
    assert(address == 0x78U && size >= 2U && size <= 17U && timeout == 5U);
    assert(data[0] == 0U || data[0] == 0x40U);
    host_tx_calls++;
    return host_i2c_fail ? 1 : HAL_OK;
}
#endif
