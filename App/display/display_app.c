#include "display_app.h"
#include "display_font.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "env_app.h"
#include "imu.h"
#include "../control/control.h"
#include "../control/control_config.h"
#include "actuator.h"
#include "can_app.h"
#include "uart_app.h"
#include "adc_app.h"
#include <string.h>

#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0) || \
    (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "DisplayTask requires static allocation and 32-bit 1 kHz ticks"
#endif

#define DISPLAY_STACK_WORDS 512U
#define DISPLAY_STEP_TICKS 5U
#define DISPLAY_FRAME_TICKS 500U
#define DISPLAY_I2C_TIMEOUT_MS 5U
#define DISPLAY_ADDRESS (0x3CU << 1)
#define DISPLAY_CHUNK 16U
#define KEY1_STABLE_TICKS 40U

static I2C_HandleTypeDef s_i2c;
static StaticTask_t s_tcb;
static StackType_t s_stack[DISPLAY_STACK_WORDS];
static TaskHandle_t s_task;
static uint8_t s_fb[128U * 4U];
static uint8_t s_page, s_row, s_column;
static struct {
    uint32_t changed_tick;
    bool raw_down, stable_down, armed;
} s_key;

/* No printf or floating point formatting. Each row is bounded to 21 glyphs. */
static void Row(uint8_t row) { s_row = row; s_column = 0U; }
static void Glyph(char c)
{
    if (s_column >= 21U) { return; }
    if ((unsigned char)c < 32U || (unsigned char)c > 122U) { c = '?'; }
    memcpy(&s_fb[(uint32_t)s_row * 128U + (uint32_t)s_column * 6U],
           s_font[(unsigned char)c - 32U], 6U);
    s_column++;
}
static void Text(const char *text)
{
    while (*text != '\0' && s_column < 21U) { Glyph(*text++); }
}
static void Number(uint32_t value, uint32_t base)
{
    char digits[10];
    uint32_t n = 0U;
    do {
        uint32_t d = value % base;
        digits[n++] = (char)(d < 10U ? '0' + d : 'A' + d - 10U);
        value /= base;
    } while (value != 0U);
    while (n != 0U) { Glyph(digits[--n]); }
}
static void Centi(int32_t value)
{
    uint32_t magnitude = (uint32_t)value;
    if (value < 0) { Glyph('-'); magnitude = 0U - magnitude; }
    Number(magnitude / 100U, 10U); Glyph('.');
    Glyph((char)('0' + (magnitude / 10U) % 10U));
    Glyph((char)('0' + magnitude % 10U));
}

static void BuildFrame(void)
{
    uint32_t now;
    memset(s_fb, 0, sizeof(s_fb));
    if (s_page == 0U)
    {
        EnvSnapshot env;
        bool available = EnvApp_GetSnapshot(&env);
        bool fresh;
        now = xTaskGetTickCount(); /* After copies, including any preemption. */
        fresh = available && env.valid && (uint32_t)(now - env.sample_tick) <= 2000U;
        Row(0); Text(!available ? "ENV WAIT" : !env.valid ? "ENV ERR" :
                     !fresh ? "ENV STALE" : "ENV OK");
        Row(1); Text("T:");
        if (fresh) { Centi(env.temperature_centi_c); } else { Text("--"); }
        Text("C H:");
        if (fresh) { Centi((int32_t)env.humidity_centi_pct); } else { Text("--"); }
        Glyph('%');
        Row(2); Text("P:");
        if (fresh) { Centi((int32_t)env.pressure_pa); } else { Text("--"); }
        Text("hPa"); /* 1 Pa = 0.01 hPa. */
        Row(3); Text("AGE:");
        if (available) { Number(now - env.sample_tick, 10U); Text("ms"); }
        else { Text("--"); }
    }
    else if (s_page == 1U)
    {
        ImuAttitudeSnapshot imu;
        ControlStatusSnapshot control;
        ActuatorSnapshot actuator;
        bool available = Imu_GetSnapshot(&imu);
        bool fresh;
        Control_GetStatus(&control);
        Actuator_GetSnapshot(&actuator);
        now = xTaskGetTickCount();
        fresh = available && imu.valid && imu.state == IMU_READY &&
            (uint32_t)(now - imu.sample_tick) < P2B_SAMPLE_TTL_TICKS &&
            imu.roll_deg >= -360.0f && imu.roll_deg <= 360.0f &&
            imu.pitch_deg >= -360.0f && imu.pitch_deg <= 360.0f;
        Row(0); Text("IMU/PWM ");
        if (control.key0_stop_latched) { Text("KEY0 LOCK"); }
        else if (control.state == P3B_FAULT) { Text("FAULT"); }
        else if (control.state == P3B_DISABLED) { Text("DISABLED"); }
        else if (control.state <= P3B_ACTIVE_LOCAL) { Text("LOCAL"); }
        else { Text("REMOTE"); }
        Row(1); Text("R:");
        if (fresh) { Centi((int32_t)(imu.roll_deg * 100.0f)); } else { Text("--"); }
        Text(" P:");
        if (fresh) { Centi((int32_t)(imu.pitch_deg * 100.0f)); } else { Text("--"); }
        Row(2); Text("PAN:");
        if (actuator.initialized) { Number(actuator.applied_pan_us, 10U); } else { Text("--"); }
        Text(" TILT:");
        if (actuator.initialized) { Number(actuator.applied_tilt_us, 10U); } else { Text("--"); }
        Row(3); Text(actuator.enabled ? "PWM:ON STAGE:" : "PWM:OFF STAGE:");
        Number(P3B_OUTPUT_STAGE, 10U);
    }
    else if (s_page == 2U)
    {
        CanDisplaySnapshot can;
        bool available = CanApp_GetDisplaySnapshot(&can);
        now = xTaskGetTickCount();
        Row(0); Text("CAN RX ID:");
        if (available) { Number(can.last_id, 16U); } else { Text("--"); }
        Row(1); Text("REQ:");
        if (available && can.request_valid)
        {
            Number(can.requested_pan_us, 10U); Glyph('/');
            Number(can.requested_tilt_us, 10U);
            if ((uint32_t)(now - can.request_tick) >= CONTROL_MAX_TTL_TICKS) { Text(" OLD"); }
        }
        else { Text("--/--"); }
        Row(2); Text("RX:"); Number(can.rx_count, 10U);
        Row(3); Text("AGE:");
        if (available) { Number(now - can.last_rx_tick, 10U); Text("ms"); }
        else { Text("--"); }
    }
    else
    {
        UartDisplaySnapshot uart;
        AdcApp_Result adc;
        bool available = UartApp_GetDisplaySnapshot(&uart);
        bool adc_ok = AdcApp_GetLatest(&adc);
        now = xTaskGetTickCount();
        Row(0); Text("UART RX:"); Number(uart.rx_bytes, 10U);
        Row(1); Text(available && (uint32_t)(now - uart.last_command_tick) > 2000U ? "OLD:" : "CMD:");
        Text(available ? uart.last_command : "--");
        Row(2); Text("OVF:"); Number(uart.ovf_count, 10U);
        Row(3); Text("ADC:");
        if (adc_ok) { Number(adc.mv, 10U); Text("mV"); } else { Text("--"); }
    }
}

/* Polling TX does not use the old HAL's receive-tail IRQ masking. */
static bool Send(uint8_t *bytes, uint16_t size)
{
    uint32_t start = HAL_GetTick();
    if (s_i2c.State != HAL_I2C_STATE_READY) { return false; }
    while (__HAL_I2C_GET_FLAG(&s_i2c, I2C_FLAG_BUSY) != RESET)
    {
        if ((uint32_t)(HAL_GetTick() - start) >= DISPLAY_I2C_TIMEOUT_MS) { return false; }
    }
    return HAL_I2C_Master_Transmit(&s_i2c, DISPLAY_ADDRESS, bytes, size,
                                  DISPLAY_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool InitOled(void)
{
    /* 128x32 SSD1306 parameters reviewed against the supplied example.
     * Four-pin module: no PD4 reset. Page mode explicitly selected. */
    static const uint8_t init[] = {
        0xAE, 0x20, 0x02, 0x40, 0x81, 0xFF, 0xA1, 0xA6,
        0xA8, 0x1F, 0xC8, 0xD3, 0x00, 0xD5, 0x80, 0xD9,
        0x1F, 0xDA, 0x00, 0xDB, 0x40, 0x8D, 0x14, 0xA4, 0x2E
    };
    uint8_t bytes[DISPLAY_CHUNK + 1U];
    uint32_t offset;
    uint16_t count;
    if ((AFIO->MAPR & AFIO_MAPR_I2C1_REMAP) != 0U) { return false; }
    s_i2c.Instance = I2C1;
    s_i2c.Init.ClockSpeed = 100000U;
    s_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&s_i2c) != HAL_OK) { return false; }
    for (offset = 0U; offset < sizeof(init); offset += count)
    {
        /* First packet ends after D5/80; never split a command's parameter. */
        count = offset == 0U ? 15U : (uint16_t)(sizeof(init) - offset);
        bytes[0] = 0x00; memcpy(bytes + 1, init + offset, count);
        if (!Send(bytes, count + 1U)) { return false; }
    }
    return true;
}

static bool SendBlock(uint16_t offset)
{
    uint8_t commands[4] = {0x00, 0, 0, 0};
    uint8_t data[DISPLAY_CHUNK + 1U];
    uint8_t column = (uint8_t)(offset % 128U);
    commands[1] = (uint8_t)(0xB0U + offset / 128U);
    commands[2] = column & 0x0FU;
    commands[3] = (uint8_t)(0x10U | (column >> 4));
    data[0] = 0x40; memcpy(data + 1, s_fb + offset, DISPLAY_CHUNK);
    return Send(commands, sizeof(commands)) && Send(data, sizeof(data));
}

static void InitKey1(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_3; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &gpio);
    /* Require a stable release even if initially held. */
    s_key.raw_down = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET;
    s_key.stable_down = true;
    s_key.changed_tick = xTaskGetTickCount();
}
static bool PollKey1(uint32_t now)
{
    bool down = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET;
    if (down != s_key.raw_down) { s_key.raw_down = down; s_key.changed_tick = now; }
    if ((uint32_t)(now - s_key.changed_tick) < KEY1_STABLE_TICKS) { return false; }
    if (!down) { s_key.stable_down = false; s_key.armed = true; }
    else if (!s_key.stable_down)
    {
        s_key.stable_down = true;
        if (s_key.armed)
        {
            s_key.armed = false;
            s_page = (uint8_t)((s_page + 1U) % 4U);
            return true;
        }
    }
    return false;
}

