#include "imu.h"
#include "imu_fusion.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "../../control/control_config.h"
#include <stddef.h>

/* Every P2a build rejects the pre-existing output test authorization. */
#if P1A_AUTOTEST_ENABLE || P1A_ACTUATOR_TEST_ENABLE || P1B_AUTOTEST_ENABLE
#error "P2a requires all actuator output test modes OFF"
#endif
#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0) || \
    (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "ImuTask requires static-only allocation and 32-bit 1kHz ticks"
#endif

#define IMU_STACK_WORDS 512U
#define IMU_PERIOD_TICKS 10U
#define IMU_CALIBRATION_TIMEOUT_TICKS 3000U
typedef char ImuTickWidthMustBe32[(sizeof(TickType_t) == sizeof(uint32_t)) ? 1 : -1];

static StaticTask_t s_tcb;
static StackType_t s_stack[IMU_STACK_WORDS];
static TaskHandle_t s_handle;
static ImuAttitudeSnapshot s_snapshot;
static bool s_published;

static void Publish(ImuAttitudeSnapshot *next)
{
    /* Single writer; only the complete copy masks scheduling/RTOS interrupts. */
    next->generation++;
    taskENTER_CRITICAL();
    s_snapshot = *next;
    s_published = true;
    taskEXIT_CRITICAL();
}

bool Imu_GetSnapshot(ImuAttitudeSnapshot *out)
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

static void ImuTask(void *argument)
{
    ImuAttitudeSnapshot next = {0};
    ImuFusion fusion;
    Icm20602Raw raw;
    ImuSample sample;
    TickType_t last_wake, calibration_start;
    uint32_t previous_tick = 0U;
    bool has_previous = false;
    (void)argument;
    ImuFusion_Reset(&fusion);
    next.state = IMU_INITIALIZING;
    Publish(&next);
    if (Icm20602_Init() != ICM20602_OK) { next.state = IMU_FAILED; }
    else { next.state = IMU_CALIBRATING; }
    Publish(&next);
    last_wake = xTaskGetTickCount();
    calibration_start = last_wake;
    for (;;)
    {
        if (next.state != IMU_FAILED)
        {
            if (Icm20602_ReadRaw(&raw) != ICM20602_OK)
            {
                next.valid = false;
                next.state = IMU_FAILED;
                /* Keep last successful sample_tick, never refresh on IO error. */
            }
            else
            {
                uint32_t elapsed;
                next.sample_tick = (uint32_t)xTaskGetTickCount();
                elapsed = next.sample_tick - previous_tick;
                Icm20602_ToUnits(&raw, &sample);
                if (next.state == IMU_CALIBRATING)
                {
                    if ((uint32_t)(next.sample_tick-calibration_start) >=
                        IMU_CALIBRATION_TIMEOUT_TICKS)
                    { next.state = IMU_FAILED; }
                    else
                    {
                        if (has_previous && (elapsed == 0U || elapsed > 50U))
                        { ImuFusion_Reset(&fusion); }
                        if (ImuFusion_Calibrate(&fusion, &sample))
                        { next.state = IMU_READY; next.valid = true; }
                    }
                }
                else if (!ImuFusion_Update(&fusion, &sample, (float)elapsed/1000.0f))
                {
                    next.valid = false;
                    next.state = IMU_FAILED;
                }
                else { next.valid = true; }
                next.roll_deg = fusion.roll_deg;
                next.pitch_deg = fusion.pitch_deg;
                previous_tick = next.sample_tick;
                has_previous = true;
            }
            Publish(&next);
        }
        /* FAILED stays quiescent: periodic sleep only, no more SPI or recovery.
         * Missed release resynchronizes rather than issuing catch-up bursts. */
        if (xTaskDelayUntil(&last_wake, IMU_PERIOD_TICKS) == pdFALSE)
        {
            last_wake = xTaskGetTickCount();
            (void)xTaskDelayUntil(&last_wake, IMU_PERIOD_TICKS);
        }
    }
}

bool Imu_Init(void)
{
    if (s_handle != NULL) { return false; }
    s_handle = xTaskCreateStatic(ImuTask, "ImuTask", IMU_STACK_WORDS,
                                NULL, tskIDLE_PRIORITY, s_stack, &s_tcb);
    return s_handle != NULL;
}
