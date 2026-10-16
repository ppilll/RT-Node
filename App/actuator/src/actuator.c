/* Selective migration from VisionArm-MCU 92d8f45bc9c7df55f6e2abea4b4abfeb090a3e3d
 * Visionarm_MCU/App/actuator/src/actuator.c. See doc/P1/C2_handoff.md.
 * P1a: single main-context owner, no scheduler/interrupt-driven PWM.
 */
#include "actuator.h"
#include <stddef.h>
#include "stm32f1xx_hal.h"
#include "p1a_safety.h"

#define PIN_MASK       (GPIO_PIN_6 | GPIO_PIN_7)
#define PIN_MODE_MASK  0xFF000000UL
#define PIN_SAFE_MODE  0x22000000UL
#define PIN_AF_MODE    0xAA000000UL /* AF push-pull, 2 MHz */
#define CHANNELS       (TIM_CCER_CC1E | TIM_CCER_CC2E)
#define PWM_CONFIG     (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | \
                        TIM_CCMR1_OC1PE | TIM_CCMR1_OC2M_1 | \
                        TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2PE)

static ActuatorSnapshot s_state;
static bool s_center_pending;
/* Only written 0->1, never cleared by Init/Disable. Aligned 32-bit store.
 * Emergency does hardware work first; no structure copy/counter RMW needed.
 */
static volatile uint32_t s_fault_latched;

static uint32_t Lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return saved;
}

static void Unlock(uint32_t saved)
{
    __DMB();
    __set_PRIMASK(saved);
}

void Actuator_ForceSafeOutput(void)
{
    P1a_ForceSafeOutput();
    s_fault_latched = 1U;
    __DMB();
}

static bool Fail(void)
{
    Actuator_ForceSafeOutput();
    return false;
}

static bool ClockIsValid(void)
{
    /* Reject a changed clock tree; CMSIS/HAL values are software checks only. */
    return (SystemCoreClock == 72000000U) &&
           (__HAL_RCC_GET_SYSCLK_SOURCE() == RCC_SYSCLKSOURCE_STATUS_PLLCLK) &&
           ((RCC->CR & (RCC_CR_HSERDY | RCC_CR_PLLRDY)) ==
                       (RCC_CR_HSERDY | RCC_CR_PLLRDY)) &&
           ((RCC->CFGR & (RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL)) ==
                         (RCC_PLLSOURCE_HSE | RCC_PLL_MUL9)) &&
           ((RCC->CFGR & RCC_CFGR_HPRE) == RCC_SYSCLK_DIV1) &&
           ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_HCLK_DIV2) &&
           ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_HCLK_DIV1) &&
           (HAL_RCC_GetPCLK1Freq() == 36000000U) &&
           (HAL_RCC_GetPCLK2Freq() == 72000000U);
}

static bool ConfigIsValid(bool running)
{
    uint32_t expected_cr1 = TIM_CR1_ARPE;
    uint32_t expected_pins = PIN_SAFE_MODE;
    uint32_t expected_ccer = 0U;
    if (running)
    {
        expected_cr1 |= TIM_CR1_CEN;
        expected_pins = PIN_AF_MODE;
        expected_ccer = CHANNELS;
    }
    return ClockIsValid() &&
           ((RCC->APB1ENR & RCC_APB1ENR_TIM3EN) != 0U) &&
           ((RCC->APB2ENR & (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPCEN)) ==
                             (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPCEN)) &&
           ((AFIO->MAPR & AFIO_MAPR_TIM3_REMAP) == AFIO_MAPR_TIM3_REMAP_FULLREMAP) &&
           ((GPIOC->CRL & PIN_MODE_MASK) == expected_pins) &&
           ((GPIOC->ODR & PIN_MASK) == 0U) &&
           (TIM3->CR1 == expected_cr1) && (TIM3->CR2 == 0U) &&
           (TIM3->SMCR == 0U) && (TIM3->DIER == 0U) &&
           (TIM3->CCER == expected_ccer) && (TIM3->CCMR1 == PWM_CONFIG) &&
           (TIM3->CCMR2 == 0U) && (TIM3->PSC == 71U) &&
           (TIM3->ARR == ACTUATOR_PWM_PERIOD_US - 1U) &&
           (TIM3->CCR1 == s_state.applied_pan_us) &&
           (TIM3->CCR2 == s_state.applied_tilt_us);
}

