/* Runs actual imu.c, fusion, driver and decoder. Only HAL/RTOS are mocked.
 * Virtual tick/CS tests are software evidence, never MCU timing evidence. */
#include "imu.h"
#include "icm20602.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static const char *scenario;
static uint32_t tick, calls, bursts, delays, cs_low, cs_high;
static uint32_t failed_calls, failed_delays, ready_seen;
static uint32_t late_wake_calls;
static int critical, selected, enabled, gpio_count, partial, injected;
static uint8_t regs[128];
static void (*task_entry)(void *);
static jmp_buf done;
static ImuAttitudeSnapshot last;
static int have_last;

static int Is(const char *s) { return strcmp(scenario, s) == 0; }
void HostEnter(void) { critical++; }
void HostExit(void) { assert(critical > 0); critical--; }
void HostSpiEnable(int value) { enabled = value; }

TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name,
    uint32_t words, void *arg, uint32_t priority, StackType_t *stack, StaticTask_t *tcb)
{
    assert(strcmp(name, "ImuTask") == 0 && words == 512 && priority == 0);
    assert(arg == NULL && stack != NULL && tcb != NULL && task_entry == NULL);
    if (Is("create_fail")) { return NULL; }
    task_entry = fn;
    return tcb;
}
TickType_t xTaskGetTickCount(void) { return tick; }
BaseType_t xTaskDelayUntil(TickType_t *wake, TickType_t period)
{
    ImuAttitudeSnapshot s;
    assert(critical == 0 && !selected && period > 0);
    delays++;
    if (Is("late_wake") && injected == 1)
    {
        /* An overdue release must sleep a fresh period before another SPI. */
        assert(calls == late_wake_calls);
        injected = 2;
    }
    if (task_entry != NULL)
    {
        assert(Imu_GetSnapshot(&s));
        if (have_last)
        {
            assert((uint32_t)(s.generation-last.generation) < 0x80000000U);
            if (s.valid) { assert(s.state == IMU_READY); }
        }
        if (s.state == IMU_READY) { ready_seen++; }
        if (s.state == IMU_FAILED)
        {
            assert(!s.valid && !selected);
            if (failed_delays == 0)
            {
                failed_calls = calls;
                if (Is("burst_timeout"))
                { assert(have_last && s.sample_tick == last.sample_tick && !enabled); }
            }
            assert(calls == failed_calls); /* no polling/recovery after terminal */
            failed_delays++;
        }
        last = s; have_last = 1;
        if (failed_delays == 3 || (ready_seen == 20 && (Is("nominal") || Is("rollover") || Is("late_wake"))))
        { longjmp(done, 1); }
    }
    assert(delays < 450); /* Finite initialization + <=3s calibration */
    if (ready_seen > 0 && !injected && (Is("zero_dt") || Is("long_dt")))
    {
        injected = 1;
        if (Is("long_dt")) { tick += 60; }
        *wake = tick;
        return pdTRUE;
    }
    if (ready_seen > 0 && !injected && Is("late_wake"))
    {
        injected = 1;
        late_wake_calls = calls;
        tick += 20;
        return pdFALSE;
    }
    *wake += period;
    tick += period;
    return pdTRUE;
}

