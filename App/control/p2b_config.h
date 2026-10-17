#ifndef RT_NODE_P2B_CONFIG_H
#define RT_NODE_P2B_CONFIG_H

/* Build permission never replaces a fresh physical KEY0 press.
 * Stage1: servo signals AND power disconnected. Stage2: reviewed G2 evidence
 * and explicit operator approval. Neither permission is enabled by default.
 */
#ifndef P2B_OUTPUT_STAGE
#define P2B_OUTPUT_STAGE 0
#endif
#ifndef P2B_OUTPUT_ARM_KEY
#define P2B_OUTPUT_ARM_KEY 0UL
#endif
#ifndef P2B_UNLOADED_ACK
#define P2B_UNLOADED_ACK 0
#endif
#ifndef P2B_G2_EVIDENCE_ACK
#define P2B_G2_EVIDENCE_ACK 0
#endif
#ifndef P2B_OPERATOR_ACK
#define P2B_OPERATOR_ACK 0
#endif
#ifndef P2B_PAN_SIGN
#define P2B_PAN_SIGN 1
#endif
#ifndef P2B_TILT_SIGN
#define P2B_TILT_SIGN 1
#endif

#if (P2B_OUTPUT_STAGE < 0) || (P2B_OUTPUT_STAGE > 2)
#error "P2B_OUTPUT_STAGE must be 0..2"
#endif
#if ((P2B_PAN_SIGN != 1) && (P2B_PAN_SIGN != -1)) || \
    ((P2B_TILT_SIGN != 1) && (P2B_TILT_SIGN != -1))
#error "P2b axis signs must be +1 or -1"
#endif
#if P2B_OUTPUT_STAGE == 0
#if P2B_OUTPUT_ARM_KEY || P2B_UNLOADED_ACK || P2B_G2_EVIDENCE_ACK || P2B_OPERATOR_ACK
#error "Stage0 must not retain P2b output authorization"
#endif
#else
#if P1A_AUTOTEST_ENABLE || P1A_ACTUATOR_TEST_ENABLE || P1B_AUTOTEST_ENABLE
#error "P2b output and P1 output tests are mutually exclusive"
#endif
#if P2B_OUTPUT_ARM_KEY != 0xB2201500UL
#error "P2b output requires explicit compile permission"
#endif
#if P2B_OUTPUT_STAGE == 1
#if (P2B_UNLOADED_ACK != 1) || P2B_G2_EVIDENCE_ACK || P2B_OPERATOR_ACK
#error "Stage1 requires unloaded acknowledgement only"
#endif
#else
#if P2B_UNLOADED_ACK || (P2B_G2_EVIDENCE_ACK != 1) || (P2B_OPERATOR_ACK != 1)
#error "Stage2 requires reviewed G2 evidence and explicit operator approval"
#endif
#endif
#endif

#define P2B_SAMPLE_TTL_TICKS 40U
#define P2B_DEADBAND_DEG 2.0f
#define P2B_MAX_ANGLE_DEG 15.0f
#define P2B_US_PER_DEG 3.0f
#define P2B_CENTER_US 1500
#define P2B_MAX_OFFSET_US 45
#define P2B_SLEW_US 2

#endif
