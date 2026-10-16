#include "stm32f1xx_hal.h"
#include "p1a_safety.h"
#include "actuator.h"
#include "../../App/p1a_test/p1a_test.h"

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

    if (!Actuator_InitSafe())
    {
        P1a_FatalError();
    }
    for (;;)
    {
#if P1A_ACTUATOR_TEST_ENABLE
        P1aTest_Poll();
#else
        /* Default image has no caller of Enable/Apply and no scheduler. */
        __WFI();
#endif
    }
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