bool Actuator_InitSafe(void)
{
    uint32_t saved;
    uint32_t mapr;
    bool ok;
    if (__get_IPSR() != 0U)
    {
        return Fail();
    }
    saved = Lock();
    if ((s_fault_latched != 0U) || s_state.enabled)
    {
        ok = Fail();
    }
    else if (s_state.initialized)
    {
        ok = ConfigIsValid(false);
        if (!ok) { (void)Fail(); }
    }
    else
    {
        P1a_ForceSafeOutput();
        ok = ClockIsValid();
        if (ok)
        {
            RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
            (void)RCC->APB2ENR;
            /* SWJ_CFG readback is undefined. NEVER copy it or use this HAL's
             * remap macro (which ORs 111). Preserve all other MAPR bits and
             * explicitly select 000 = full JTAG + SWD. Only P1a writes MAPR.
             */
            mapr = AFIO->MAPR & ~(AFIO_MAPR_SWJ_CFG | AFIO_MAPR_TIM3_REMAP);
            AFIO->MAPR = mapr | AFIO_MAPR_SWJ_CFG_RESET | AFIO_MAPR_TIM3_REMAP_FULLREMAP;
            __DSB();

            RCC->APB1RSTR |= RCC_APB1RSTR_TIM3RST;
            (void)RCC->APB1RSTR;
            RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM3RST;
            (void)RCC->APB1RSTR;
            /* Direct register subset avoids HAL MSP callbacks, locks and
             * per-channel Start calls. No HAL_TIM handle/state is used.
             */
            TIM3->CR1 = TIM_CR1_ARPE;
            TIM3->CR2 = 0U;
            TIM3->SMCR = 0U;
            TIM3->DIER = 0U;
            TIM3->CCER = 0U;
            TIM3->CCMR1 = PWM_CONFIG;
            TIM3->CCMR2 = 0U;
            TIM3->PSC = 71U; /* 72 MHz / (71 + 1) = 1 MHz */
            TIM3->ARR = ACTUATOR_PWM_PERIOD_US - 1U;
            TIM3->CCR1 = ACTUATOR_PAN_CENTER_US;
            TIM3->CCR2 = ACTUATOR_TILT_CENTER_US;
            TIM3->EGR = TIM_EGR_UG; /* transfer ARR/PSC/CCR, reset CNT/PSC */
            TIM3->SR = 0U;
            s_state.applied_pan_us = ACTUATOR_PAN_CENTER_US;
            s_state.applied_tilt_us = ACTUATOR_TILT_CENTER_US;
            ok = ConfigIsValid(false);
        }
        if (ok) { s_state.initialized = true; }
        else { (void)Fail(); }
    }
    Unlock(saved);
    return ok;
}

bool Actuator_Enable(void)
{
    uint32_t saved;
    bool ok;
    if (__get_IPSR() != 0U) { return Fail(); }
    saved = Lock();
    if ((s_fault_latched != 0U) || !s_state.initialized || s_state.enabled ||
        !ConfigIsValid(false))
    {
        ok = Fail();
    }
    else
    {
        /* Active CCR=0 gives a LOW opening frame. Install center in preload
         * AFTER UG: first natural overflow starts both complete center pulses.
         * No high pulse is extended by GP->AF switching or CEN sequencing.
         */
        TIM3->CCR1 = 0U;
        TIM3->CCR2 = 0U;
        TIM3->EGR = TIM_EGR_UG;
        TIM3->SR = 0U;
        TIM3->CCR1 = ACTUATOR_PAN_CENTER_US;
        TIM3->CCR2 = ACTUATOR_TILT_CENTER_US;
        s_state.applied_pan_us = ACTUATOR_PAN_CENTER_US;
        s_state.applied_tilt_us = ACTUATOR_TILT_CENTER_US;
        TIM3->CCER = CHANNELS;
        GPIOC->CRL = (GPIOC->CRL & ~PIN_MODE_MASK) | PIN_AF_MODE;
        __DSB();
        TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
        __DSB();
        ok = ConfigIsValid(true);
        if (ok)
        {
            s_state.enabled = true;
            s_center_pending = true;
            s_state.enable_count++;
        }
        else { (void)Fail(); }
    }
    Unlock(saved);
    return ok;
}

