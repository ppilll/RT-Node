#ifndef RT_NODE_P1B_TEST_H
#define RT_NODE_P1B_TEST_H

#include "../control/control_config.h"
#include "../control/control_policy.h"

#define P1B_TEST_ACTIVE_STEPS 1400U
#define P1B_TEST_INJECT_STEP  200U

#if P1B_AUTOTEST_ENABLE
/* Called ONLY by ControlTask. step is monotonic and bounded, no ISR/producer. */
void P1bTest_MakeCommand(uint32_t step, uint32_t now, ControlCommandSnapshot *out);
void P1bTest_InjectLateWake(uint32_t step);
void P1bTest_InjectExecution(uint32_t step);
#endif

#endif
