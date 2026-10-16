#ifndef RT_NODE_P1A_TEST_H
#define RT_NODE_P1A_TEST_H

/* Deliberately OFF. Override only for the supervised H01-H10 test image. */
#ifndef P1A_ACTUATOR_TEST_ENABLE
#define P1A_ACTUATOR_TEST_ENABLE 0
#endif
#if (P1A_ACTUATOR_TEST_ENABLE != 0) && (P1A_ACTUATOR_TEST_ENABLE != 1)
#error "P1A_ACTUATOR_TEST_ENABLE must be 0 or 1"
#endif

#if P1A_ACTUATOR_TEST_ENABLE
#include "actuator.h"

#define P1A_TEST_ENABLE_KEY 0xC2061500UL
/* Debugger mailbox: write arguments/key/action first, request_seq LAST.
 * One command at a time: wait ack_seq==request_seq before changing anything.
 * No protocol, automatic sequence, delay, sweep or background re-enable.
 */
typedef struct
{
    uint32_t request_seq;
    uint32_t action;       /* 1 Disable, 2 Enable, 3 Apply, 4 fault, 5 Init,
                           * 6 Apply requested near overflow (no busy wait) */
    uint32_t enable_key;   /* fresh key required for EACH action 2; consumed */
    int32_t pan_us;
    int32_t tilt_us;
    uint32_t ack_seq;
    uint32_t status;       /* 0 idle, 1 success, 2 refused, 3 bad key/action,
                           * 4 waiting near overflow (action 6, no ack yet) */
    uint32_t dispatch_counter; /* sampled before Apply, NOT exact MMIO phase */
    ActuatorApplyResult result;
    ActuatorSnapshot snapshot;
} P1aTestMailbox;

extern volatile P1aTestMailbox g_p1a_test;
void P1aTest_Poll(void);
#endif
#endif
