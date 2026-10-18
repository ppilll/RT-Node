#include "env_app.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bme280.h"

#if !defined(BME280_64BIT_ENABLE) || defined(BME280_32BIT_ENABLE) || defined(BME280_DOUBLE_ENABLE)
#error "P5a requires Bosch v3.5.1 64-bit integer compensation in every translation unit"
#endif
#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0) || \
    (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "SensorTask requires static allocation and 32-bit 1 kHz ticks"
#endif

#define ENV_STACK_WORDS 512U
#define ENV_PERIOD_TICKS 500U
#define ENV_I2C_TIMEOUT_MS 5U
#define ENV_HAL_ADDRESS (0x76U << 1)

static I2C_HandleTypeDef s_i2c;
static StaticTask_t s_tcb;
static StackType_t s_stack[ENV_STACK_WORDS];
static TaskHandle_t s_task;
static struct bme280_dev s_bme;
static EnvSnapshot s_snapshot;
static bool s_published;

static void Publish(const EnvSnapshot *next)
{
    taskENTER_CRITICAL();
    s_snapshot = *next;
    s_published = true;
    taskEXIT_CRITICAL();
}

bool EnvApp_GetSnapshot(EnvSnapshot *out)
{
    bool available;
    if (out == NULL) { return false; }
    configASSERT(__get_IPSR() == 0U);
    taskENTER_CRITICAL();
    available = s_published;
    if (available) { *out = s_snapshot; }
    taskEXIT_CRITICAL();
    return available;
}

static bool InitI2c2(void)
{
    /* Only SensorTask owns I2C2; common MSP dispatches by peripheral instance. */
    s_i2c.Instance = I2C2;
    s_i2c.Init.ClockSpeed = 100000U;
    s_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_i2c.Init.OwnAddress1 = 0U;
    s_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.OwnAddress2 = 0U;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&s_i2c) == HAL_OK;
}

/* Bound the repository HAL's hard-coded 25 ms BUSY wait. No bus recovery,
 * clock pulses, or peripheral resets: a stuck bus remains an ENV-local error. */
static bool BusReady(void)
{
    uint32_t start = HAL_GetTick();
    if (s_i2c.State != HAL_I2C_STATE_READY) { return false; }
    /* STOP can still be on the wire when the previous HAL call returns.
     * Allow it to finish; all IRQs and higher-priority tasks remain enabled. */
    while (__HAL_I2C_GET_FLAG(&s_i2c, I2C_FLAG_BUSY) != RESET)
    {
        if ((uint32_t)(HAL_GetTick() - start) >= ENV_I2C_TIMEOUT_MS) { return false; }
    }
    return true;
}

static BME280_INTF_RET_TYPE Read(uint8_t reg, uint8_t *data,
                                uint32_t length, void *context)
{
    uint32_t offset;
    uint16_t chunk;
    (void)context;
    if ((data == NULL) || (length == 0U) || (length > 256U - reg)) { return -1; }
    for (offset = 0U; offset < length; offset += chunk)
    {
        chunk = (length - offset > 2U) ? 2U : (uint16_t)(length - offset);
        /* Old F1 HAL's >=3-byte tail waits for BTF with PRIMASK set, which
         * can block both ControlTask and its own timeout tick on a bus fault.
         * 1/2-byte paths mask only register operations, never a polling wait.
         * Calibration is static; sample registers are stable in forced sleep. */
        if (!BusReady() ||
            HAL_I2C_Mem_Read(&s_i2c, ENV_HAL_ADDRESS, (uint16_t)(reg + offset),
                             I2C_MEMADD_SIZE_8BIT, data + offset, chunk,
                             ENV_I2C_TIMEOUT_MS) != HAL_OK) { return -1; }
    }
    if ((reg == BME280_REG_DATA) && (length == BME280_LEN_P_T_H_DATA))
    {
        /* Reject skipped/uninitialized ADC sentinels before Bosch clamps. */
        if (((data[0] == 0x80U) && (data[1] == 0U) && ((data[2] & 0xF0U) == 0U)) ||
            ((data[3] == 0x80U) && (data[4] == 0U) && ((data[5] & 0xF0U) == 0U)) ||
            ((data[6] == 0x80U) && (data[7] == 0U))) { return -1; }
    }
    return BME280_INTF_RET_SUCCESS;
}

