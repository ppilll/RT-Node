#include "adc_app.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define ADC_SIZE       64U
#define HALF_SIZE      32U
#define STALL_TICKS    200U

#if configTICK_RATE_HZ != 1000
#error "P4b uses the existing 1 kHz tick"
#endif

static ADC_HandleTypeDef s_adc;
static DMA_HandleTypeDef s_dma;
/* HAL's uint32_t* parameter conveys the address, NOT the DMA element width. */
static volatile uint16_t adc_dma[ADC_SIZE];
static volatile uint32_t s_wraps;
static volatile uint32_t s_pending;
static volatile AdcApp_Status s_status;
static AdcApp_Result s_latest;
static uint32_t s_consumed, s_last_progress;
static TickType_t s_progress_tick;
static bool s_active;

/* All clocks are enabled before this helper is used. No shared DMA reset,
 * no timer/ADC wait and no Control/CAN/actuator action on ADC failure. */
static void StopHardware(void)
{
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    HAL_NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    CLEAR_BIT(ADC1->CR2, ADC_CR2_EXTTRIG | ADC_CR2_DMA | ADC_CR2_ADON);
    __HAL_DMA_DISABLE(&s_dma);
    __HAL_DMA_DISABLE_IT(&s_dma, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE);
    DMA1->IFCR = DMA_IFCR_CGIF1;
    s_active = false;
    s_latest.valid = false;
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *adc)
{
    GPIO_InitTypeDef gpio = {0};
    if (adc != &s_adc) { return; }
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);
    /* CC2E enables the INTERNAL waveform. PA1 is never an AF output. */
}

bool AdcApp_Init(void)
{
    ADC_ChannelConfTypeDef channel = {0};
    if ((s_status != ADC_APP_NOT_READY) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) { return false; }
    s_status = ADC_APP_INIT_ERROR;
    if ((HAL_RCC_GetPCLK1Freq() != 36000000U) ||
        (HAL_RCC_GetPCLK2Freq() != 72000000U) ||
        (SystemCoreClock != 72000000U)) { return false; }

    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6); /* 72 / 6 = 12 MHz. */
    __HAL_RCC_DMA1_CLK_ENABLE(); /* Ch5 UART is already configured. */
    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 6U, 0U);
    s_dma.Instance = DMA1_Channel1;
    s_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_dma.Init.MemInc = DMA_MINC_ENABLE;
    s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    s_dma.Init.Mode = DMA_CIRCULAR;
    s_dma.Init.Priority = DMA_PRIORITY_MEDIUM;
    if (HAL_DMA_Init(&s_dma) != HAL_OK) { return false; }
    __HAL_LINKDMA(&s_adc, DMA_Handle, s_dma);

    /* Only TIM2 is owned here. No AFIO remap, GPIO AF, TIM3, timer IRQ or DMA. */
    TIM2->CR1 = 0U;
    TIM2->CR2 = 0U;
    TIM2->SMCR = 0U;
    TIM2->DIER = 0U;
    TIM2->CCER = 0U;
    TIM2->CCMR1 = TIM_CCMR1_OC2M; /* OC2M=111: PWM2; CC2S=00: output. */
    TIM2->CCMR2 = 0U;
    TIM2->PSC = 71U;  /* APB1 timer clock 72 MHz -> 1 MHz. */
    TIM2->ARR = 999U;
    TIM2->CCR2 = 500U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CNT = 0U;
    TIM2->SR = 0U;
    TIM2->CCER = TIM_CCER_CC2E; /* Active high, rising edge at count 500. */

    s_adc.Instance = ADC1;
    s_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_adc.Init.ContinuousConvMode = DISABLE;
    s_adc.Init.NbrOfConversion = 1U;
    s_adc.Init.DiscontinuousConvMode = DISABLE;
    s_adc.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_CC2;
    if (HAL_ADC_Init(&s_adc) != HAL_OK) { StopHardware(); return false; }
    channel.Channel = ADC_CHANNEL_1;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    if ((HAL_ADC_ConfigChannel(&s_adc, &channel) != HAL_OK) ||
        (HAL_ADCEx_Calibration_Start(&s_adc) != HAL_OK))
    { StopHardware(); return false; }
    s_status = ADC_APP_READY;
    return true;
}

