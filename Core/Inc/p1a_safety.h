#ifndef P1A_SAFETY_H
#define P1A_SAFETY_H

/* Internal raw hardware shutdown for cold start / normal Disable only.
 * Implemented in main.c, no HAL/RTOS dependency. This does not latch faults.
 * Emergency callers MUST use Actuator_ForceSafeOutput, or P1a_FatalError.
 */
void P1a_ForceSafeOutput(void);

/* Latching hardware shutdown then non-returning loop; recovery needs reset.
 * All NMI/fault handlers must use this, NEVER return to normal MMIO writes.
 */
void P1a_FatalError(void);

#endif /* P1A_SAFETY_H */
