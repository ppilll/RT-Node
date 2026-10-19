#include "uart_app.h"
#include "adc_app.h"
#include "env_app.h"
#include "../health/health_app.h"
#include "../control/control.h"
#include "../can/can_app.h"
#include "../actuator/include/actuator.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#if (configTICK_RATE_HZ != 1000) || (configUSE_TASK_NOTIFICATIONS != 1) || \
    (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "P4a requires static tasks, notifications and the existing 1 kHz tick"
#endif
#if (6 < configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) || \
    (6 > configLIBRARY_LOWEST_INTERRUPT_PRIORITY)
#error "P4a IRQ priority must permit FreeRTOS FromISR APIs"
#endif

#define RX_SIZE       256U
#define LINE_SIZE     32U
#define STACK_WORDS   384U
#define POLL_TICKS    5U
#define RX_BUDGET     64U
#define ADC_REPORT_TICKS 200U
#define UART_ERRORS  (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)

static UART_HandleTypeDef s_uart;
static DMA_HandleTypeDef s_dma;
static StaticTask_t s_tcb;
static StackType_t s_stack[STACK_WORDS];
static TaskHandle_t s_task;
/* Volatile because the DMA engine, rather than C code, writes this memory. */
static volatile uint8_t s_rx[RX_SIZE];
static volatile uint32_t s_wraps;
static volatile uint32_t s_error_pending;
static volatile uint32_t s_stopped_progress;
static volatile uint32_t s_fault;
static volatile uint32_t s_min_stack_words = STACK_WORDS;
static uint32_t s_rx_base, s_consumer, s_overflow;
static char s_line[LINE_SIZE];
static uint32_t s_length;
static bool s_discard, s_cr_seen;
static bool s_safe_ready, s_reboot_pending;
static TickType_t s_safe_tick;
static UartDisplaySnapshot s_display;
static uint32_t ProgressLocked(void);

bool UartApp_GetDisplaySnapshot(UartDisplaySnapshot *out)
{
    if (out == NULL) { return false; }
    taskENTER_CRITICAL();
    *out = s_display;
    taskEXIT_CRITICAL();
    return out->valid;
}

static void PublishCommand(void)
{
    uint32_t i;
    char summary[16];
    for (i = 0U; i < sizeof(summary) - 1U && s_line[i] != '\0'; i++)
    { summary[i] = s_line[i]; }
    summary[i] = '\0';
    /* Fill the entire local array before copying its bounded contents. */
    while (++i < sizeof(summary)) { summary[i] = '\0'; }
    taskENTER_CRITICAL();
    memcpy(s_display.last_command, summary, sizeof(summary));
    s_display.last_command_tick = xTaskGetTickCount();
    s_display.rx_bytes = s_rx_base + ProgressLocked();
    s_display.ovf_count = s_overflow;
    s_display.valid = true;
    taskEXIT_CRITICAL();
}

void USART1_IRQHandler(void);
void DMA1_Channel5_IRQHandler(void);
static void UartTask(void *argument);

/* Caller masks priority-6 IRQs (task) or is a priority-6 ISR. DMA still runs.
 * HAL clears TC immediately before our TC callback; the task cannot observe
 * that interval. A pending TC adds one whole ring even before the callback.
 * Read TC around NDTR and retry NDTR if rollover intervened. NDTR==0 already
 * represents the completed ring, so do not add it twice before auto-reload.
 * IRQ service must occur at least once per 22.2 ms ring: sticky TC cannot
 * represent multiple laps while interrupts are masked. No such long masking
 * is introduced here. All progress/subtractions are uint32_t modulo 2^32;
 * RX_SIZE divides 2^32, so index and unread survive counter wrap naturally.
 */
static uint32_t ProgressLocked(void)
{
    uint32_t before = DMA1->ISR & DMA_ISR_TCIF5;
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&s_dma);
    uint32_t after = DMA1->ISR & DMA_ISR_TCIF5;
    if (before != after) { remaining = __HAL_DMA_GET_COUNTER(&s_dma); }
    return s_wraps * RX_SIZE + RX_SIZE - remaining +
           (((after != 0U) && (remaining != 0U)) ? RX_SIZE : 0U);
}

