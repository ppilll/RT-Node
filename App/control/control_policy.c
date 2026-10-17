#include "control_policy.h"
#include "control_config.h"
#include "../actuator/include/actuator.h"
#include <stddef.h>

void ControlSession_Close(ControlSession *session, ControlFault fault)
{
    session->authorized = false;
    session->terminal = true;
    if (session->fault == CONTROL_FAULT_NONE) { session->fault = fault; }
}

static bool AuthorizeSource(ControlSession *session, ControlSource source)
{
    if (session->authorization_used || session->terminal) { return false; }
    session->authorization_used = true;
    session->authorized = true;
    session->expected_source = source;
    return true;
}

bool ControlSession_Authorize(ControlSession *session)
{
    return AuthorizeSource(session, CONTROL_SOURCE_TEST);
}

bool ControlSession_AuthorizeLocal(ControlSession *session)
{
    return AuthorizeSource(session, CONTROL_SOURCE_LOCAL_IMU);
}

bool ControlSession_Accept(ControlSession *session,
                           const ControlCommandSnapshot *command, uint32_t now)
{
    uint32_t delta;
    if (session->terminal) { return false; }
    if (!session->authorized)
    {
        ControlSession_Close(session, CONTROL_FAULT_MODE);
        return false;
    }
    if (command == NULL)
    {
        ControlSession_Close(session, CONTROL_FAULT_COMMAND);
        return false;
    }
    delta = command->generation - session->last_generation;
    if (session->has_generation && ((delta == 0U) || (delta >= 0x80000000UL)))
    {
        ControlSession_Close(session, CONTROL_FAULT_GENERATION);
        return false;
    }
    /* Consume even a rejected new generation. A later close never rewinds it. */
    session->last_generation = command->generation;
    session->has_generation = true;
    if (((session->expected_source != CONTROL_SOURCE_TEST) &&
         (session->expected_source != CONTROL_SOURCE_LOCAL_IMU)) ||
        (command->source != session->expected_source) || !command->valid ||
        (command->ttl_ticks == 0U) || (command->ttl_ticks > CONTROL_MAX_TTL_TICKS))
    {
        ControlSession_Close(session, CONTROL_FAULT_COMMAND);
    }
    else if ((uint32_t)(now - command->issued_tick) >= command->ttl_ticks)
    {
        /* Unsigned modulo subtraction also rejects a future timestamp. */
        ControlSession_Close(session, CONTROL_FAULT_EXPIRED);
    }
    else if ((command->pan_us < (int32_t)ACTUATOR_PAN_SAFE_MIN_US) ||
             (command->pan_us > (int32_t)ACTUATOR_PAN_SAFE_MAX_US) ||
             (command->tilt_us < (int32_t)ACTUATOR_TILT_SAFE_MIN_US) ||
             (command->tilt_us > (int32_t)ACTUATOR_TILT_SAFE_MAX_US))
    {
        ControlSession_Close(session, CONTROL_FAULT_RANGE);
    }
    return !session->terminal;
}
