#include "p1a_test.h"

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
