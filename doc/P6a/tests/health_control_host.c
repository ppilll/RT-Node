/* Actual health/control C with simulated registers/Flash. No board IO. */
#include <windows.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
int host_scheduler = 2;
#define main P5bRegressionMain
#include "../../P5b/tests/control_display_host.c"
#undef main
#include "../../../App/health/health_app.c"

HostRcc host_rcc;
HostPwr host_pwr;
HostBkp host_bkp;
HostFlash host_flash;
static unsigned programs, failure;
static uint16_t *page;
void HAL_PWR_EnableBkUpAccess(void) { host_pwr.CR |= PWR_CR_DBP; }
void HAL_PWR_DisableBkUpAccess(void) { host_pwr.CR &= ~PWR_CR_DBP; }
HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
    assert(host_scheduler == taskSCHEDULER_NOT_STARTED);
    if (failure == 1U) { return HAL_ERROR; }
    host_flash.CR = 0U; return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Lock(void) { host_flash.CR = FLASH_CR_LOCK; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data)
{
    uint16_t *slot = (uint16_t *)(uintptr_t)address;
    assert(host_scheduler == taskSCHEDULER_NOT_STARTED && !s_finalized);
    assert(type == FLASH_TYPEPROGRAM_HALFWORD && (address & 1U) == 0U);
    assert(address >= STORE_BASE && address < STORE_BASE + 2048U);
    assert(*slot == RECORD_EMPTY && host_flash.CR == 0U);
    programs++;
    if (failure == 2U) { return HAL_ERROR; }
    *slot = failure == 3U ? 0x7777U : (uint16_t)data;
    return HAL_OK;
}

static void EraseFixture(void)
{
    /* Test process RAM only, never calls a firmware erase function. */
    memset(page, 0xFF, 2048U); memset(&host_bkp, 0, sizeof(host_bkp));
    failure = 0U; programs = 0U;
}
static bool Boot(uint32_t flags)
{
    memset(&s_boot, 0, sizeof(s_boot));
    s_captured = false; s_finalized = false; s_next = 0U;
    host_scheduler = taskSCHEDULER_NOT_STARTED; host_rcc.CSR = flags;
    Health_BootCaptureReset();
    assert(s_boot.reset_flags_raw == flags);
    return Health_BootFinalize();
}
static void CheckPolicy(HealthPersistentState state, bool auth, bool automatic)
{
    assert(s_boot.persistent_state == state);
    assert(Health_ControlAuthorizationAllowed() == auth);
    assert(Health_BootAutoLocalAllowed() == automatic);
}
static void Ticket(void)
{
    unsigned before = programs;
    host_scheduler = taskSCHEDULER_RUNNING;
    assert(Health_StageLocalRecovery());
    assert(programs == before && host_bkp.DR1 == RECOVERY_TOKEN);
}
static void LockedControl(void)
{
    unsigned i;
    host_scheduler = taskSCHEDULER_RUNNING;
    ResetBoot(); s_run.boot_local_pending = Health_BootAutoLocalAllowed();
    for (i = 0U; i < 8U; i++)
    {
        Mode((uint8_t)(1U + i % 2U), (uint16_t)(i + 1U));
        Control_PostRemote(s_status.epoch, (uint16_t)i, 1500U, 1500U, host_tick);
        Step(20U);
        assert(s_status.state == P3B_DISABLED && !s_status.action_accepted);
        assert(!session.authorized && host_enables == 0U && host_applies == 0U);
    }
#if P3B_OUTPUT_STAGE > 0
    assert(!Enable(&session, host_tick)); Apply(&session, 1500, 1500, host_tick);
    assert(host_enables == 0U && host_applies == 0U);
#endif
    Control_PostStop(20U); Step(20U);
    assert(s_status.state == P3B_DISABLED && s_status.action_accepted);
    host_key0 = true; Step(20U); assert(s_status.key0_stop_latched);
    host_key0 = false; Step(20U); assert(s_status.key0_stop_latched);
    Stop(&session, CONTROL_FAULT_DRIVER); Step(20U);
    assert(s_status.state == P3B_FAULT); /* health cannot replace terminal fault */
}