static void NotifyFromISR(void)
{
    BaseType_t woken = pdFALSE;
    /* NVIC is enabled only by UartTask, after the scheduler has started. */
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};
    if (uart != &s_uart) { return; }
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE(); /* Shared controller: never reset DMA1. */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_NVIC_SetPriority(USART1_IRQn, 6U, 0U);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 6U, 0U);
}

bool UartApp_Init(void)
{
    if ((s_task != NULL) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ||
        ((AFIO->MAPR & AFIO_MAPR_USART1_REMAP) != 0U)) { return false; }
    s_uart.Instance = USART1;
    s_uart.Init.BaudRate = 115200U;
    s_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits = UART_STOPBITS_1;
    s_uart.Init.Parity = UART_PARITY_NONE;
    s_uart.Init.Mode = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_uart) != HAL_OK) { return false; }
    s_dma.Instance = DMA1_Channel5;
    s_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_dma.Init.MemInc = DMA_MINC_ENABLE;
    s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_dma.Init.Mode = DMA_CIRCULAR;
    s_dma.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&s_dma) != HAL_OK) { return false; }
    __HAL_LINKDMA(&s_uart, hdmarx, s_dma);
    s_task = xTaskCreateStatic(UartTask, "UartTask", STACK_WORDS, NULL,
                              tskIDLE_PRIORITY, s_stack, &s_tcb);
    return s_task != NULL;
}

/* Normal IDLE must never read DR while RXNE is pending for DMA. Defer the
 * clear to the 5 ms task check in that case, masking IDLE to avoid an IRQ
 * storm. DMA remains enabled to drain RXNE. Error flags belong to HAL first.
 */
static void ClearIdleIfDrained(void)
{
    uint32_t status = s_uart.Instance->SR;
    if (((status & USART_SR_IDLE) != 0U) &&
        ((status & (USART_SR_RXNE | UART_ERRORS)) == 0U))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&s_uart); /* Repository macro: SR then DR. */
        __HAL_UART_ENABLE_IT(&s_uart, UART_IT_IDLE);
    }
}

void USART1_IRQHandler(void)
{
    uint32_t status = s_uart.Instance->SR;
    if (s_error_pending != 0U) { return; }
    if ((status & UART_ERRORS) != 0U)
    {
        /* Freeze progress before HAL abort clears pending DMA flags. HAL
         * still owns error handling, the abort and its completion callback. */
        __HAL_DMA_DISABLE(&s_dma);
        __DSB();
        s_stopped_progress = ProgressLocked();
        HAL_UART_IRQHandler(&s_uart);
        return;
    }
    if (((status & USART_SR_IDLE) != 0U) &&
        (__HAL_UART_GET_IT_SOURCE(&s_uart, UART_IT_IDLE) != RESET))
    {
        __HAL_UART_DISABLE_IT(&s_uart, UART_IT_IDLE);
        ClearIdleIfDrained();
        NotifyFromISR();
    }
}

void DMA1_Channel5_IRQHandler(void)
{
    if (((DMA1->ISR & DMA_ISR_TEIF5) != 0U) && (s_error_pending == 0U))
    { s_stopped_progress = ProgressLocked(); }
    HAL_DMA_IRQHandler(&s_dma);
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &s_uart) { NotifyFromISR(); }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != &s_uart) { return; }
    s_wraps++;
    NotifyFromISR(); /* Circular mode continues; never restart on TC. */
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != &s_uart) { return; }
    __HAL_UART_DISABLE_IT(uart, UART_IT_IDLE);
    CLEAR_BIT(uart->Instance->CR3, USART_CR3_DMAR);
    s_error_pending = 1U; /* Old HAL calls here after DMA abort has completed. */
    NotifyFromISR();
}

static void Fault(void)
{
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
    CLEAR_BIT(s_uart.Instance->CR1, USART_CR1_IDLEIE | USART_CR1_PEIE);
    CLEAR_BIT(s_uart.Instance->CR3, USART_CR3_DMAR | USART_CR3_EIE);
    __HAL_DMA_DISABLE(&s_dma);
    s_fault = 1U; /* UART only; no actuator/CAN ownership or STOP changes. */
}

