#include "control.h"
#include "control_config.h"
#include "../p1b_test/p1b_test.h"
#include "../actuator/include/actuator.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>
#include "../can/can_app.h"
#if !P1B_AUTOTEST_ENABLE
#include "local_follow.h"
#include "../imu/include/imu.h"
#include <float.h>
#endif

#if (configTICK_RATE_HZ != 1000) || (configUSE_16_BIT_TICKS != 0)
#error "Control snapshots require 32-bit, 1 kHz RTOS ticks"
#endif
#if (configSUPPORT_STATIC_ALLOCATION != 1) || (configSUPPORT_DYNAMIC_ALLOCATION != 0)
#error "ControlTask requires static-only RTOS allocation"
#endif

#define CYCLES_PER_US 72U
#define CYCLES_PER_TICK 72000U
typedef char TickWidthMustBe32[(sizeof(TickType_t) == sizeof(uint32_t)) ? 1 : -1];

static StaticTask_t s_control_tcb;
static StackType_t s_control_stack[CONTROL_STACK_WORDS];
static TaskHandle_t s_control_handle;
static volatile ControlDiagnostics s_diagnostics;
static void ControlTask(void *argument);

bool Control_Init(void)
{
    if (s_control_handle != NULL) { return false; }
    s_control_handle = xTaskCreateStatic(ControlTask, "ControlTask", CONTROL_STACK_WORDS,
                                        NULL, tskIDLE_PRIORITY + 1U,
                                        s_control_stack, &s_control_tcb);
    return s_control_handle != NULL;
}

void Control_GetDiagnostics(ControlDiagnostics *out)
{
    if (out == NULL) { return; }
    configASSERT(__get_IPSR() == 0U);
    taskENTER_CRITICAL();
    *out = s_diagnostics;
    taskEXIT_CRITICAL();
}

static void Stop(ControlSession *session, ControlFault reason)
{
    /* Hardware first. A normal finite test end does not invent a fault.
     * Every error latches in the existing driver, before touching diagnostics.
     */
    if (reason == CONTROL_FAULT_NONE) { Actuator_Disable(); }
    else { Actuator_ForceSafeOutput(); }
    ControlSession_Close(session, reason);
    s_diagnostics.mode = (reason == CONTROL_FAULT_NONE) ? CONTROL_COMPLETE : CONTROL_FAULT;
    s_diagnostics.fault = session->fault;
    s_diagnostics.generation_fence = session->last_generation;
}

static bool StartCycleCounter(void)
{
    uint32_t before;
    uint32_t n;
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U) { return false; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    before = DWT->CYCCNT;
    for (n = 0U; n < 128U; n++) { __NOP(); }
    return ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) && (DWT->CYCCNT != before);
}

static void SampleStack(void)
{
    UBaseType_t control = uxTaskGetStackHighWaterMark(NULL);
    UBaseType_t idle = uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle());
    if (control < s_diagnostics.min_control_stack_words)
    {
        s_diagnostics.min_control_stack_words = control;
    }
    if (idle < s_diagnostics.min_idle_stack_words)
    {
        s_diagnostics.min_idle_stack_words = idle;
    }
}

#if P1B_AUTOTEST_ENABLE || (P3B_OUTPUT_STAGE > 0)
static bool Accept(ControlSession *session, const ControlCommandSnapshot *command,
                   TickType_t now)
{
    if (!ControlSession_Accept(session, command, now))
    {
        Stop(session, session->fault);
        return false;
    }
    s_diagnostics.last_accepted_generation = command->generation;
    s_diagnostics.generation_fence = session->last_generation;
    s_diagnostics.accepted_count++;
    return true;
}
#endif

#if !P1B_AUTOTEST_ENABLE
/* Elite V2 candidate only: KEY0 PE4 / KEY1 PE3, pull-up, active low.
 * No GPIO interrupt, task or trace. Physical board confirmation is a G1 gate.
 */
static void InitKeys(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &gpio);
}
#endif


/* One mailbox set. CanTask posts complete data, ControlTask consumes events
 * and alone publishes the authoritative status. No second RTOS queue. */