void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState value)
{
    assert(port == GPIOC && pin == GPIO_PIN_4);
    if (value == GPIO_PIN_RESET)
    { assert(!selected && enabled); selected = 1; cs_low++; }
    else { selected = 0; cs_high++; }
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *g)
{
    assert(critical > 0);
    if (gpio_count == 0)
    { assert(port == GPIOC && g->Pin == GPIO_PIN_4 && g->Mode == GPIO_MODE_OUTPUT_PP && cs_high > 0); }
    else if (gpio_count == 1)
    { assert(port == GPIOA && g->Pin == (GPIO_PIN_5|GPIO_PIN_7) && g->Mode == GPIO_MODE_AF_PP); }
    else
    { assert(gpio_count == 2 && port == GPIOA && g->Pin == GPIO_PIN_6 && g->Mode == GPIO_MODE_INPUT); }
    gpio_count++;
}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *spi)
{
    assert(spi->Instance == SPI1 && spi->Init.Mode == SPI_MODE_MASTER);
    assert(spi->Init.Direction == SPI_DIRECTION_2LINES && spi->Init.DataSize == 8);
    assert(spi->Init.CLKPolarity == SPI_POLARITY_HIGH && spi->Init.CLKPhase == SPI_PHASE_2EDGE);
    assert(spi->Init.BaudRatePrescaler == 128 && spi->Init.NSS == SPI_NSS_SOFT);
    assert(spi->Init.FirstBit == SPI_FIRSTBIT_MSB && spi->Init.CRCCalculation == 0);
    HAL_SPI_MspInit(spi);
    regs[0x75] = Is("id_mismatch") ? 0 : 0x12;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
    uint8_t *tx, uint8_t *rx, uint16_t size, uint32_t timeout)
{
    uint8_t reg;
    (void)spi;
    assert(selected && enabled && critical == 0 && timeout == 3);
    assert(size == 2 || size == 15);
    calls++;
    memset(rx, 0, size);
    if (Is("init_io")) { return HAL_TIMEOUT; }
    reg = tx[0] & 0x7F;
    if (size == 15)
    {
        unsigned i;
        assert(tx[0] == 0xBB);
        for (i = 1; i < 15; i++) { assert(tx[i] == 0); }
        bursts++;
        /* A failed HAL call may have filled only part of the receive buffer. */
        if (partial || (Is("burst_timeout") && ready_seen > 2))
        { rx[1] = 0x55; return HAL_TIMEOUT; }
        if (!Is("invalid_accel")) { rx[5] = 0x40; } /* Az=1g */
        if (Is("motion_timeout")) { rx[9] = 3; rx[10] = 18; } /* Gx=786 ->6dps */
        return HAL_OK;
    }
    if ((tx[0] & 0x80) != 0)
    {
        rx[1] = regs[reg];
        if (reg == 0x1B && Is("config_mismatch")) { rx[1] = 0x18; }
    }
    else if (reg == 0x6B && tx[1] == 0x80)
    { regs[reg] = Is("reset_stuck") ? 0x80 : 0x41; }
    else { regs[reg] = tx[1]; }
    return HAL_OK;
}

int main(int argc, char **argv)
{
    ImuAttitudeSnapshot s;
    Icm20602Raw out, before;
    assert(argc == 2);
    scenario = argv[1];
    tick = Is("rollover") ? UINT32_MAX-800U : 0;
    assert(!Imu_GetSnapshot(NULL) && !Imu_GetSnapshot(&s));
    if (Is("partial_frame"))
    {
        assert(Icm20602_Init() == ICM20602_OK);
        assert(Icm20602_ReadRaw(&out) == ICM20602_OK && out.az == 16384);
        before = out; partial = 1;
        assert(Icm20602_ReadRaw(&out) == ICM20602_IO_ERROR);
        assert(memcmp(&before, &out, sizeof out) == 0 && !selected && !enabled);
        failed_calls = calls;
        assert(Icm20602_ReadRaw(&out) == ICM20602_IO_ERROR && calls == failed_calls);
    }
    else if (Is("create_fail")) { assert(!Imu_Init() && task_entry == NULL); }
    else
    {
        assert(Imu_Init() && !Imu_Init());
        if (setjmp(done) == 0) { task_entry(NULL); assert(0); }
        assert(Imu_GetSnapshot(&s));
        if (Is("nominal") || Is("rollover") || Is("late_wake"))
        {
            assert(s.state == IMU_READY && s.valid && ready_seen == 20);
            assert(s.roll_deg == 0 && s.pitch_deg == 0 && bursts >= 100);
            assert(regs[0x70] == 0x40 && regs[0x1B] == 0 && regs[0x1C] == 0);
            assert(regs[0x19] == 9 && regs[0x1A] == 3 && regs[0x1D] == 3);
            if (Is("rollover")) { assert(s.sample_tick < 2000); }
        }
        else { assert(s.state == IMU_FAILED && !s.valid && failed_delays == 3); }
        if (Is("id_mismatch")) { assert(calls == 2 && bursts == 0); }
        if (Is("init_io")) { assert(calls == 1 && !enabled); }
        if (Is("reset_stuck")) { assert(calls == 12 && bursts == 0); }
        if (Is("motion_timeout") || Is("invalid_accel")) { assert(bursts == 301); }
    }
    assert(critical == 0 && !selected && cs_high >= cs_low);
    printf("U04 PASS %-17s calls=%lu bursts=%lu delays=%lu CS=HIGH state=%d valid=%d\n",
           scenario, (unsigned long)calls, (unsigned long)bursts, (unsigned long)delays,
           have_last ? (int)last.state : -1, have_last ? (int)last.valid : 0);
    return 0;
}
