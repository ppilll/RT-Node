#ifndef RT_NODE_CAN_TIMING_H
#define RT_NODE_CAN_TIMING_H

/* User approved this configuration for physical T1 validation, 2026-10-17.
 * APB1=36MHz: 36MHz/(4*(1+14+3))=500000bit/s; sample point=15/18.
 * Approval permits a flashable validation image; it is NOT a measured PASS.
 * TIMING_CONFIRMED remains 0 until actual T1 evidence is reviewed.
 * BS1/BS2/SJW are integer time quanta, NOT HAL register encodings.
 */
#ifndef P3A_CAN_TIMING_USER_APPROVED
#define P3A_CAN_TIMING_USER_APPROVED 1
#endif
#ifndef P3A_CAN_TIMING_CONFIRMED
#define P3A_CAN_TIMING_CONFIRMED 0
#endif
/* Explicit build-only escape authorized for P3a compilation, never evidence
 * of working bus timing. Default project does not set this permission. */
#ifndef P3A_CAN_UNVERIFIED_BUILD_ONLY
#define P3A_CAN_UNVERIFIED_BUILD_ONLY 0
#endif
#ifndef P3A_CAN_BITRATE
#define P3A_CAN_BITRATE 500000UL
#endif
#ifndef P3A_CAN_PRESCALER
#define P3A_CAN_PRESCALER 4UL
#endif
#ifndef P3A_CAN_BS1_TQ
#define P3A_CAN_BS1_TQ 14UL
#endif
#ifndef P3A_CAN_BS2_TQ
#define P3A_CAN_BS2_TQ 3UL
#endif
#ifndef P3A_CAN_SJW_TQ
#define P3A_CAN_SJW_TQ 1UL
#endif

#if (P3A_CAN_TIMING_USER_APPROVED != 1) && (P3A_CAN_TIMING_CONFIRMED != 1) && \
    (P3A_CAN_UNVERIFIED_BUILD_ONLY != 1)
#error "P3a CAN timing requires user approval, measured confirmation, or build-only permission"
#elif (P3A_CAN_BITRATE < 1) || \
    (P3A_CAN_PRESCALER < 1) || (P3A_CAN_BS1_TQ < 1) || \
    (P3A_CAN_BS2_TQ < 1) || (P3A_CAN_SJW_TQ < 1)
#error "P3a BLOCKED: CAN bitrate/Prescaler/BS1/BS2/SJW are unset"
#elif (P3A_CAN_PRESCALER > 1024) || (P3A_CAN_BS1_TQ > 16) || \
      (P3A_CAN_BS2_TQ > 8) || (P3A_CAN_SJW_TQ > 4) || \
      (P3A_CAN_SJW_TQ > P3A_CAN_BS2_TQ) || (P3A_CAN_BITRATE > 1000000UL)
#error "P3a CAN timing exceeds bxCAN limits"
#elif (36000000UL % (P3A_CAN_PRESCALER * (1UL + P3A_CAN_BS1_TQ + P3A_CAN_BS2_TQ))) != 0 || \
      (36000000UL / (P3A_CAN_PRESCALER * (1UL + P3A_CAN_BS1_TQ + P3A_CAN_BS2_TQ))) != P3A_CAN_BITRATE
#error "P3a CAN timing does not match bitrate at APB1=36MHz"
#endif

#endif
