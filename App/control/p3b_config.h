#ifndef RT_NODE_P3B_CONFIG_H
#define RT_NODE_P3B_CONFIG_H

/* 0: no PWM; 1: servo power AND signals disconnected, LA only;
 * 2: separately approved limited loaded test (never selected by default). */
#ifndef P3B_OUTPUT_STAGE
#define P3B_OUTPUT_STAGE 0
#endif
#ifndef P3B_LOADED_APPROVED
#define P3B_LOADED_APPROVED 0
#endif
#if (P3B_OUTPUT_STAGE < 0) || (P3B_OUTPUT_STAGE > 2)
#error "P3B_OUTPUT_STAGE must be 0..2"
#endif
#if P3B_OUTPUT_STAGE == 2
#if P3B_LOADED_APPROVED != 1
#error "Stage2 requires separate explicit user approval"
#endif
#elif P3B_LOADED_APPROVED != 0
#error "Loaded approval belongs only to Stage2"
#endif
#if P2B_OUTPUT_STAGE != 0
#error "P3b replaces P2b output test stages; keep P2B_OUTPUT_STAGE=0"
#endif
#if P3B_OUTPUT_STAGE && (P1A_AUTOTEST_ENABLE || P1A_ACTUATOR_TEST_ENABLE || P1B_AUTOTEST_ENABLE)
#error "P3b output and historical output tests are mutually exclusive"
#endif
#endif