static bool StartRx(void)
{
    /* Old HAL ignores HAL_DMA_Start_IT's result: verify READY before calling
     * and BUSY/EN/DMAR afterwards. Never repeatedly reset/reinitialize HAL. */
    if ((s_uart.RxState != HAL_UART_STATE_READY) ||
        (s_dma.State != HAL_DMA_STATE_READY)) { return false; }
    if (HAL_UART_Receive_DMA(&s_uart, (uint8_t *)s_rx, RX_SIZE) != HAL_OK)
    { return false; }
    if ((s_dma.State != HAL_DMA_STATE_BUSY) ||
        ((s_dma.Instance->CCR & DMA_CCR_EN) == 0U) ||
        ((s_uart.Instance->CR3 & USART_CR3_DMAR) == 0U)) { return false; }
    __HAL_UART_ENABLE_IT(&s_uart, UART_IT_IDLE);
    return true;
}

static bool RecoverRx(void)
{
    bool ok;
    taskENTER_CRITICAL();
    s_rx_base += s_stopped_progress;
    s_wraps = 0U;
    s_consumer = 0U;
    s_error_pending = 0U;
    s_length = 0U;
    s_cr_seen = false;
    s_discard = true;
    s_safe_ready = false;
    s_overflow++; /* Lost/error-damaged line; discard through the next LF. */
    ok = StartRx(); /* Exactly one task-context attempt for this error event. */
    taskEXIT_CRITICAL();
    return ok;
}

/* One byte per short critical section. Check again AFTER loading the byte:
 * DMA may overwrite it even while IRQs are masked. If that happened, never
 * feed the suspect byte (especially a newline) to the parser. Retain only
 * recent bytes and discard until LF; absolute counts detect equal-index
 * full laps, unlike a pair of modulo pointers. Keep the newest half-ring on
 * overflow to leave DMA headroom while resynchronizing under continuous RX. */
static bool TakeByte(uint8_t *byte)
{
    uint32_t producer, unread;
    bool available = false;
    taskENTER_CRITICAL();
    if (s_error_pending == 0U)
    {
        producer = ProgressLocked();
        unread = producer - s_consumer;
        if (unread != 0U)
        {
            if (unread <= RX_SIZE)
            {
                *byte = s_rx[s_consumer % RX_SIZE];
                __DMB();
                producer = ProgressLocked();
                available = (producer - s_consumer) <= RX_SIZE;
            }
            if (available) { s_consumer++; }
            else
            {
                s_consumer = producer - RX_SIZE / 2U;
                s_overflow++;
                s_length = 0U;
                s_cr_seen = false;
                s_discard = true;
                s_safe_ready = false;
            }
        }
    }
    taskEXIT_CRITICAL();
    return available;
}

static char *AppendDecimal(char *out, uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U);
    while (count != 0U) { *out++ = digits[--count]; }
    return out;
}

/* Maximum ADC line is 63 bytes, including CRLF, no terminator needed. */
static uint16_t AdcReply(char *reply)
{
    AdcApp_Result result;
    AdcApp_Status status;
    const char *error;
    char *end;
    if (!AdcApp_GetLatest(&result))
    {
        status = AdcApp_GetStatus();
        switch (status)
        {
            case ADC_APP_INIT_ERROR:  error = "ADC ERROR INIT\r\n"; break;
            case ADC_APP_START_ERROR: error = "ADC ERROR START\r\n"; break;
            case ADC_APP_DMA_ERROR:   error = "ADC ERROR DMA\r\n"; break;
            case ADC_APP_STALLED:     error = "ADC ERROR STALLED\r\n"; break;
            default:                 error = "ADC NOT READY\r\n"; break;
        }
        memcpy(reply, error, strlen(error));
        return (uint16_t)strlen(error);
    }
    memcpy(reply, "ADC: raw=", 9U);
    end = AppendDecimal(reply + 9, result.raw);
    memcpy(end, ",avg=", 5U);
    end = AppendDecimal(end + 5, result.avg);
    memcpy(end, ",mv=", 4U);
    end = AppendDecimal(end + 4, result.mv);
    memcpy(end, ",n=", 3U);
    end = AppendDecimal(end + 3, result.n);
    memcpy(end, ",missed=", 8U);
    end = AppendDecimal(end + 8, result.missed);
    *end++ = '\r'; *end++ = '\n';
    return (uint16_t)(end - reply);
}