typedef struct {
    uint16_t epoch, seq, pan, tilt;
    uint32_t tick, generation;
    bool valid;
} RemoteTarget;
typedef struct {
    uint8_t mode;
    uint16_t action, epoch;
    uint32_t tick;
    bool eligible, replay, accepted;
} ModeEvent;
static struct {
    RemoteTarget remote;
    ModeEvent mode, cache;
    bool mode_pending, cache_valid, cache_complete;
    bool stop_pending, rejected_pending, has_action, has_seq;
    uint16_t stop_action, rejected_action, action_fence, seq;
    uint32_t generation;
} s_mail;
static ControlStatusSnapshot s_status;

static bool New16(uint16_t next, uint16_t previous)
{
    uint16_t delta = (uint16_t)(next - previous);
    return delta != 0U && delta < 0x8000U;
}
static bool After(uint32_t tick, uint32_t boundary)
{
    uint32_t delta = tick - boundary;
    return delta != 0U && delta < 0x80000000UL;
}
static bool RemoteState(ControlWireState state)
{
    return state >= P3B_WAIT_REMOTE && state <= P3B_ACTIVE_REMOTE;
}
void Control_GetStatus(ControlStatusSnapshot *out)
{
    if (out == NULL) { return; }
    taskENTER_CRITICAL();
    *out = s_status;
    taskEXIT_CRITICAL();
}
void Control_PostRemote(uint16_t epoch, uint16_t seq, uint16_t pan, uint16_t tilt,
                        uint32_t received_tick)
{
    taskENTER_CRITICAL();
    if (!s_mail.stop_pending && RemoteState(s_status.state) && epoch == s_status.epoch &&
        After(received_tick, s_status.boundary_tick) &&
        (uint32_t)(xTaskGetTickCount() - received_tick) < CONTROL_MAX_TTL_TICKS &&
        pan >= 1000U && pan <= 2000U && tilt >= 1200U && tilt <= 1600U &&
        (!s_mail.has_seq || New16(seq, s_mail.seq)))
    {
        s_mail.has_seq = true;
        s_mail.seq = seq;
        s_mail.remote.epoch = epoch;
        s_mail.remote.seq = seq;
        s_mail.remote.pan = pan;
        s_mail.remote.tilt = tilt;
        s_mail.remote.tick = received_tick;
        s_mail.remote.generation = ++s_mail.generation;
        s_mail.remote.valid = true;
    }
    taskEXIT_CRITICAL();
}
void Control_PostMode(uint8_t mode, uint16_t action, uint16_t epoch, uint32_t tick)
{
    ModeEvent event;
    bool same;
    if (mode > 2U) { return; }
    event.mode = mode; event.action = action; event.epoch = epoch; event.tick = tick;
    event.replay = false; event.accepted = false;
    taskENTER_CRITICAL();
    same = s_mail.cache_valid && action == s_mail.cache.action &&
           mode == s_mail.cache.mode && epoch == s_mail.cache.epoch &&
           s_mail.has_action && action == s_mail.action_fence;
    event.eligible = !s_mail.has_action || New16(action, s_mail.action_fence);
    if (event.eligible) { s_mail.has_action = true; s_mail.action_fence = action; }
    if (same && !s_mail.cache_complete && s_mail.mode_pending)
    { /* Identical retransmission of the pending action: coalesce. */ }
    else if (s_mail.mode_pending || s_mail.stop_pending)
    {
        /* Never replace the first request. A single latest busy rejection is
         * sufficient: STATUS is a snapshot, not a guaranteed ACK history. */
        s_mail.rejected_pending = true; s_mail.rejected_action = action;
    }
    else
    {
        if (same && s_mail.cache_complete)
        { event.replay = true; event.accepted = s_mail.cache.accepted; }
        s_mail.mode = event; s_mail.mode_pending = true;
        if (event.eligible)
        { s_mail.cache = event; s_mail.cache_valid = true; s_mail.cache_complete = false; }
    }
    taskEXIT_CRITICAL();
}
void Control_PostStop(uint16_t action)
{
    taskENTER_CRITICAL();
    s_mail.stop_pending = true; s_mail.stop_action = action;
    if (!s_mail.has_action || New16(action, s_mail.action_fence))
    { s_mail.has_action = true; s_mail.action_fence = action; }
    taskEXIT_CRITICAL();
}

#if !P1B_AUTOTEST_ENABLE
/* All fields below have a single writer: ControlTask. Static to preserve its
 * original 256-word stack. Wire state is deliberately separate from diagnostics. */
