#ifndef RT_NODE_CONTROL_H
#define RT_NODE_CONTROL_H

#include "FreeRTOS.h"
#include "task.h"
#include "control_policy.h"

typedef enum
{
    CONTROL_DISABLED = 0, CONTROL_PRESTART, CONTROL_PRIMING,
    CONTROL_TEST, CONTROL_COMPLETE, CONTROL_FAULT
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

#endif