/* One TX owner and one complete reply per pass. ADC lines are sent in short
 * chunks with ADC service between them. Existing <=30-byte replies stay in
 * one HAL call with the original timeout; ControlTask can preempt all TX. */
static bool SendReply(const char *reply, uint16_t size)
{
    uint16_t chunk;
    while (size != 0U)
    {
        chunk = (size > 30U) ? 30U : size;
        if (HAL_UART_Transmit(&s_uart, (uint8_t *)reply, chunk, 10U) != HAL_OK)
        { return false; }
        AdcApp_Process();
        reply += chunk;
        size -= chunk;
    }
    return true;
}

static char *AppendCenti(char *out, uint32_t value)
{
    out = AppendDecimal(out, value / 100U);
    *out++ = '.';
    *out++ = (char)('0' + (value / 10U) % 10U);
    *out++ = (char)('0' + value % 10U);
    return out;
}

/* Max valid ENV line: 52 bytes (T=-40.00, H=100.00, P=110000,
 * tick=4294967295), CRLF
 * included; fits the existing 64-byte ADC reply buffer without a terminator. */
static uint16_t EnvReply(char *reply)
{
    EnvSnapshot env;
    char *end;
    uint32_t magnitude;
    if (!EnvApp_GetSnapshot(&env))
    { memcpy(reply, "ENV NOT READY\r\n", 15U); return 15U; }
    if (!env.valid || ((uint32_t)xTaskGetTickCount() - env.sample_tick > 2000U))
    { memcpy(reply, "ENV ERROR\r\n", 11U); return 11U; }
    memcpy(reply, "ENV:T=", 6U);
    end = reply + 6;
    /* Unsigned negation also handles INT32_MIN without signed UB. */
    magnitude = (uint32_t)env.temperature_centi_c;
    if (env.temperature_centi_c < 0) { *end++ = '-'; magnitude = 0U - magnitude; }
    end = AppendCenti(end, magnitude);
    memcpy(end, "C,H=", 4U);
    end = AppendCenti(end + 4, env.humidity_centi_pct);
    memcpy(end, "%,P=", 4U);
    end = AppendDecimal(end + 4, env.pressure_pa);
    memcpy(end, "Pa,tick=", 8U);
    end = AppendDecimal(end + 8, env.sample_tick);
    *end++ = '\r'; *end++ = '\n';
    return (uint16_t)(end - reply);
}

