#ifndef RT_NODE_CONTROL_POLICY_H
#define RT_NODE_CONTROL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { CONTROL_SOURCE_NONE = 0, CONTROL_SOURCE_TEST = 1 } ControlSource;

/* Fixed uint32_t ticks: this build requires a 32-bit, 1 kHz TickType_t.
 * Only ControlTask constructs and consumes full local snapshots.
 */
typedef struct
{
    ControlSource source;
    uint32_t generation;
    uint32_t issued_tick;
    uint32_t ttl_ticks;
    bool valid;
    int32_t pan_us;
    int32_t tilt_us;
} ControlCommandSnapshot;

typedef enum
{
    CONTROL_FAULT_NONE = 0,
    CONTROL_FAULT_MODE,
    CONTROL_FAULT_COMMAND,
    CONTROL_FAULT_GENERATION,
    CONTROL_FAULT_EXPIRED,
    CONTROL_FAULT_RANGE,
    CONTROL_FAULT_DRIVER,
    CONTROL_FAULT_DEADLINE,
    CONTROL_FAULT_EXECUTION,
    CONTROL_FAULT_CLOCK,
    CONTROL_FAULT_EXTERNAL
} ControlFault;

/* Zero-initialized once at task entry; never reset to clear a fault.
 * Authorization is consumed once per boot. Closing is terminal, even after
 * successful test completion. There is deliberately no runtime rearm API.
 */
typedef struct
{
    bool authorized;
    bool authorization_used;
    bool terminal;
    bool has_generation;
    uint32_t last_generation;
    ControlFault fault;
} ControlSession;

bool ControlSession_Authorize(ControlSession *session);
bool ControlSession_Accept(ControlSession *session,
                           const ControlCommandSnapshot *command, uint32_t now);
void ControlSession_Close(ControlSession *session, ControlFault fault);

#endif