static struct {
    ControlStatusSnapshot status;
    LocalFollowState follow;
    RemoteTarget remote;
    uint32_t enabled_tick, enabled_cycle;
    uint32_t key0_up, key0_down, key1_down;
    bool released_ready;
    int32_t pan, tilt;
} s_run;

static uint8_t RuntimeMode(void)
{
    if (s_run.status.state >= P3B_WAIT_LOCAL && s_run.status.state <= P3B_ACTIVE_LOCAL) { return 1U; }
    return RemoteState(s_run.status.state) ? 2U : 0U;
}
static void ResetKeys(void)
{
    s_run.key0_up = 0U; s_run.key0_down = 0U; s_run.released_ready = false;
}
static void Invalidate(uint32_t now)
{
    s_run.status.boundary_tick = now;
    s_run.remote.valid = false;
    s_run.pan = 1500; s_run.tilt = 1500;
    ResetKeys();
    taskENTER_CRITICAL();
    s_mail.remote.valid = false; s_mail.has_seq = false;
    s_mail.mode_pending = false; s_mail.rejected_pending = false;
    s_mail.cache_valid = false;
    taskEXIT_CRITICAL();
}
static void Disarm(ControlSession *session, uint8_t reason, uint32_t now)
{
    Actuator_Disable();
    now = xTaskGetTickCount(); /* actual disabled boundary, not cycle entry */
    if (!ControlSession_DisarmNormal(session)) { return; }
    if (s_run.status.state != P3B_DISABLED) { s_run.status.epoch++; }
    Invalidate(now);
    s_run.status.state = P3B_DISABLED;
    s_run.status.reason = reason;
    s_diagnostics.mode = CONTROL_DISABLED;
}
static void PublishStatus(const ControlSession *session)
{
    ActuatorSnapshot actuator;
    uint32_t now;
    Actuator_GetSnapshot(&actuator);
    now = xTaskGetTickCount();
    if (session->terminal || actuator.fault_latched)
    {
        s_run.status.state = P3B_FAULT;
        s_run.status.reason = (uint8_t)(0x80U + (uint32_t)session->fault);
    }
    s_run.status.enabled = actuator.enabled;
    s_run.status.remote_tick = s_run.remote.tick;
    s_run.status.remote_fresh = RemoteState(s_run.status.state) && s_run.remote.valid &&
        s_run.remote.epoch == s_run.status.epoch &&
        (uint32_t)(now - s_run.remote.tick) < CONTROL_MAX_TTL_TICKS;
    taskENTER_CRITICAL();
    s_status = s_run.status;
    taskEXIT_CRITICAL();
}
#if P3B_OUTPUT_STAGE > 0
static bool Enable(ControlSession *session, uint32_t now)
{
    /* ISR overflow cannot slip between the final safety check and driver call.
     * CAN STOP posting is task-context at lower priority; it cannot preempt us. */
    bool overflow, expired, ok = false;
    taskENTER_CRITICAL();
    overflow = CanApp_TakeOverflow();
    expired = RuntimeMode() == 2U &&
        (uint32_t)(xTaskGetTickCount() - s_run.remote.tick) >= CONTROL_MAX_TTL_TICKS;
    if (!overflow && !expired) { ok = Actuator_Enable(); }
    taskEXIT_CRITICAL();
    if (overflow || expired) { Disarm(session, overflow ? 0x04U : 0x03U, now); return false; }
    if (!ok) { Stop(session, CONTROL_FAULT_DRIVER); return false; }
    s_run.enabled_tick = xTaskGetTickCount();
    s_run.enabled_cycle = DWT->CYCCNT;
    return true;
}
static int32_t Slew(int32_t target, int32_t previous)
{
    if (target > previous + 2) { return previous + 2; }
    if (target < previous - 2) { return previous - 2; }
    return target;
}
static void Apply(ControlSession *session, int32_t pan, int32_t tilt, uint32_t now)
{
    ActuatorCommand command;
    ActuatorApplyResult result;
    bool overflow, expired, ok = false;
    command.pan_position_us = pan; command.tilt_position_us = tilt;
    taskENTER_CRITICAL();
    overflow = CanApp_TakeOverflow();
    expired = RuntimeMode() == 2U &&
        (uint32_t)(xTaskGetTickCount() - s_run.remote.tick) >= CONTROL_MAX_TTL_TICKS;
    if (!overflow && !expired) { ok = Actuator_Apply(&command, &result); }
    taskEXIT_CRITICAL();
    if (overflow || expired) { Disarm(session, overflow ? 0x04U : 0x03U, now); }
    else if (!ok) { Stop(session, CONTROL_FAULT_DRIVER); }
    else { s_diagnostics.applied_count++; }
}
static void Motion(ControlSession *session, uint32_t now, bool key0, bool key1)
{
    ControlCommandSnapshot command;
    bool primed;
    if (RuntimeMode() == 1U || s_run.status.state == P3B_DISABLED)
    {
        ImuAttitudeSnapshot imu;
        bool available = Imu_GetSnapshot(&imu);
        bool ready = available && imu.state == IMU_READY && imu.valid;
        now = xTaskGetTickCount();
        ready = ready && (uint32_t)(now - imu.sample_tick) < P2B_SAMPLE_TTL_TICKS &&
            imu.roll_deg >= -FLT_MAX && imu.roll_deg <= FLT_MAX &&
            imu.pitch_deg >= -FLT_MAX && imu.pitch_deg <= FLT_MAX;
        /* IMU is required only by an authorized LOCAL session. REMOTE and
         * DISABLED must not latch a spurious IMU failure. */
        if (session->authorized && !ready) { Stop(session, CONTROL_FAULT_COMMAND); return; }
        if (!session->authorized)
        {
            if (!ready || key1) { ResetKeys(); return; }
            if (!key0)
            {
                s_run.key0_down = 0U;
                if (s_run.key0_up < 2U) { s_run.key0_up++; }
                if (s_run.key0_up >= 2U) { s_run.released_ready = true; }
            }
            else
            {
                s_run.key0_up = 0U;
                if (s_run.key0_down < 2U) { s_run.key0_down++; }
            }
            if (!key0 || s_run.key0_down < 2U || !s_run.released_ready) { return; }
            if (s_run.status.state == P3B_DISABLED)
            {
                Actuator_Disable(); s_run.status.epoch++; Invalidate(xTaskGetTickCount());
                s_run.status.state = P3B_WAIT_LOCAL;
            }
            if (!LocalFollow_Start(&s_run.follow, imu.roll_deg, imu.pitch_deg))
            { Stop(session, CONTROL_FAULT_COMMAND); return; }
            if (!ControlSession_AuthorizeLocal(session)) { Stop(session, CONTROL_FAULT_MODE); return; }
        }
        command.source = CONTROL_SOURCE_LOCAL_IMU;
        command.generation = imu.generation; command.issued_tick = imu.sample_tick;
        command.ttl_ticks = P2B_SAMPLE_TTL_TICKS; command.valid = true;
        command.pan_us = 1500; command.tilt_us = 1500;
        primed = s_run.status.state != P3B_WAIT_LOCAL &&
            (uint32_t)(now - s_run.enabled_tick) >= CONTROL_PRIMING_TICKS &&
            (uint32_t)(DWT->CYCCNT - s_run.enabled_cycle) >= CONTROL_PRIMING_TICKS * CYCLES_PER_TICK;
        if (primed && !LocalFollow_Target(&s_run.follow, imu.roll_deg, imu.pitch_deg,
                                          &command.pan_us, &command.tilt_us))
        { Stop(session, CONTROL_FAULT_COMMAND); return; }
        if (!Accept(session, &command, xTaskGetTickCount())) { return; }
        if (s_run.status.state == P3B_WAIT_LOCAL)
        {
            if (Enable(session, now)) { s_run.status.state = P3B_PRIMING_LOCAL; }
        }
        else if (primed)
        {
            s_run.status.state = P3B_ACTIVE_LOCAL;
            s_diagnostics.mode = CONTROL_LOCAL_FOLLOW;
            Apply(session, command.pan_us, command.tilt_us, now);
        }
    }
    else if (RemoteState(s_run.status.state) && s_run.remote.valid)
    {
        now = xTaskGetTickCount();
        if ((uint32_t)(now - s_run.remote.tick) >= CONTROL_MAX_TTL_TICKS)
        { Disarm(session, 0x03U, now); return; }
        if (!session->authorized && !ControlSession_AuthorizeRemote(session))
        { Stop(session, CONTROL_FAULT_MODE); return; }
        if (!session->has_generation || session->last_generation != s_run.remote.generation)
        {
            command.source = CONTROL_SOURCE_REMOTE_CAN;
            command.generation = s_run.remote.generation; command.issued_tick = s_run.remote.tick;
            command.ttl_ticks = CONTROL_MAX_TTL_TICKS; command.valid = true;
            command.pan_us = s_run.remote.pan; command.tilt_us = s_run.remote.tilt;
            if (!Accept(session, &command, now)) { return; }
        }
        if (s_run.status.state == P3B_WAIT_REMOTE)
        {
            if (Enable(session, now)) { s_run.status.state = P3B_PRIMING_REMOTE; }
        }
        else if ((uint32_t)(now - s_run.enabled_tick) >= CONTROL_PRIMING_TICKS &&
                 (uint32_t)(DWT->CYCCNT - s_run.enabled_cycle) >= CONTROL_PRIMING_TICKS * CYCLES_PER_TICK)
        {
            s_run.pan = Slew(s_run.remote.pan, s_run.pan);
            s_run.tilt = Slew(s_run.remote.tilt, s_run.tilt);
            s_run.status.state = P3B_ACTIVE_REMOTE;
            s_diagnostics.mode = CONTROL_TEST;
            Apply(session, s_run.pan, s_run.tilt, now);
        }
    }
}
#endif
static void P3bCycle(ControlSession *session)
{
    uint32_t now = xTaskGetTickCount();
    ModeEvent mode;
    RemoteTarget remote;
    bool stop, pending, rejected, overflow;
    uint16_t stop_action, rejected_action;
    bool key0 = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET;
    bool key1 = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET;
    if (key1) { if (s_run.key1_down < 2U) { s_run.key1_down++; } }
    else { s_run.key1_down = 0U; }
    taskENTER_CRITICAL();
    stop = s_mail.stop_pending; stop_action = s_mail.stop_action; s_mail.stop_pending = false;
    pending = s_mail.mode_pending; mode = s_mail.mode; s_mail.mode_pending = false;
    rejected = s_mail.rejected_pending; rejected_action = s_mail.rejected_action;
    s_mail.rejected_pending = false;
    remote = s_mail.remote;
    overflow = CanApp_TakeOverflow();
    taskEXIT_CRITICAL();
#if P3B_OUTPUT_STAGE > 0
    if (!session->terminal && session->authorized && RuntimeMode() == 1U)
    {
        ImuAttitudeSnapshot imu;
        bool ready = Imu_GetSnapshot(&imu) && imu.state == IMU_READY && imu.valid;
        uint32_t sample_now = xTaskGetTickCount();
        ready = ready && (uint32_t)(sample_now - imu.sample_tick) < P2B_SAMPLE_TTL_TICKS &&
            imu.roll_deg >= -FLT_MAX && imu.roll_deg <= FLT_MAX &&
            imu.pitch_deg >= -FLT_MAX && imu.pitch_deg <= FLT_MAX;
        if (!ready) { Stop(session, CONTROL_FAULT_COMMAND); }
    }
#endif
    if (stop || pending || rejected) { s_run.status.revision++; }
    /* Fatal state is never replaced by normal stops or requests. */
    if (session->terminal)
    {
        if (stop) { s_run.status.last_action = stop_action; s_run.status.action_accepted = true; }
        else if (pending || rejected)
        { s_run.status.last_action = rejected ? rejected_action : mode.action; s_run.status.action_accepted = false; }
        return;
    }
    if (stop || s_run.key1_down >= 2U)
    {
        Disarm(session, stop ? 0x02U : 0x01U, now);
        if (stop) { s_run.status.last_action = stop_action; s_run.status.action_accepted = true; }
        return;
    }
    if (overflow) { Disarm(session, 0x04U, now); return; }
    /* Expiry has priority over MODE and output, including a simultaneously
     * pending request. No new generation is accepted to resurrect an expired
     * session after a >=100ms gap between ISR reception times. */
    if (RemoteState(s_run.status.state) && s_run.remote.valid &&
        (uint32_t)(now - s_run.remote.tick) >= CONTROL_MAX_TTL_TICKS &&
        (!remote.valid || remote.generation == s_run.remote.generation ||
         (uint32_t)(remote.tick - s_run.remote.tick) >= CONTROL_MAX_TTL_TICKS))
    { Disarm(session, 0x03U, now); return; }
    if (pending)
    {
        bool switched = false;
        bool accepted = mode.replay ? mode.accepted :
            mode.eligible && mode.epoch == s_run.status.epoch &&
            After(mode.tick, s_run.status.boundary_tick);
        if (accepted && !mode.replay && mode.mode != RuntimeMode())
        {
            switched = true;
            Disarm(session, 0U, now);
            /* DISABLED -> LOCAL/REMOTE is also a real mode transition. */
            if (mode.mode != 0U && s_run.status.state == P3B_DISABLED &&
                mode.epoch == s_run.status.epoch) { s_run.status.epoch++; }
            s_run.status.state = mode.mode == 1U ? P3B_WAIT_LOCAL :
                                 mode.mode == 2U ? P3B_WAIT_REMOTE : P3B_DISABLED;
        }
        s_run.status.last_action = mode.action; s_run.status.action_accepted = accepted;
        s_run.status.reason = accepted ? 0U : 0x05U;
        taskENTER_CRITICAL();
        if (mode.eligible)
        { s_mail.cache = mode; s_mail.cache.accepted = accepted; s_mail.cache_valid = true; s_mail.cache_complete = true; }
        taskEXIT_CRITICAL();
        if (rejected)
        { s_run.status.last_action = rejected_action; s_run.status.action_accepted = false; s_run.status.reason = 0x05U; }
        /* A real switch is an output-free cycle, even if KEY0 was already down. */
        if (switched) { return; }
    }
    if (rejected)
    { s_run.status.last_action = rejected_action; s_run.status.action_accepted = false; s_run.status.reason = 0x05U; }
    if (RemoteState(s_run.status.state) && remote.valid && remote.epoch == s_run.status.epoch &&
        After(remote.tick, s_run.status.boundary_tick)) { s_run.remote = remote; }
    if (RemoteState(s_run.status.state) && s_run.remote.valid &&
        (uint32_t)(now - s_run.remote.tick) >= CONTROL_MAX_TTL_TICKS)
    { Disarm(session, 0x03U, now); return; }
#if P3B_OUTPUT_STAGE > 0
    Motion(session, now, key0, key1);
#else
    (void)key0;
#endif
}
#endif