/* One compact boot/runtime query; the original UartTask remains sole TX owner. */
static uint16_t HealthReply(char *reply)
{
    HealthBootSnapshot boot;
    HealthRuntimeSnapshot runtime;
    const char *reset, *policy, *progress, *watchdog;
    char *end = reply;
    Health_GetBootSnapshot(&boot);
    Health_GetRuntimeSnapshot(&runtime);
    switch (boot.reset_reason)
    {
        case HEALTH_RESET_POWER: reset = "POWER"; break;
        case HEALTH_RESET_IWDG: reset = "IWDG"; break;
        case HEALTH_RESET_SOFTWARE: reset = "SOFT"; break;
        case HEALTH_RESET_PIN: reset = "PIN"; break;
        case HEALTH_RESET_OTHER: reset = "OTHER"; break;
        default: reset = "UNKNOWN"; break;
    }
    switch (boot.persistent_state)
    {
        case HEALTH_STORE_AUTO: policy = "AUTO"; break;
        case HEALTH_STORE_IWDG_LOCKED: policy = "LOCK"; break;
        case HEALTH_STORE_MANUAL_ONLY: policy = "MANUAL"; break;
        case HEALTH_STORE_EMPTY_LOCKED: policy = "EMPTY"; break;
        default: policy = "BAD"; break;
    }
    progress = runtime.control_progress == HEALTH_CTRL_RUNNING ? "RUN" :
               runtime.control_progress == HEALTH_CTRL_STALLED ? "STALL" : "INIT";
    watchdog = runtime.watchdog == HEALTH_WDG_RUNNING ? "ON" :
               runtime.watchdog == HEALTH_WDG_START_ERROR ? "STARTERR" :
               runtime.watchdog == HEALTH_WDG_STARTING ? "START" : "OFF";
    /* Max 58 bytes including CRLF. A is this boot's automatic LOCAL policy;
     * T distinguishes compile-time T2 stop from a measured control stall. */
    memcpy(end, "HEALTH C=", 9U); end += 9;
    memcpy(end, progress, strlen(progress)); end += strlen(progress);
    memcpy(end, " W=", 3U); end += 3;
    memcpy(end, watchdog, strlen(watchdog)); end += strlen(watchdog);
    memcpy(end, " R=", 3U); end += 3;
    memcpy(end, reset, strlen(reset)); end += strlen(reset);
    memcpy(end, " L=", 3U); end += 3;
    *end++ = Health_ControlAuthorizationAllowed() ? '0' : '1';
    memcpy(end, " P=", 3U); end += 3;
    memcpy(end, policy, strlen(policy)); end += strlen(policy);
    memcpy(end, " A=", 3U); end += 3;
    *end++ = Health_BootAutoLocalAllowed() ? '1' : '0';
    memcpy(end, " T=", 3U); end += 3;
    *end++ = runtime.test_stop_active ? '1' : '0';
    *end++ = '\r'; *end++ = '\n';
    return (uint16_t)(end - reply);
}

/* Fixed four uint32 fields. Max prefix length 3 => 57 bytes including CRLF.
 * No NUL is transmitted or required. Only called by the existing UartTask. */
static uint16_t P7Format(char *reply, const char *prefix, const char *keys,
                         const uint32_t *values)
{
    uint32_t i;
    char *end = reply;
    while (*prefix != '\0') { *end++ = *prefix++; }
    for (i = 0U; i < 4U; ++i)
    {
        *end++ = ' '; *end++ = keys[i]; *end++ = '=';
        end = AppendDecimal(end, values[i]);
    }
    *end++ = '\r'; *end++ = '\n';
    return (uint16_t)(end - reply);
}
static uint16_t P7Reply(const char *command, char *reply)
{
    uint32_t values[4];
    if (strcmp(command, "PERF?") == 0 || strcmp(command, "PERF3?") == 0)
    {
        P7ControlMetrics p;
        uint32_t violations;
        Control_GetP7Metrics(&p, &violations);
        if (strcmp(command, "PERF?") == 0)
        {
            values[0] = p.period_min_cycles; values[1] = p.period_max_cycles;
            values[2] = p.elapsed_max_cycles; values[3] = violations;
            return P7Format(reply, "P7", "abxv", values);
        }
        values[0] = p.window_start_tick; values[1] = p.observed_tick;
        values[2] = p.flags; values[3] = p.invalid_samples;
        return P7Format(reply, "P7W", "tufd", values);
    }
    if (strcmp(command, "PERF2?") == 0)
    {
        P7ControlMetrics p;
        ControlDiagnostics c;
        HealthRuntimeSnapshot h;
        uint32_t violations;
        Control_GetP7Metrics(&p, &violations);
        Control_GetDiagnostics(&c); Health_GetRuntimeSnapshot(&h);
        values[0] = p.period_samples; values[1] = c.min_control_stack_words;
        values[2] = c.min_idle_stack_words; values[3] = h.min_stack_words;
        return P7Format(reply, "P7N", "ncih", values);
    }
    if (strcmp(command, "PERF4?") == 0)
    {
        ControlDiagnostics c;
        Control_GetDiagnostics(&c);
        values[0] = (uint32_t)c.mode; values[1] = (uint32_t)c.fault;
        values[2] = c.wake_count; values[3] = c.late_count;
        return P7Format(reply, "P7S", "mfwl", values);
    }
    if (strcmp(command, "CANC?") == 0)
    {
        CanDisplaySnapshot c;
        CanRxEvents e;
        (void)CanApp_GetDisplaySnapshot(&c); CanApp_GetRxEvents(&e);
        values[0] = c.rx_count; values[1] = e.queue_fail_events;
        values[2] = e.overrun_callback_events; values[3] = e.read_fail_events;
        return P7Format(reply, "CAN", "rqed", values);
    }
    return 0U;
}

