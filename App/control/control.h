#ifndef RT_NODE_CONTROL_H
#define RT_NODE_CONTROL_H

#include "FreeRTOS.h"
#include "task.h"
#include "control_policy.h"

typedef enum
{
    CONTROL_DISABLED = 0, CONTROL_PRESTART, CONTROL_PRIMING,
    CONTROL_TEST, CONTROL_COMPLETE, CONTROL_FAULT,
    CONTROL_WAIT_ARM, CONTROL_LOCAL_FOLLOW
} ControlMode;

typedef struct
{
    ControlMode mode;
    ControlFault fault;
    uint32_t wake_count;
    uint32_t late_count;
    uint32_t last_period_ticks;
    uint32_t worst_execution_cycles;
    uint32_t min_control_stack_words;
    uint32_t min_idle_stack_words;
    uint32_t last_accepted_generation;
    uint32_t generation_fence;
    uint32_t accepted_count;
    uint32_t applied_count;
} ControlDiagnostics;

/* Main calls once after Actuator_InitSafe. Static allocation only, no output. */
bool Control_Init(void);
/* Task-context read-only copy. No UART/mailbox or output authorization. */
void Control_GetDiagnostics(ControlDiagnostics *out);

/* P3a frozen wire state values; distinct from historical ControlMode. */
typedef enum {
    P3B_DISABLED = 0, P3B_WAIT_LOCAL, P3B_PRIMING_LOCAL, P3B_ACTIVE_LOCAL,
    P3B_WAIT_REMOTE, P3B_PRIMING_REMOTE, P3B_ACTIVE_REMOTE, P3B_FAULT
} ControlWireState;
typedef struct {
    ControlWireState state;
    uint16_t epoch, last_action;
    uint8_t reason;
    bool enabled, action_accepted, remote_fresh;
    uint32_t boundary_tick, remote_tick, revision;
} ControlStatusSnapshot;
/* Task context, CanTask producer; complete copies under short critical sections.
 * No actuator access. ISR reception tick MUST be preserved. */
void Control_PostRemote(uint16_t epoch, uint16_t seq, uint16_t pan, uint16_t tilt,
                        uint32_t received_tick);
void Control_PostMode(uint8_t mode, uint16_t action, uint16_t epoch, uint32_t received_tick);
void Control_PostStop(uint16_t action);
void Control_GetStatus(ControlStatusSnapshot *out);

#endif
