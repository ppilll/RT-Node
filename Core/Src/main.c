#include "stm32f1xx_hal.h"
#include "p1a_safety.h"
#include "actuator.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../../App/control/control.h"
#include "../../App/control/control_config.h"
#include "imu.h"
#include "can_app.h"
#include "uart_app.h"
#include "adc_app.h"
#include "env_app.h"
#include "display_app.h"
#include "health_app.h"

/* P1b test authorization and P1a exclusion live in control_config.h. */
#if configTICK_RATE_HZ != 1000
#error "C1 shared HAL/RTOS SysTick requires 1000 Hz"
#endif
#if TICK_INT_PRIORITY != configLIBRARY_LOWEST_INTERRUPT_PRIORITY
#error "HAL and FreeRTOS SysTick must use the same lowest priority"
#endif
#if (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "C1 requires static-only RTOS allocation"
#endif
#if (configUSE_TICKLESS_IDLE != 0) || (configUSE_TIMERS != 0)
#error "C1 requires the shared periodic tick and no timer task"
#endif

#if HSE_VALUE != 8000000U
#error "P1a requires the confirmed 8 MHz HSE clock configuration"
#endif

static HAL_StatusTypeDef SystemClock_Config(void);

/* Internal cold-start/normal-disable primitive: deliberately does not latch.
 * Emergency callers must use Actuator_ForceSafeOutput / P1a_FatalError.
 */
void P1a_ForceSafeOutput(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    TIM3->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC2E);
    TIM3->CR1 &= ~TIM_CR1_CEN;

    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    (void)RCC->APB2ENR;
    /* Preload low before selecting general-purpose push-pull, 2 MHz. */
    GPIOC->BRR = GPIO_PIN_6 | GPIO_PIN_7;
    GPIOC->CRL = (GPIOC->CRL & ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6 |
                                GPIO_CRL_MODE7 | GPIO_CRL_CNF7)) |
                 GPIO_CRL_MODE6_1 | GPIO_CRL_MODE7_1;
    __DSB();
}

void P1a_FatalError(void)
{
    /* Hardware first; no HAL tick, RTOS, diagnostics or recovery needed. */
    Actuator_ForceSafeOutput();
    __disable_irq();
    for (;;)
    {
        Actuator_ForceSafeOutput();
    }
}

int main(void)
{
    /* First action after C runtime. Reset-time high impedance is unavoidable. */
    P1a_ForceSafeOutput();

    Health_BootCaptureReset();

    /* Read-only admission check BEFORE HAL/Flash. Hardware-auto IWDG and
     * corrupt option bytes need a separate startup assessment, never a new
     * attempt to program options or unconditional early feeding here. */
    if ((FLASH->OBR & (FLASH_OBR_IWDG_SW | FLASH_OBR_OPTERR)) != FLASH_OBR_IWDG_SW)
    { P1a_FatalError(); }

    /* The CMSIS template's initial value is not the reset HSI frequency. */
    SystemCoreClockUpdate();
    if (HAL_Init() != HAL_OK)
    {
        P1a_FatalError();
    }
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    /* This HAL version ignores HAL_InitTick's result inside HAL_Init. */
    if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK)
    {
        P1a_FatalError();
    }

    if (SystemClock_Config() != HAL_OK)
    {
        P1a_FatalError();
    }

    /* A storage error stays observable through UART, with authorization off. */
    (void)Health_BootFinalize();
    if (!Actuator_InitSafe())
    {
        P1a_FatalError();
    }
    if (!Control_Init())
    {
        P1a_FatalError();
    }
    if (!Health_InitTask())
    {
        P1a_FatalError();
    }
    if (!Imu_Init())
    {
        P1a_FatalError();
    }
    if (!CanApp_Init())
    {
        P1a_FatalError();
    }
    if (!UartApp_Init())
    {
        P1a_FatalError();
    }
    /* ADC failure is reported by UartTask; existing control stays available. */
    (void)AdcApp_Init();
    /* Environment is non-critical; I2C2 IO runs only after RTOS startup. */
    (void)EnvApp_Init();
    (void)DisplayApp_Init(); /* Noncritical OLED; KEY0 remains in ControlTask. */

    /* End the HAL-owned startup timebase before the port restarts the SAME
     * SysTick at 72 MHz / 1000. Clear any old pending tick so it cannot be
     * mistaken for the first RTOS tick. No HAL delay follows this handoff.
     * The reviewed RVDS port's prvStartFirstTask re-enables IRQs before SVC.
     */
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    __DSB();
    __ISB();
    vTaskStartScheduler();
    P1a_FatalError(); /* Idle creation/port failure or unexpected return. */
    return 0;
}

static HAL_StatusTypeDef SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    /* User-confirmed 8 MHz crystal: HSE / 1 * 9 = 72 MHz. */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        return HAL_ERROR;
    }

    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV2;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2) != HAL_OK)
    {
        return HAL_ERROR;
    }

    SystemCoreClockUpdate();
    if ((__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_PLLCLK) ||
        (SystemCoreClock != 72000000U) ||
        (HAL_RCC_GetPCLK1Freq() != 36000000U) ||
        (HAL_RCC_GetPCLK2Freq() != 72000000U))
    {
        return HAL_ERROR;
    }

    /* HAL_RCC_ClockConfig also ignores its internal HAL_InitTick result. */
    return HAL_InitTick(TICK_INT_PRIORITY);
}
