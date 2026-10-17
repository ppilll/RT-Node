#include "p1a_test.h"

#if P1A_AUTOTEST_ENABLE
#include "actuator.h"
#include "stm32f1xx_hal.h"
#include "p1a_safety.h"

#define AUTO_CYCLES_PER_MS 72000UL
#define AUTO_POLL_BUDGET   65536UL
#define AUTO_HOLD_MS       3040U /* >=3000 ms active, allowing preload latency */

static void AutoRequire(bool ok)
{
    if (!ok) { P1a_FatalError(); }
}

static bool AutoConditions(bool enabled, bool fault)
{
    ActuatorSnapshot state;
    Actuator_GetSnapshot(&state);
    return (__get_IPSR() == 0U) && (__get_PRIMASK() == 0U) &&
        (SystemCoreClock == 72000000U) &&
        (__HAL_RCC_GET_SYSCLK_SOURCE() == RCC_SYSCLKSOURCE_STATUS_PLLCLK) &&
        ((RCC->CR & (RCC_CR_HSERDY | RCC_CR_PLLRDY)) ==
                    (RCC_CR_HSERDY | RCC_CR_PLLRDY)) &&
        ((RCC->CFGR & (RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                      RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) ==
                     (RCC_PLLSOURCE_HSE | RCC_PLL_MUL9 | RCC_HCLK_DIV2)) &&
        ((SysTick->CTRL & (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk |
                          SysTick_CTRL_CLKSOURCE_Msk)) ==
                         (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk |
                          SysTick_CTRL_CLKSOURCE_Msk)) &&
        (SysTick->LOAD == AUTO_CYCLES_PER_MS - 1U) &&
        ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0U) &&
        ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) &&
        state.initialized && (state.enabled == enabled) &&
        (state.fault_latched == fault) &&
        ((RCC->APB1ENR & RCC_APB1ENR_TIM3EN) != 0U) &&
        ((RCC->APB2ENR & (RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN)) ==
                         (RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN)) &&
        ((AFIO->MAPR & AFIO_MAPR_TIM3_REMAP) == AFIO_MAPR_TIM3_REMAP_FULLREMAP) &&
        ((GPIOC->CRL & 0xFF000000UL) == (enabled ? 0xAA000000UL : 0x22000000UL)) &&
        ((GPIOC->ODR & (GPIO_PIN_6 | GPIO_PIN_7)) == 0U) &&
        (TIM3->CR1 == (enabled ? (TIM_CR1_ARPE | TIM_CR1_CEN) : TIM_CR1_ARPE)) &&
        (TIM3->CCER == (enabled ? (TIM_CCER_CC1E | TIM_CCER_CC2E) : 0U)) &&
        (TIM3->PSC == 71U) && (TIM3->ARR == ACTUATOR_PWM_PERIOD_US - 1U) &&
        (TIM3->CR2 == 0U) && (TIM3->SMCR == 0U) && (TIM3->CCMR2 == 0U) &&
        (TIM3->CCMR1 == (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE |
                         TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2PE)) &&
        (TIM3->CNT < ACTUATOR_PWM_PERIOD_US) && (TIM3->DIER == 0U) &&
        (TIM3->CCR1 == state.applied_pan_us) &&
        (TIM3->CCR2 == state.applied_tilt_us);
}

