#ifndef RT_NODE_HEALTH_CONFIG_H
#define RT_NODE_HEALTH_CONFIG_H

#include "../control/control_config.h"

#define HEALTH_PERIOD_TICKS       100U
#define HEALTH_STALL_TICKS        300U
#define HEALTH_STACK_WORDS        256U

/* Firmware-only T2: never exposed through UART/CAN. These defaults must stay
 * OFF. A separate Stage0 image requires explicit unloaded build consent. */
#ifndef P6_WDG_TEST_ENABLE
#define P6_WDG_TEST_ENABLE 0
#endif
#ifndef P6_WDG_TEST_UNLOADED_ACK
#define P6_WDG_TEST_UNLOADED_ACK 0
#endif

#if (P6_WDG_TEST_ENABLE != 0) && (P6_WDG_TEST_ENABLE != 1)
#error "P6_WDG_TEST_ENABLE must be 0 or 1"
#endif
#if P6_WDG_TEST_ENABLE
#if (P3B_OUTPUT_STAGE != 0) || (P6_WDG_TEST_UNLOADED_ACK != 1)
#error "P6 T2 requires Stage0 and explicit unloaded acknowledgement"
#endif
#if P1A_AUTOTEST_ENABLE || P1A_ACTUATOR_TEST_ENABLE || P1B_AUTOTEST_ENABLE
#error "P6 T2 forbids historical output tests"
#endif
#else
#if P6_WDG_TEST_UNLOADED_ACK != 0
#error "Production image must not retain P6 T2 acknowledgement"
#endif
#endif

#endif