static void DisplayTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t frame_tick;
    uint16_t offset = sizeof(s_fb);
    bool healthy, display_on = false;
    (void)argument;
    InitKey1();
    (void)xTaskDelayUntil(&wake, 100U); /* Module power-up wait, IRQs enabled. */
    healthy = InitOled();
    frame_tick = xTaskGetTickCount() - DISPLAY_FRAME_TICKS;
    wake = xTaskGetTickCount();
    for (;;)
    {
        uint32_t now = xTaskGetTickCount();
        bool changed = PollKey1(now);
        if (healthy)
        {
            if (changed || (offset == sizeof(s_fb) &&
                (uint32_t)(now - frame_tick) >= DISPLAY_FRAME_TICKS))
            {
                BuildFrame(); offset = 0U; frame_tick = xTaskGetTickCount();
            }
            if (offset < sizeof(s_fb))
            {
                healthy = SendBlock(offset); /* At most one 16-byte block per step. */
                offset += DISPLAY_CHUNK;
                if (healthy && offset == sizeof(s_fb) && !display_on)
                {
                    uint8_t on[2] = {0x00, 0xAF};
                    healthy = Send(on, sizeof(on)); display_on = healthy;
                }
            }
        }
        /* Fault stays display-local; key polling continues, no retry/reset loop. */
        if (xTaskDelayUntil(&wake, DISPLAY_STEP_TICKS) == pdFALSE)
        { wake = xTaskGetTickCount(); (void)xTaskDelayUntil(&wake, DISPLAY_STEP_TICKS); }
    }
}

bool DisplayApp_Init(void)
{
    if (s_task != NULL || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) { return false; }
    s_task = xTaskCreateStatic(DisplayTask, "DisplayTask", DISPLAY_STACK_WORDS, NULL,
                               tskIDLE_PRIORITY, s_stack, &s_tcb);
    return s_task != NULL;
}