static void AutoWait(uint32_t milliseconds, bool enabled)
{
    uint32_t start_cycle = DWT->CYCCNT;
    uint32_t start_tick = HAL_GetTick();
    uint32_t step;
    uint32_t poll;
    uint32_t cycles;
    uint32_t ticks;
    uint32_t start_counter = TIM3->CNT;
    uint32_t counter;
    uint32_t expected_counter;
    uint32_t phase_error;

    AutoRequire((milliseconds != 0U) && (milliseconds <= AUTO_HOLD_MS));
    /* Finite nested loops, even if BOTH time bases stop. Each 1 ms milestone
     * gets a fixed CPU poll budget; no HAL_Delay/WFI while PWM is enabled.
     * Unsigned subtraction supports CYCCNT and HAL Tick rollover.
     */
    for (step = 1U; step <= milliseconds + 2U; step++)
    {
        for (poll = 0U; poll < AUTO_POLL_BUDGET; poll++)
        {
            if ((uint32_t)(DWT->CYCCNT - start_cycle) >= step * AUTO_CYCLES_PER_MS)
            {
                break;
            }
        }
        AutoRequire(poll < AUTO_POLL_BUDGET);
        AutoRequire(AutoConditions(enabled, false));
        if (enabled)
        {
            cycles = (uint32_t)(DWT->CYCCNT - start_cycle);
            counter = TIM3->CNT;
            expected_counter = (start_counter + cycles / 72U) % ACTUATOR_PWM_PERIOD_US;
            phase_error = (counter + ACTUATOR_PWM_PERIOD_US - expected_counter) %
                          ACTUATOR_PWM_PERIOD_US;
            /* Allow 100 us for read/ISR skew, reject a stalled/wrong-rate TIM3.
             * This is a software consistency check, not a measured waveform.
             */
            AutoRequire((phase_error <= 100U) ||
                        (phase_error >= ACTUATOR_PWM_PERIOD_US - 100U));
        }
        cycles = (uint32_t)(DWT->CYCCNT - start_cycle) / AUTO_CYCLES_PER_MS;
        ticks = (uint32_t)(HAL_GetTick() - start_tick);
        /* Reject stalled/fast Tick or unexpected execution delay. DWT and
         * Tick agree within 10 ms; all waits also have a +20 ms deadline.
         */
        AutoRequire((cycles <= milliseconds + 20U) && (ticks <= milliseconds + 20U));
        AutoRequire((cycles <= ticks + 10U) && (ticks <= cycles + 10U));
        if ((cycles >= milliseconds) && (ticks >= milliseconds)) { return; }
    }
    AutoRequire(false);
}

static void AutoEnable(void)
{
    ActuatorSnapshot state;
    AutoRequire(Actuator_Enable());
    Actuator_GetSnapshot(&state);
    AutoRequire((state.applied_pan_us == ACTUATOR_PAN_CENTER_US) &&
                (state.applied_tilt_us == ACTUATOR_TILT_CENTER_US));
    /* LOW opening frame (20 ms), then one COMPLETE center frame (20 ms). */
    AutoWait(40U, true);
    AutoRequire((TIM3->SR & TIM_SR_UIF) != 0U);
}

#if P1A_AUTOTEST_CASE == 2
static void AutoNearBoundary(void)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t poll;
    AutoRequire(AutoConditions(true, false));
    /* Read-only phase selection. Apply retains sole ownership of UDIS/CCRs.
     * CNT is sampled BEFORE the API: this does not prove MMIO at overflow.
     * If TIM3 or DWT stalls, the time limit / finite poll budget faults closed.
     */
    for (poll = 0U; poll < 32U * AUTO_POLL_BUDGET; poll++)
    {
        AutoRequire((uint32_t)(DWT->CYCCNT - start) < 25U * AUTO_CYCLES_PER_MS);
        if ((TIM3->CNT >= 19980U) && (TIM3->CNT < ACTUATOR_PWM_PERIOD_US))
        {
            return;
        }
    }
    AutoRequire(false);
}
#endif

static void AutoTarget(int32_t pan, int32_t tilt, uint16_t expected_pan,
                       uint16_t expected_tilt, bool boundary)
{
    ActuatorCommand command;
    ActuatorApplyResult result;
    command.pan_position_us = pan;
    command.tilt_position_us = tilt;
    AutoRequire(AutoConditions(true, false));
#if P1A_AUTOTEST_CASE == 2
    if (boundary) { AutoNearBoundary(); }
#else
    (void)boundary;
#endif
    AutoRequire(Actuator_Apply(&command, &result));
    AutoRequire((result.requested_pan_us == pan) && (result.requested_tilt_us == tilt) &&
                (result.applied_pan_us == expected_pan) &&
                (result.applied_tilt_us == expected_tilt) &&
                (result.pan_saturated == (pan != (int32_t)expected_pan)) &&
                (result.tilt_saturated == (tilt != (int32_t)expected_tilt)));
    /* Next natural overflow, possibly one extra old frame under UDIS, then
     * >=3 s at the requested active pair before any subsequent action.
     */
    AutoWait(AUTO_HOLD_MS, true);
}