static bool LocalRecoverySafe(void)
{
    HealthBootSnapshot boot;
    ControlStatusSnapshot control;
    ControlDiagnostics diagnostics;
    ActuatorSnapshot actuator;
    Health_GetBootSnapshot(&boot);
    Control_GetStatus(&control);
    Control_GetDiagnostics(&diagnostics);
    Actuator_GetSnapshot(&actuator);
    return (boot.persistent_state == HEALTH_STORE_EMPTY_LOCKED ||
            boot.persistent_state == HEALTH_STORE_IWDG_LOCKED) &&
           !Health_ControlAuthorizationAllowed() &&
           control.state == P3B_DISABLED && !control.enabled &&
           !control.key0_stop_latched && diagnostics.fault == CONTROL_FAULT_NONE &&
           HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) != GPIO_PIN_RESET &&
           actuator.initialized && !actuator.enabled && !actuator.fault_latched;
}

/* Reply is a single task-local short buffer, not a command/TX queue. */
static uint16_t ParseByte(uint8_t byte, char *reply)
{
    char *end;
    uint32_t received;
    if (s_discard)
    {
        if (byte != '\n') { return 0U; }
        s_discard = false;
        s_cr_seen = false;
        memcpy(reply, "ERR\r\n", 5U);
        return 5U;
    }
    /* CR is a line terminator prefix, never silently remove embedded CR
     * from a maintenance command. Both LF and CRLF remain supported. */
    if (byte == '\r' && !s_cr_seen) { s_cr_seen = true; return 0U; }
    if (byte != '\n' && s_cr_seen)
    {
        s_length = 0U; s_discard = true; s_safe_ready = false;
        return 0U;
    }
    if (byte == '\n') { s_cr_seen = false; }
    if (byte != '\n')
    {
        if ((byte < 0x20U) || (byte > 0x7eU) || (s_length == LINE_SIZE - 1U))
        {
            if (s_length == LINE_SIZE - 1U) { s_overflow++; }
            s_length = 0U;
            s_discard = true;
            s_safe_ready = false;
        }
        else { s_line[s_length++] = (char)byte; }
        return 0U;
    }
    if (s_length == 0U) { return 0U; }
    s_line[s_length] = '\0';
    s_length = 0U;
    PublishCommand(); /* Also records complete but unknown ASCII commands. */
    if (strcmp(s_line, "SAFE CLEAR YES") == 0)
    {
        bool confirmed = s_safe_ready &&
            (TickType_t)(xTaskGetTickCount() - s_safe_tick) < 10000U;
        s_safe_ready = false; /* Every YES consumes the window, even on denial. */
        if (confirmed && LocalRecoverySafe() && Health_StageLocalRecovery())
        {
            s_reboot_pending = true;
            memcpy(reply, "SAFE REBOOT\r\n", 13U); return 13U;
        }
        memcpy(reply, "SAFE DENIED\r\n", 13U); return 13U;
    }
    s_safe_ready = false; /* A different complete command cancels old intent. */
    {
        uint16_t p7_size = P7Reply(s_line, reply);
        if (p7_size != 0U) { return p7_size; }
    }
    if (strcmp(s_line, "SAFE CLEAR?") == 0)
    {
        if (LocalRecoverySafe())
        {
            s_safe_tick = xTaskGetTickCount(); s_safe_ready = true;
            memcpy(reply, "SAFE READY 10S\r\n", 16U); return 16U;
        }
        memcpy(reply, "SAFE DENIED\r\n", 13U); return 13U;
    }
    if (strcmp(s_line, "HEALTH?") == 0) { return HealthReply(reply); }
    if (strcmp(s_line, "PING") == 0)
    { memcpy(reply, "PONG\r\n", 6U); return 6U; }
    if (strcmp(s_line, "ADC?") == 0) { return AdcReply(reply); }
    if (strcmp(s_line, "ENV?") == 0) { return EnvReply(reply); }
    if (strcmp(s_line, "COUNT?") == 0)
    {
        taskENTER_CRITICAL();
        received = s_rx_base + ProgressLocked();
        taskEXIT_CRITICAL();
        memcpy(reply, "RX=", 3U);
        end = AppendDecimal(reply + 3, received);
        memcpy(end, ",OVF=", 5U);
        end = AppendDecimal(end + 5, s_overflow);
        *end++ = '\r'; *end++ = '\n';
        return (uint16_t)(end - reply);
    }
    memcpy(reply, "ERR\r\n", 5U);
    return 5U;
}