static BME280_INTF_RET_TYPE Write(uint8_t reg, const uint8_t *data,
                                 uint32_t length, void *context)
{
    (void)context;
    /* Bosch set_regs supplies data/register pairs for multi-register writes. */
    if ((data == NULL) || (length == 0U) || (length > 19U) || !BusReady()) { return -1; }
    return (HAL_I2C_Mem_Write(&s_i2c, ENV_HAL_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
                              (uint8_t *)data, (uint16_t)length,
                              ENV_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static void DelayUs(uint32_t period, void *context)
{
    TickType_t start = xTaskGetTickCount();
    (void)context;
    /* Ceil to ms plus one tick for the unknown phase of the current tick.
     * Scheduler is running here; never busy-wait or enable vTaskDelay. */
    if (period != 0U)
    { (void)xTaskDelayUntil(&start, period / 1000U + ((period % 1000U) != 0U) + 1U); }
}

static bool InitSensor(uint32_t *measurement_us)
{
    struct bme280_settings settings = {0};
    struct bme280_settings actual = {0};
    s_bme.intf = BME280_I2C_INTF;
    s_bme.read = Read;
    s_bme.write = Write;
    s_bme.delay_us = DelayUs;
    s_bme.intf_ptr = &s_i2c;
    DelayUs(2000U, NULL); /* Also safe if MCU and sensor power up together. */
    /* init reads ID (must be 0x60), resets, waits for NVM, reads all trim. */
    if ((bme280_init(&s_bme) != BME280_OK) ||
        (s_bme.chip_id != BME280_CHIP_ID) || (s_bme.calib_data.dig_p1 == 0U))
    { return false; }
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;
    /* Sensor remains asleep while applying settings, humidity before ctrl_meas. */
    if ((bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &s_bme) != BME280_OK) ||
        (bme280_get_sensor_settings(&actual, &s_bme) != BME280_OK) ||
        (actual.osr_t != settings.osr_t) || (actual.osr_p != settings.osr_p) ||
        (actual.osr_h != settings.osr_h) || (actual.filter != settings.filter))
    { return false; }
    return bme280_cal_meas_delay(measurement_us, &settings) == BME280_OK;
}

static bool Sample(EnvSnapshot *next, uint32_t measurement_us)
{
    struct bme280_data data;
    uint8_t status, mode;
    if (bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &s_bme) != BME280_OK)
    { return false; }
    DelayUs(measurement_us, NULL); /* x1 T/P/H: Bosch returns 9300 microseconds. */
    if ((bme280_get_regs(BME280_REG_STATUS, &status, 1U, &s_bme) != BME280_OK) ||
        ((status & (BME280_STATUS_MEAS_DONE | BME280_STATUS_IM_UPDATE)) != 0U) ||
        (bme280_get_sensor_mode(&mode, &s_bme) != BME280_OK) ||
        (mode != BME280_POWERMODE_SLEEP) ||
        (bme280_get_sensor_data(BME280_ALL, &data, &s_bme) != BME280_OK))
    { return false; }
    /* v3.5.1 64-bit integer mode: T = 0.01 C, H = 1/1024 %RH,
     * P = 0.01 Pa (NOT the older Q24.8 pressure format). Bound before scaling.
     * Bosch itself clamps to these sensor ranges; this is not calibration QA. */
    if ((data.temperature < -4000) || (data.temperature > 8500) ||
        (data.humidity > 102400U) ||
        (data.pressure < 3000000U) || (data.pressure > 11000000U)) { return false; }
    next->temperature_centi_c = data.temperature;
    next->humidity_centi_pct = (data.humidity * 100U + 512U) / 1024U;
    next->pressure_pa = (data.pressure + 50U) / 100U;
    next->sample_tick = (uint32_t)xTaskGetTickCount();
    return true;
}

static void SensorTask(void *argument)
{
    EnvSnapshot next = {0};
    TickType_t last_wake;
    uint32_t measurement_us = 0U;
    bool ready;
    (void)argument;
    ready = InitI2c2() && InitSensor(&measurement_us);
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        /* Failure preserves all last-good fields/tick and clears valid.
         * Init failure stays quiescent until MCU reset; no P6 recovery policy. */
        next.valid = ready && Sample(&next, measurement_us);
        Publish(&next);
        if (xTaskDelayUntil(&last_wake, ENV_PERIOD_TICKS) == pdFALSE)
        {
            last_wake = xTaskGetTickCount();
            (void)xTaskDelayUntil(&last_wake, ENV_PERIOD_TICKS);
        }
    }
}

bool EnvApp_Init(void)
{
    if ((s_task != NULL) || (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
    { return false; }
    s_task = xTaskCreateStatic(SensorTask, "SensorTask", ENV_STACK_WORDS, NULL,
                              tskIDLE_PRIORITY, s_stack, &s_tcb);
    return s_task != NULL;
}