void P1aAutotest_Run(void)
{
#if P1A_AUTOTEST_CASE == 3
    ActuatorCommand command;
    ActuatorApplyResult result;
#endif
    AutoRequire((__get_IPSR() == 0U) && (__get_PRIMASK() == 0U));
    /* Cortex-M3 cycle counter works without a debugger. Never reset it or
     * touch TIM3 to manufacture timing success; failure is a latched stop.
     */
    AutoRequire((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    AutoWait(2000U, false);
    AutoEnable();
    AutoWait(AUTO_HOLD_MS, true);

#if P1A_AUTOTEST_CASE == 1
    AutoTarget(1000, 1500, 1000U, 1500U, false);
    AutoTarget(2000, 1500, 2000U, 1500U, false);
    AutoTarget(1500, 1200, 1500U, 1200U, false);
    AutoTarget(1500, 1600, 1500U, 1600U, false);
#elif P1A_AUTOTEST_CASE == 2
    AutoTarget(0, 0, 1000U, 1200U, false);
    AutoTarget(3000, 3000, 2000U, 1600U, false);
    AutoTarget(1000, 1600, 1000U, 1600U, false);
    AutoTarget(2000, 1200, 2000U, 1200U, true);
    AutoTarget(1000, 1600, 1000U, 1600U, true);
#else
    AutoTarget(1000, 1600, 1000U, 1600U, false);
    Actuator_Disable();
    AutoWait(2000U, false);
    AutoEnable();
    AutoWait(AUTO_HOLD_MS, true);
    AutoTarget(2000, 1200, 2000U, 1200U, false);
    Actuator_ForceSafeOutput(); /* Controlled software fault; latch retained. */
    AutoRequire(AutoConditions(false, true));
    AutoRequire(!Actuator_Enable());
    AutoRequire(AutoConditions(false, true));
    command.pan_position_us = 1500;
    command.tilt_position_us = 1500;
    AutoRequire(!Actuator_Apply(&command, &result));
    AutoRequire((result.applied_pan_us == 0U) && (result.applied_tilt_us == 0U));
    AutoRequire(AutoConditions(false, true));
    AutoRequire(!Actuator_InitSafe());
    AutoRequire(AutoConditions(false, true));
#endif
    Actuator_Disable();
    AutoRequire(AutoConditions(false, P1A_AUTOTEST_CASE == 3));
    /* Return to main's permanent disabled WFI. No restart without MCU reset. */
}
#endif

#if P1A_ACTUATOR_TEST_ENABLE
#include "stm32f1xx_hal.h"

volatile P1aTestMailbox g_p1a_test;
static uint32_t s_wait_sequence;
static uint32_t s_wait_count;

void P1aTest_Poll(void)
{
    ActuatorCommand command;
    ActuatorApplyResult result = {0};
    ActuatorSnapshot snapshot;
    uint32_t sequence = g_p1a_test.request_seq;
    uint32_t action;
    uint32_t key;
    bool ok;

    if (sequence != g_p1a_test.ack_seq)
    {
        __DMB();
        action = g_p1a_test.action;
        key = g_p1a_test.enable_key;
        command.pan_position_us = g_p1a_test.pan_us;
        command.tilt_position_us = g_p1a_test.tilt_us;
        if (action == 6U)
        {
            if (sequence != s_wait_sequence)
            {
                s_wait_sequence = sequence;
                s_wait_count = 0U;
            }
            /* Poll without blocking interrupts; finite poll budget also
             * fails closed if TIM3 stalls. No HAL tick timeout dependency.
             */
            if (((TIM3->CR1 & TIM_CR1_CEN) != 0U) && (TIM3->CNT < 19980U))
            {
                s_wait_count++;
                if (s_wait_count < 65536U)
                {
                    g_p1a_test.status = 4U;
                    return;
                }
                Actuator_ForceSafeOutput();
                action = 3U; /* Apply below refuses after sticky fault */
            }
        }
        g_p1a_test.enable_key = 0U;
        ok = false;
        g_p1a_test.status = 2U;
        switch (action)
        {
        case 1U:
            Actuator_Disable();
            ok = true;
            break;
        case 2U:
            if (key == P1A_TEST_ENABLE_KEY) { ok = Actuator_Enable(); }
            else
            {
                Actuator_Disable();
                g_p1a_test.status = 3U;
            }
            break;
        case 3U:
        case 6U:
            g_p1a_test.dispatch_counter = TIM3->CNT;
            ok = Actuator_Apply(&command, &result);
            break;
        case 4U:
            Actuator_ForceSafeOutput(); /* supervised software fault, returns */
            ok = true;
            break;
        case 5U:
            ok = Actuator_InitSafe();
            break;
        default:
            Actuator_Disable();
            g_p1a_test.status = 3U;
            break;
        }
        if (ok) { g_p1a_test.status = 1U; }
        g_p1a_test.result = result;
        Actuator_GetSnapshot(&snapshot);
        g_p1a_test.snapshot = snapshot;
        __DMB();
        g_p1a_test.ack_seq = sequence;
    }
}
#endif