bool AdcApp_Start(void)
{
    if ((s_status != ADC_APP_READY) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)) { return false; }
    /* This HAL ignores HAL_DMA_Start_IT's return; verify before and after. */
    if ((s_dma.State != HAL_DMA_STATE_READY) ||
        (HAL_ADC_Start_DMA(&s_adc, (uint32_t *)adc_dma, ADC_SIZE) != HAL_OK) ||
        (s_dma.State != HAL_DMA_STATE_BUSY) ||
        ((s_dma.Instance->CCR & DMA_CCR_EN) == 0U) ||
        ((ADC1->CR2 & (ADC_CR2_DMA | ADC_CR2_EXTTRIG)) !=
                     (ADC_CR2_DMA | ADC_CR2_EXTTRIG)))
    {
        s_status = ADC_APP_START_ERROR;
        StopHardware();
        return false;
    }
    s_progress_tick = xTaskGetTickCount();
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    s_status = ADC_APP_RUNNING;
    s_active = true;
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    __DMB();
    SET_BIT(TIM2->CR1, TIM_CR1_CEN); /* Last: DMA/ADC now await CC2 edges. */
    return true;
}

void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_dma);
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &s_adc) { s_pending = 1U; }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc != &s_adc) { return; }
    s_wraps++;
    s_pending = 1U; /* Circular DMA continues, no restart and no summation. */
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &s_adc) { s_status = ADC_APP_DMA_ERROR; }
}

/* Task masks priority-6 IRQs; DMA remains live. HAL clears TC immediately
 * before its callback, so the masked task cannot observe that interval.
 * A pending TC is accounted for even before its ISR. NDTR==0 already counts
 * the whole ring. Sticky TC requires IRQ service at least once per 64 ms;
 * our critical section copies only 64 bytes and never waits/transmits. */
static uint32_t ProgressLocked(void)
{
    uint32_t before = DMA1->ISR & DMA_ISR_TCIF1;
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&s_dma);
    uint32_t after = DMA1->ISR & DMA_ISR_TCIF1;
    if (before != after) { remaining = __HAL_DMA_GET_COUNTER(&s_dma); }
    return s_wraps * ADC_SIZE + ADC_SIZE - remaining +
           (((after != 0U) && (remaining != 0U)) ? ADC_SIZE : 0U);
}

void AdcApp_Process(void)
{
    uint16_t copy[HALF_SIZE];
    uint32_t producer, complete, start, available, after, i, sum = 0U;
    bool accepted = false;
    TickType_t now;
    if (s_status != ADC_APP_RUNNING)
    {
        if (s_active) { StopHardware(); }
        return;
    }
    taskENTER_CRITICAL();
    producer = ProgressLocked();
    if ((DMA1->ISR & DMA_ISR_TEIF1) != 0U)
    { s_status = ADC_APP_DMA_ERROR; }
    else if (s_pending != 0U)
    {
        s_pending = 0U;
        complete = producer & ~(HALF_SIZE - 1U);
        available = complete - s_consumed; /* modulo 2^32, aligned to 32. */
        if (available != 0U)
        {
            /* Only the latest completed half can still be DMA-owned by us.
             * Discard older halves, even if scheduling missed a whole lap. */
            s_latest.missed += available / HALF_SIZE - 1U;
            start = complete - HALF_SIZE;
            for (i = 0U; i < HALF_SIZE; i++)
            { copy[i] = adc_dma[(start + i) % ADC_SIZE]; }
            __DMB();
            after = ProgressLocked();
            /* A byte copy under an IRQ mask alone is NOT ownership: reject
             * if DMA has written into this half since the first check. */
            accepted = ((after - start) <= ADC_SIZE) &&
                       ((DMA1->ISR & DMA_ISR_TEIF1) == 0U);
            s_consumed = complete;
            if (!accepted) { s_latest.missed++; }
        }
    }
    taskEXIT_CRITICAL();
    if (s_status != ADC_APP_RUNNING) { StopHardware(); return; }
    now = xTaskGetTickCount();
    if (producer != s_last_progress)
    { s_last_progress = producer; s_progress_tick = now; }
    else if ((TickType_t)(now - s_progress_tick) >= STALL_TICKS)
    { s_status = ADC_APP_STALLED; StopHardware(); return; }
    if (!accepted) { return; }
    /* Work on the private copy outside the critical section and the ISR. */
    for (i = 0U; i < HALF_SIZE; i++) { sum += copy[i]; }
    taskENTER_CRITICAL();
    s_latest.raw = copy[HALF_SIZE - 1U];
    s_latest.avg = (uint16_t)(sum / HALF_SIZE);
    s_latest.mv = (uint16_t)((uint32_t)s_latest.avg * 3300U / 4095U);
    s_latest.n += HALF_SIZE;
    s_latest.valid = true;
    taskEXIT_CRITICAL();
}

bool AdcApp_GetLatest(AdcApp_Result *out)
{
    if (out == NULL) { return false; }
    /* DisplayTask is now a second reader. UartTask alone still processes ADC. */
    taskENTER_CRITICAL();
    *out = s_latest;
    out->valid = out->valid && (s_status == ADC_APP_RUNNING);
    taskEXIT_CRITICAL();
    return out->valid;
}

AdcApp_Status AdcApp_GetStatus(void)
{
    return s_status;
}
