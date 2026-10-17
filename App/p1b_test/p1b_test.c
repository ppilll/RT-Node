#include "p1b_test.h"

#if P1B_AUTOTEST_ENABLE
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"

void P1bTest_MakeCommand(uint32_t step, uint32_t now, ControlCommandSnapshot *out)
{
    ControlCommandSnapshot command;
    command.source = CONTROL_SOURCE_TEST;
    command.generation = step + 1U;
    command.issued_tick = now;
    command.ttl_ticks = CONTROL_TEST_TTL_TICKS;
    command.valid = true;
    command.pan_us = 1500;
    command.tilt_us = 1500;
    switch (step / 200U)
    {
    case 1U: command.pan_us = 1450; break;
    case 2U: command.pan_us = 1550; break;
    case 4U: command.tilt_us = 1450; break;
    case 5U: command.tilt_us = 1550; break;
    default: break;
    }
#if P1B_AUTOTEST_CASE == 2
    if (step == P1B_TEST_INJECT_STEP)
    {
        command.issued_tick = now - command.ttl_ticks; /* expiry boundary */
    }
#endif
    *out = command; /* Complete local snapshot, no cross-task publication. */
}

void P1bTest_InjectLateWake(uint32_t step)
{
#if P1B_AUTOTEST_CASE == 3
    if (step == P1B_TEST_INJECT_STEP)
    {
        TickType_t start = xTaskGetTickCount();
        /* Deliberate task-only delay; next wake check must fault immediately.
         * This is an unloaded fault injection, never a normal control wait.
         */
        (void)xTaskDelayUntil(&start, 40U);
    }
#else
    (void)step;
#endif
}

void P1bTest_InjectExecution(uint32_t step)
{
#if P1B_AUTOTEST_CASE == 4
    if (step == P1B_TEST_INJECT_STEP)
    {
        uint32_t start = DWT->CYCCNT;
        uint32_t poll;
        for (poll = 0U; poll < 1000000U; poll++)
        {
            __NOP();
            if ((uint32_t)(DWT->CYCCNT - start) >= 216000U) { break; }
        }
        /* Finite even if CYCCNT stalls; normal duration check faults closed. */
    }
#else
    (void)step;
#endif
}
#endif
