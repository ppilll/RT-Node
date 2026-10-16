#ifndef RT_NODE_ACTUATOR_H
#define RT_NODE_ACTUATOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Configuration retained from VisionArm-MCU fixed commit
 * 92d8f45bc9c7df55f6e2abea4b4abfeb090a3e3d; NOT a new-board measurement.
 *
 * Electrical S20F command envelope:
 *   500..2500 us, 50 Hz.
 *
 * Installed software-safe envelope:
 *   Pan  1000..2000 us, center 1500 us.
 *   Tilt 1200..1600 us, center 1500 us.
 */
#define ACTUATOR_PWM_PERIOD_US             20000U
#define ACTUATOR_PWM_COUNTER_HZ            1000000U
#define ACTUATOR_SERVO_ELECTRICAL_MIN_US   500U
#define ACTUATOR_SERVO_CENTER_US           1500U
#define ACTUATOR_SERVO_ELECTRICAL_MAX_US   2500U

#define ACTUATOR_PAN_SAFE_MIN_US           1000U
#define ACTUATOR_PAN_CENTER_US             1500U
#define ACTUATOR_PAN_SAFE_MAX_US           2000U

#define ACTUATOR_TILT_SAFE_MIN_US          1200U
#define ACTUATOR_TILT_CENTER_US            1500U
#define ACTUATOR_TILT_SAFE_MAX_US          1600U

#if (ACTUATOR_PAN_SAFE_MIN_US < ACTUATOR_SERVO_ELECTRICAL_MIN_US) || \
    (ACTUATOR_PAN_SAFE_MAX_US > ACTUATOR_SERVO_ELECTRICAL_MAX_US)
#error "Pan software limits exceed servo electrical limits"
#endif

#if (ACTUATOR_TILT_SAFE_MIN_US < ACTUATOR_SERVO_ELECTRICAL_MIN_US) || \
    (ACTUATOR_TILT_SAFE_MAX_US > ACTUATOR_SERVO_ELECTRICAL_MAX_US)
#error "Tilt software limits exceed servo electrical limits"
#endif

#if (ACTUATOR_PAN_SAFE_MIN_US >= ACTUATOR_PAN_CENTER_US) || \
    (ACTUATOR_PAN_CENTER_US >= ACTUATOR_PAN_SAFE_MAX_US)
#error "Invalid Pan calibration ordering"
#endif

#if (ACTUATOR_TILT_SAFE_MIN_US >= ACTUATOR_TILT_CENTER_US) || \
    (ACTUATOR_TILT_CENTER_US >= ACTUATOR_TILT_SAFE_MAX_US)
#error "Invalid Tilt calibration ordering"
#endif

typedef struct
{
    int32_t pan_position_us;
    int32_t tilt_position_us;
} ActuatorCommand;

typedef struct
{
    int32_t requested_pan_us;
    int32_t requested_tilt_us;
    uint16_t applied_pan_us;
    uint16_t applied_tilt_us;
    bool pan_saturated;
    bool tilt_saturated;
} ActuatorApplyResult;

typedef struct
{
    bool initialized;
    bool enabled;
    bool fault_latched;
    uint16_t applied_pan_us;
    uint16_t applied_tilt_us;
    uint32_t command_count;
    uint32_t pan_saturation_count;
    uint32_t tilt_saturation_count;
    uint32_t enable_count;
    uint32_t disable_count;
    uint32_t fault_count;
} ActuatorSnapshot;

/* TIM3 full remap: CH1/PC6 Pan, CH2/PC7 Tilt; initialize disabled/LOW. */
bool Actuator_InitSafe(void);

/* Main context, explicit caller authorization: start at center after one LOW frame.
 * Init/Enable/Apply misuse or configuration failure shuts down and latches.
 * Repeated Init while disabled is idempotent; Init/Enable while enabled fails.
 */
bool Actuator_Enable(void);

/* Main context, enabled only. Apply independent absolute pulse-width limits.
 * Wait >= one 20 ms opening frame after Enable; premature Apply faults closed.
 * On false, result applied values are zero (no accepted request).
 */
bool Actuator_Apply(const ActuatorCommand *command,
                    ActuatorApplyResult *result);

/* Disable PWM, force PC6/PC7 LOW, discard old target; keep fault latch. */
void Actuator_Disable(void);

/*
 * Register-level emergency primitive. Safe to call with interrupts disabled
 * and without a running scheduler or initialized HAL state. Latches a fault;
 * only MCU reset clears it. NMI/fault handlers MUST enter P1a_FatalError
 * and never return to an interrupted normal register write.
 */
void Actuator_ForceSafeOutput(void);

/* Main-context snapshot; NULL is no-op. Counters wrap naturally.
 * applied = last preload target (not measured active pulse); normal Disable
 * sets center. Emergency retains last target for diagnosis with enabled=false.
 * fault_count = 0 or 1 fault episode since reset.
 * PRIMASK is preserved; NMI/fault never returns. No peripheral reads here.
 */
void Actuator_GetSnapshot(ActuatorSnapshot *snapshot);

#endif /* RT_NODE_ACTUATOR_H */
