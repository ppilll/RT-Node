#ifndef RT_NODE_CONTROL_CONFIG_H
#define RT_NODE_CONTROL_CONFIG_H

#include "../p1a_test/p1a_test.h"

#ifndef P1B_AUTOTEST_ENABLE
#define P1B_AUTOTEST_ENABLE 0
#endif
#ifndef P1B_AUTOTEST_CASE
#define P1B_AUTOTEST_CASE 1
#endif
#ifndef P1B_UNLOADED_ACK
#define P1B_UNLOADED_ACK 0
#endif
#ifndef P1B_TEST_ARM_KEY
#define P1B_TEST_ARM_KEY 0UL
#endif
#ifndef P1B_TRACE_ENABLE
#define P1B_TRACE_ENABLE 0
#endif

#if (P1B_AUTOTEST_ENABLE != 0) && (P1B_AUTOTEST_ENABLE != 1)
#error "P1B_AUTOTEST_ENABLE must be 0 or 1"
#endif
#if P1A_AUTOTEST_ENABLE || P1A_ACTUATOR_TEST_ENABLE
#error "P1b ControlTask requires both P1a test modes OFF"
#endif
#if (P1B_AUTOTEST_CASE < 1) || (P1B_AUTOTEST_CASE > 4)
#error "P1B_AUTOTEST_CASE must be 1..4"
#endif
#if P1B_AUTOTEST_ENABLE
#if (P1B_UNLOADED_ACK != 1) || (P1B_TEST_ARM_KEY != 0xC2201500UL)
#error "P1b output test requires unloaded acknowledgement and explicit arm key"
#endif
#elif (P1B_UNLOADED_ACK != 0) || (P1B_TEST_ARM_KEY != 0) || (P1B_AUTOTEST_CASE != 1)
#error "Production image must not retain P1b test authorization"
#endif
#if P1B_TRACE_ENABLE != 0
#error "No trace GPIO or diagnostic channel is approved for this C2 delivery"
#endif

#define CONTROL_PERIOD_TICKS       20U
#define CONTROL_MAX_TTL_TICKS      100U
#define CONTROL_TEST_TTL_TICKS     40U
#define CONTROL_PRESTART_TICKS     2000U
#define CONTROL_PRIMING_TICKS      40U
#define CONTROL_MAX_LATE_TICKS     1U
#define CONTROL_EXECUTION_US       2000U
#define CONTROL_STACK_WORDS        256U

#include "p2b_config.h"

#endif