static void ReplyAndMaybeReboot(const char *reply, uint16_t size)
{
    bool sent = s_error_pending == 0U && SendReply(reply, size);
    if (s_reboot_pending)
    {
        /* HAL blocking TX waits for TC. Reply means request staged only;
         * persistent result requires a new HEALTH? after reboot. */
        if (sent && s_error_pending == 0U && LocalRecoverySafe())
        { NVIC_SystemReset(); }
        s_reboot_pending = false;
        if (!Health_CancelLocalRecovery()) { Fault(); }
    }
    if (!sent) { s_safe_ready = false; Fault(); }
}

static void UartTask(void *argument)
{
    char reply[64]; /* Max ADC 63 / ENV 52 bytes, no printf stack cost. */
    uint8_t byte;
    uint16_t size;
    uint32_t n;
    UBaseType_t free_words;
    bool ok;
    TickType_t report_tick;
    (void)argument;
    configASSERT(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel5_IRQn);
    taskENTER_CRITICAL();
    ok = StartRx();
    if (ok)
    {
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    }
    taskEXIT_CRITICAL();
    if (!ok) { Fault(); }
    else { (void)AdcApp_Start(); } /* RX first, then ADC DMA, then TIM2. */
    report_tick = xTaskGetTickCount();
    for (;;)
    {
        AdcApp_Process(); /* Existing nominal 5 ms polling, no AdcTask. */
        size = 0U;
        n = 0U;
        if (s_fault == 0U)
        {
            if ((s_error_pending != 0U) && !RecoverRx()) { Fault(); }
            if (s_fault == 0U)
            {
                taskENTER_CRITICAL();
                if (s_error_pending == 0U) { ClearIdleIfDrained(); }
                taskEXIT_CRITICAL();
                /* At most 64 RX bytes and ONE reply/report per pass;
                 * resume consumption immediately after the bounded TX.
                 * Low-priority TX stays preemptible by ControlTask (prio1).
                 */
                if ((TickType_t)(xTaskGetTickCount() - report_tick) >= ADC_REPORT_TICKS)
                {
                    report_tick = xTaskGetTickCount();
                    size = AdcReply(reply); /* No starvation under RX load. */
                }
                else
                {
                    for (n = 0U; n < RX_BUDGET; n++)
                    {
                        if (!TakeByte(&byte)) { break; }
                        size = ParseByte(byte, reply);
                        if (size != 0U) { break; }
                    }
                }
                if (size != 0U)
                { ReplyAndMaybeReboot(reply, size); }
            }
        }
        taskENTER_CRITICAL();
        s_display.rx_bytes = s_rx_base + ProgressLocked();
        s_display.ovf_count = s_overflow;
        taskEXIT_CRITICAL();
        free_words = uxTaskGetStackHighWaterMark(NULL);
        if (free_words < s_min_stack_words) { s_min_stack_words = free_words; }
        if ((s_fault != 0U) || ((size == 0U) && (n < RX_BUDGET)))
        { (void)ulTaskNotifyTake(pdTRUE, POLL_TICKS); }
        else { taskYIELD(); }
    }
}
