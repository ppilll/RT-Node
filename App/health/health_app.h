#ifndef RT_NODE_HEALTH_APP_H
#define RT_NODE_HEALTH_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_STORE_EMPTY_LOCKED = 0,
    HEALTH_STORE_AUTO,
    HEALTH_STORE_IWDG_LOCKED,
    HEALTH_STORE_MANUAL_ONLY,
    HEALTH_STORE_BAD_LOCKED
} HealthPersistentState;

typedef enum {
    HEALTH_RESET_UNKNOWN = 0,
    HEALTH_RESET_POWER,
    HEALTH_RESET_IWDG,
    HEALTH_RESET_SOFTWARE,
    HEALTH_RESET_PIN,
    HEALTH_RESET_OTHER
} HealthResetReason;

typedef struct {
    uint32_t reset_flags_raw;
    HealthResetReason reset_reason;
    HealthPersistentState persistent_state;
    bool authorization_allowed;
    bool auto_local_allowed;
    bool manual_recovery_boot;
} HealthBootSnapshot;

/* main only: capture immediately after P1a safe output, before HAL. */
void Health_BootCaptureReset(void);
/* Once, with HAL timebase ready, before scheduler start. No erase. */
bool Health_BootFinalize(void);
/* Immutable after BootFinalize; no runtime Flash writes. */
void Health_GetBootSnapshot(HealthBootSnapshot *out);
bool Health_ControlAuthorizationAllowed(void);
bool Health_BootAutoLocalAllowed(void);

/* UartTask only, after both local confirmations and safety checks.
 * Success stages a request, NEVER means persistent recovery succeeded. */
bool Health_StageLocalRecovery(void);
/* Cancel staged ticket if TX/error handling prevents the controlled reset. */
bool Health_CancelLocalRecovery(void);

typedef enum {
    HEALTH_CTRL_INIT = 0,
    HEALTH_CTRL_RUNNING,
    HEALTH_CTRL_STALLED
} HealthControlProgress;

typedef enum {
    HEALTH_WDG_OFF = 0,
    HEALTH_WDG_RUNNING,
    HEALTH_WDG_START_ERROR,
    HEALTH_WDG_STARTING
} HealthWatchdogState;

typedef struct {
    HealthControlProgress control_progress;
    HealthWatchdogState watchdog;
    uint32_t last_control_wake;
    uint32_t last_progress_tick;
    uint32_t min_stack_words;
    bool test_stop_active;
} HealthRuntimeSnapshot;

/* main only, static allocation, before scheduler start. No IWDG start here. */
bool Health_InitTask(void);
/* Task context, short critical-section snapshot; no hardware IO. */
void Health_GetRuntimeSnapshot(HealthRuntimeSnapshot *out);

#endif