int main(void)
{
    uint32_t reset, i;
    unsigned before;
    void *memory = VirtualAlloc((void *)(uintptr_t)0x08070000U, 65536U,
                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory == (void *)(uintptr_t)0x08070000U);
    page = (uint16_t *)(uintptr_t)STORE_BASE;
    EraseFixture(); assert(!Health_ControlAuthorizationAllowed());
    assert(Boot(RCC_CSR_PORRSTF | RCC_CSR_PINRSTF));
    CheckPolicy(HEALTH_STORE_EMPTY_LOCKED, false, false); LockedControl();
    Ticket(); assert(Boot(RCC_CSR_SFTRSTF | RCC_CSR_PINRSTF));
    CheckPolicy(HEALTH_STORE_AUTO, true, false);
    assert(s_boot.manual_recovery_boot && page[0] == RECORD_AUTO);
    assert(host_bkp.DR1 == 0U && host_bkp.DR2 == 0U);
    assert(Boot(RCC_CSR_PORRSTF)); CheckPolicy(HEALTH_STORE_AUTO, true, true);
    host_scheduler = taskSCHEDULER_RUNNING;
    assert(P5bRegressionMain() == 0); /* existing KEY0/KEY1/protocol behavior */
    host_bkp.DR1 = RECOVERY_TOKEN; host_bkp.DR2 = RECOVERY_INVERSE;
    assert(Boot(RCC_CSR_IWDGRSTF | RCC_CSR_SFTRSTF));
    CheckPolicy(HEALTH_STORE_IWDG_LOCKED, false, false);
    assert(page[1] == RECORD_LOCK && !s_boot.manual_recovery_boot);
    before = programs;
    assert(Boot(RCC_CSR_IWDGRSTF)); assert(programs == before);
    for (reset = 0U; reset < 3U; reset++)
    {
        uint32_t flags = reset == 0U ? RCC_CSR_PINRSTF :
                         reset == 1U ? RCC_CSR_SFTRSTF : RCC_CSR_PORRSTF;
        assert(Boot(flags)); CheckPolicy(HEALTH_STORE_IWDG_LOCKED, false, false);
        LockedControl();
    }
    Ticket(); assert(Boot(RCC_CSR_SFTRSTF));
    CheckPolicy(HEALTH_STORE_MANUAL_ONLY, true, false);
    assert(page[2] == RECORD_MANUAL && s_boot.manual_recovery_boot);
    for (reset = 0U; reset < 3U; reset++)
    {
        assert(Boot(reset == 0U ? RCC_CSR_PINRSTF :
                    reset == 1U ? RCC_CSR_SFTRSTF : RCC_CSR_PORRSTF));
        CheckPolicy(HEALTH_STORE_MANUAL_ONLY, true, false);
        host_scheduler = taskSCHEDULER_RUNNING; ResetBoot();
        s_run.boot_local_pending = Health_BootAutoLocalAllowed();
        Step(20U); assert(s_status.state == P3B_DISABLED && host_enables == 0U);
        Mode(2U, 1U); assert(s_status.state == P3B_WAIT_REMOTE && s_status.action_accepted);
    }
    assert(!Health_StageLocalRecovery()); /* AUTO/MANUAL cannot restage recovery */
    assert(!FlashAppend(RECORD_LOCK)); /* No post-finalize/runtime Flash */
    assert(Boot(RCC_CSR_IWDGRSTF));
    assert(page[3] == RECORD_LOCK); CheckPolicy(HEALTH_STORE_IWDG_LOCKED, false, false);

    /* Every combination of F1 reset flags: IWDG wins; only software +/- PIN
     * accepts a valid ticket. A ticket on POR/WWDG/LPWR never provisions. */
    for (reset = 0U; reset < 64U; reset++)
    {
        uint32_t flags = reset << 26;
        EraseFixture(); host_bkp.DR1 = RECOVERY_TOKEN; host_bkp.DR2 = RECOVERY_INVERSE;
        assert(Boot(flags));
        if (flags & RCC_CSR_IWDGRSTF)
        { CheckPolicy(HEALTH_STORE_IWDG_LOCKED, false, false); assert(page[0] == RECORD_LOCK); }
        else if ((flags & ~RCC_CSR_PINRSTF) == RCC_CSR_SFTRSTF)
        { CheckPolicy(HEALTH_STORE_AUTO, true, false); }
        else { CheckPolicy(HEALTH_STORE_EMPTY_LOCKED, false, false); }
        assert(host_bkp.DR1 == 0U && host_bkp.DR2 == 0U);
    }
    EraseFixture(); host_bkp.DR1 = RECOVERY_TOKEN; host_bkp.DR2 = 0U;
    assert(Boot(RCC_CSR_SFTRSTF)); CheckPolicy(HEALTH_STORE_EMPTY_LOCKED, false, false);
    Ticket(); assert(Health_CancelLocalRecovery());
    assert(Boot(RCC_CSR_SFTRSTF)); CheckPolicy(HEALTH_STORE_EMPTY_LOCKED, false, false);

    /* Illegal first record, unknown/partially programmed halfwords, holes,
     * illegal transitions, full and last-slot append all fail closed. */
    for (i = 0U; i < 65535U; i++)
    {
        if (i == RECORD_AUTO || i == RECORD_LOCK) { continue; }
        EraseFixture(); page[0] = (uint16_t)i;
        assert(!Boot(RCC_CSR_PINRSTF)); CheckPolicy(HEALTH_STORE_BAD_LOCKED, false, false);
    }
    EraseFixture(); page[1] = RECORD_AUTO;
    assert(!Boot(RCC_CSR_PINRSTF)); CheckPolicy(HEALTH_STORE_BAD_LOCKED, false, false);
    EraseFixture(); page[0] = RECORD_AUTO; page[1] = RECORD_AUTO;
    assert(!Boot(RCC_CSR_PINRSTF)); CheckPolicy(HEALTH_STORE_BAD_LOCKED, false, false);
    EraseFixture(); page[0] = RECORD_AUTO;
    for (i = 1U; i < STORE_HALFWORDS; i++)
    { page[i] = (i & 1U) ? RECORD_LOCK : RECORD_MANUAL; }
    assert(!Boot(RCC_CSR_PINRSTF)); CheckPolicy(HEALTH_STORE_BAD_LOCKED, false, false);
    page[STORE_HALFWORDS - 1U] = RECORD_EMPTY;
    assert(Boot(RCC_CSR_PINRSTF)); CheckPolicy(HEALTH_STORE_MANUAL_ONLY, true, false);
    assert(!Boot(RCC_CSR_IWDGRSTF)); CheckPolicy(HEALTH_STORE_BAD_LOCKED, false, false);
    host_scheduler = taskSCHEDULER_RUNNING; assert(!Health_StageLocalRecovery());

    for (i = 1U; i <= 3U; i++)
    {
        EraseFixture(); page[0] = RECORD_AUTO; failure = i;
        assert(!Boot(RCC_CSR_IWDGRSTF)); CheckPolicy(HEALTH_STORE_BAD_LOCKED, false, false);
        assert(host_rcc.CSR == RCC_CSR_IWDGRSTF); /* preserve evidence on failure */
        failure = 0U;
        if (i != 3U)
        { assert(Boot(host_rcc.CSR)); CheckPolicy(HEALTH_STORE_IWDG_LOCKED, false, false); }
    }
    printf("P6a health/control assertions PASS (simulated stage %d); hardware NOT RUN\n", P3B_OUTPUT_STAGE);
    return 0;
}