static uint16_t ClampPulse(int32_t requested, uint16_t minimum,
                           uint16_t maximum, bool *clamped)
{
    *clamped = true;
    if (requested < (int32_t)minimum) { return minimum; }
    if (requested > (int32_t)maximum) { return maximum; }
    *clamped = false;
    return (uint16_t)requested;
}

bool Actuator_Apply(const ActuatorCommand *command, ActuatorApplyResult *result)
{
    ActuatorApplyResult local = {0};
    uint32_t saved;
    bool ok;
    if (__get_IPSR() != 0U)
    {
        ok = Fail(); /* hardware first, even for a rejected ISR caller */
        if (result != NULL) { *result = local; }
        return ok;
    }
    saved = Lock();
    if (command != NULL)
    {
        local.requested_pan_us = command->pan_position_us;
        local.requested_tilt_us = command->tilt_position_us;
    }
    if ((command == NULL) || (s_fault_latched != 0U) ||
        !s_state.initialized || !s_state.enabled || !ConfigIsValid(true) ||
        (s_center_pending && ((TIM3->SR & TIM_SR_UIF) == 0U)))
    {
        ok = Fail();
    }
    else
    {
        /* UIF proves the first natural update loaded center. Even an immediate
         * new Apply now transfers only at the following update, preserving it.
         */
        s_center_pending = false;
        local.applied_pan_us = ClampPulse(local.requested_pan_us,
            ACTUATOR_PAN_SAFE_MIN_US, ACTUATOR_PAN_SAFE_MAX_US, &local.pan_saturated);
        local.applied_tilt_us = ClampPulse(local.requested_tilt_us,
            ACTUATOR_TILT_SAFE_MIN_US, ACTUATOR_TILT_SAFE_MAX_US, &local.tilt_saturated);
        /* UDIS blocks shadow transfers, NOT the counter/compare output.
         * An overflow inside this bounded window retains BOTH old targets;
         * both new CCRs transfer on the next overflow. Never UG while running.
         * PRIMASK bounds this window to these four MMIO operations; NMI/fault
         * goes to non-returning fatal shutdown, so stale RMW cannot resume.
         */
        TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN | TIM_CR1_UDIS;
        TIM3->CCR1 = local.applied_pan_us;
        TIM3->CCR2 = local.applied_tilt_us;
        TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
        __DSB();
        s_state.applied_pan_us = local.applied_pan_us;
        s_state.applied_tilt_us = local.applied_tilt_us;
        ok = ConfigIsValid(true);
        if (ok)
        {
            s_state.command_count++;
            if (local.pan_saturated) { s_state.pan_saturation_count++; }
            if (local.tilt_saturated) { s_state.tilt_saturation_count++; }
        }
        else { (void)Fail(); }
    }
    if (!ok)
    {
        /* No successfully accepted target on failure. */
        local.applied_pan_us = 0U;
        local.applied_tilt_us = 0U;
    }
    if (result != NULL) { *result = local; }
    Unlock(saved);
    return ok;
}

void Actuator_Disable(void)
{
    uint32_t saved;
    if (__get_IPSR() != 0U) { (void)Fail(); return; }
    saved = Lock();
    P1a_ForceSafeOutput();
    if (s_state.enabled && (s_fault_latched == 0U)) { s_state.disable_count++; }
    s_state.enabled = false;
    s_center_pending = false;
    s_state.applied_pan_us = ACTUATOR_PAN_CENTER_US;
    s_state.applied_tilt_us = ACTUATOR_TILT_CENTER_US;
    /* Discard stale preload as well as the software target; no UG/output. */
    TIM3->CCR1 = ACTUATOR_PAN_CENTER_US;
    TIM3->CCR2 = ACTUATOR_TILT_CENTER_US;
    TIM3->CR1 &= ~TIM_CR1_UDIS;
    Unlock(saved);
}

void Actuator_GetSnapshot(ActuatorSnapshot *snapshot)
{
    uint32_t saved;
    if (snapshot == NULL) { return; }
    saved = Lock();
    *snapshot = s_state;
    snapshot->fault_latched = (s_fault_latched != 0U);
    snapshot->enabled = s_state.enabled && !snapshot->fault_latched;
    snapshot->fault_count = snapshot->fault_latched ? 1U : 0U;
    Unlock(saved);
}