static void ControlTask(void *argument)
{
    ControlSession session = {0};
    ActuatorSnapshot state;
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t previous_wake = last_wake;
    uint32_t previous_cycle = 0U;
    bool has_previous = false;
#if P1B_AUTOTEST_ENABLE
    TickType_t boot_tick = last_wake;
    TickType_t enabled_tick = 0U;
    uint32_t enabled_cycle = 0U;
    uint32_t step = 0U;
    ControlCommandSnapshot command;
    ActuatorCommand actuator_command;
    ActuatorApplyResult result;
#endif
    (void)argument;

    /* Retain the reviewed C1 first-task checks after SVC restores PSP/BASEPRI. */
    configASSERT(__get_IPSR() == 0U);
    configASSERT(__get_PRIMASK() == 0U);
    configASSERT(__get_BASEPRI() == 0U);
    configASSERT(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    configASSERT(HAL_NVIC_GetPriorityGrouping() == NVIC_PRIORITYGROUP_4);
    configASSERT(SystemCoreClock == 72000000U);
    configASSERT(SysTick->LOAD == (SystemCoreClock / configTICK_RATE_HZ) - 1U);
    Actuator_GetSnapshot(&state);
    configASSERT(state.initialized && !state.enabled && !state.fault_latched);
    s_diagnostics.min_control_stack_words = CONTROL_STACK_WORDS;
    s_diagnostics.min_idle_stack_words = configMINIMAL_STACK_SIZE;
#if P1B_AUTOTEST_ENABLE
    s_diagnostics.mode = CONTROL_PRESTART;
#else
    s_run.status.boundary_tick = last_wake;
    InitKeys();
#endif
    if (!StartCycleCounter()) { Stop(&session, CONTROL_FAULT_CLOCK); }
    /* Counter initialization is before the first scheduling epoch. */
    last_wake = xTaskGetTickCount();
    previous_wake = last_wake;

    for (;;)
    {
        uint32_t entry_cycle = DWT->CYCCNT;
        TickType_t now;
        uint32_t elapsed_cycles;
        uint32_t period;
#if P1B_AUTOTEST_ENABLE
        if (!session.terminal && session.authorized)
        {
            P1bTest_InjectLateWake(step);
        }
#endif
        now = xTaskGetTickCount();
        period = (uint32_t)(now - previous_wake);
        s_diagnostics.wake_count++;
        s_diagnostics.last_period_ticks = has_previous ? period : 0U;
        if (!session.terminal)
        {
            if (((TickType_t)(now - last_wake) > CONTROL_MAX_LATE_TICKS) ||
                (has_previous && ((period < 19U) || (period > 21U))))
            {
                s_diagnostics.late_count++;
                Stop(&session, CONTROL_FAULT_DEADLINE);
            }
            else if (has_previous &&
                (((uint32_t)(entry_cycle - previous_cycle) < (period - 2U) * CYCLES_PER_TICK) ||
                 ((uint32_t)(entry_cycle - previous_cycle) > (period + 2U) * CYCLES_PER_TICK)))
            {
                Stop(&session, CONTROL_FAULT_CLOCK);
            }
            else if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
            {
                Stop(&session, CONTROL_FAULT_CLOCK);
            }
            else
            {
                Actuator_GetSnapshot(&state);
                if (state.fault_latched) { Stop(&session, CONTROL_FAULT_EXTERNAL); }
            }
        }

#if P1B_AUTOTEST_ENABLE
        if (!session.terminal && (s_diagnostics.mode == CONTROL_PRESTART) &&
            ((TickType_t)(now - boot_tick) >= CONTROL_PRESTART_TICKS))
        {
            if (!ControlSession_Authorize(&session)) { Stop(&session, CONTROL_FAULT_MODE); }
            else
            {
                P1bTest_MakeCommand(step, now, &command);
                if (Accept(&session, &command, now))
                {
                    if (!Actuator_Enable()) { Stop(&session, CONTROL_FAULT_DRIVER); }
                    else
                    {
                        enabled_tick = xTaskGetTickCount();
                        enabled_cycle = DWT->CYCCNT;
                        s_diagnostics.mode = CONTROL_PRIMING;
                        step++;
                    }
                }
            }
        }
        else if (!session.terminal && session.authorized)
        {
            if (step >= P1B_TEST_ACTIVE_STEPS) { Stop(&session, CONTROL_FAULT_NONE); }
            else
            {
                P1bTest_MakeCommand(step, now, &command);
                if (Accept(&session, &command, now))
                {
                    /* >=40 actual ms, checked with both clocks, before Apply.
                     * No TIM3 access here. The driver still rejects missing UIF.
                     */
                    if (((TickType_t)(now - enabled_tick) >= CONTROL_PRIMING_TICKS) &&
                        ((uint32_t)(DWT->CYCCNT - enabled_cycle) >=
                         CONTROL_PRIMING_TICKS * CYCLES_PER_TICK))
                    {
                        s_diagnostics.mode = CONTROL_TEST;
                        actuator_command.pan_position_us = command.pan_us;
                        actuator_command.tilt_position_us = command.tilt_us;
                        if (!Actuator_Apply(&actuator_command, &result))
                        {
                            Stop(&session, CONTROL_FAULT_DRIVER);
                        }
                        else { s_diagnostics.applied_count++; }
                    }
                    P1bTest_InjectExecution(step);
                    step++;
                }
            }
        }
#else
        P3bCycle(&session);
#endif
#if !P1B_AUTOTEST_ENABLE
        PublishStatus(&session);
#endif
        SampleStack();
        elapsed_cycles = DWT->CYCCNT - entry_cycle;
        if (elapsed_cycles > s_diagnostics.worst_execution_cycles)
        {
            s_diagnostics.worst_execution_cycles = elapsed_cycles;
        }
        if (!session.terminal && (elapsed_cycles >= CONTROL_EXECUTION_US * CYCLES_PER_US))
        {
            Stop(&session, CONTROL_FAULT_EXECUTION);
#if !P1B_AUTOTEST_ENABLE
            PublishStatus(&session);
#endif
        }
        previous_wake = now;
        previous_cycle = entry_cycle;
        has_previous = true;
        if (xTaskDelayUntil(&last_wake, CONTROL_PERIOD_TICKS) == pdFALSE)
        {
            if (!session.terminal)
            {
                s_diagnostics.late_count++;
                Stop(&session, CONTROL_FAULT_DEADLINE);
#if !P1B_AUTOTEST_ENABLE
                PublishStatus(&session);
#endif
            }
            /* Resynchronize only AFTER terminal shutdown; no control catch-up. */
            last_wake = xTaskGetTickCount();
        }
    }
}
