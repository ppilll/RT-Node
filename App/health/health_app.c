#include "health_app.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>

#if !defined(STM32F103xE)
#error "P6 safety page requires the audited STM32F103ZE 512 KiB layout"
#endif

#define STORE_BASE      0x0807F800UL
#define STORE_HALFWORDS (2048U / 2U)
#define RECORD_EMPTY    0xFFFFU
#define RECORD_AUTO     0x0F0FU
#define RECORD_LOCK     0x3333U
#define RECORD_MANUAL   0x5555U
#define RECOVERY_TOKEN  0x6A39U
#define RECOVERY_INVERSE 0x95C6U
#define RESET_MASK (RCC_CSR_PINRSTF | RCC_CSR_PORRSTF | RCC_CSR_SFTRSTF | \
                    RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF)

static HealthBootSnapshot s_boot;
static bool s_captured, s_finalized;
static uint32_t s_next;

static uint16_t ReadRecord(uint32_t index)
{
    return *(volatile const uint16_t *)(uintptr_t)(STORE_BASE + index * 2U);
}

static bool IsLegalTransition(HealthPersistentState previous, uint16_t next)
{
    /* EMPTY->LOCK also records an IWDG before initial provisioning. */
    if (previous == HEALTH_STORE_EMPTY_LOCKED)
    { return next == RECORD_AUTO || next == RECORD_LOCK; }
    if (previous == HEALTH_STORE_AUTO || previous == HEALTH_STORE_MANUAL_ONLY)
    { return next == RECORD_LOCK; }
    return previous == HEALTH_STORE_IWDG_LOCKED && next == RECORD_MANUAL;
}

static HealthPersistentState FlashScan(void)
{
    uint32_t i;
    bool gap = false;
    HealthPersistentState state = HEALTH_STORE_EMPTY_LOCKED;
    s_next = STORE_HALFWORDS;
    for (i = 0U; i < STORE_HALFWORDS; i++)
    {
        uint16_t record = ReadRecord(i);
        if (record == RECORD_EMPTY)
        {
            if (!gap) { s_next = i; gap = true; }
        }
        else
        {
            if (gap || !IsLegalTransition(state, record))
            { return HEALTH_STORE_BAD_LOCKED; }
            state = record == RECORD_AUTO ? HEALTH_STORE_AUTO :
                    record == RECORD_LOCK ? HEALTH_STORE_IWDG_LOCKED :
                    HEALTH_STORE_MANUAL_ONLY;
        }
    }
    /* Even a syntactically valid full page is unavailable, never repaired. */
    return gap ? state : HEALTH_STORE_BAD_LOCKED;
}

static bool FlashAppend(uint16_t record)
{
    HAL_StatusTypeDef programmed, locked;
    uint32_t index = s_next;
    if (s_finalized || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ||
        index >= STORE_HALFWORDS || ReadRecord(index) != RECORD_EMPTY ||
        !IsLegalTransition(s_boot.persistent_state, record)) { return false; }
    if (HAL_FLASH_Unlock() != HAL_OK)
    { (void)HAL_FLASH_Lock(); return false; }
    programmed = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                  STORE_BASE + index * 2U, record);
    locked = HAL_FLASH_Lock();
    if (programmed != HAL_OK || locked != HAL_OK ||
        (FLASH->CR & FLASH_CR_LOCK) == 0U || ReadRecord(index) != record)
    { return false; }
    s_boot.persistent_state = FlashScan();
    return s_boot.persistent_state != HEALTH_STORE_BAD_LOCKED;
}

static bool BkpOpen(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    /* No backup-domain reset; DR1/DR2 are exclusive to this handshake. */
    return (PWR->CR & PWR_CR_DBP) != 0U;
}

static bool BkpClear(void)
{
    BKP->DR1 = 0U; BKP->DR2 = 0U;
    __DSB();
    return (uint16_t)BKP->DR1 == 0U && (uint16_t)BKP->DR2 == 0U;
}

static HealthResetReason DecodeResetFlags(uint32_t flags)
{
    if ((flags & RCC_CSR_IWDGRSTF) != 0U) { return HEALTH_RESET_IWDG; }
    if ((flags & RCC_CSR_PORRSTF) != 0U) { return HEALTH_RESET_POWER; }
    if ((flags & (RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF)) != 0U)
    { return HEALTH_RESET_OTHER; }
    if ((flags & RCC_CSR_SFTRSTF) != 0U) { return HEALTH_RESET_SOFTWARE; }
    if ((flags & RCC_CSR_PINRSTF) != 0U) { return HEALTH_RESET_PIN; }
    return HEALTH_RESET_UNKNOWN;
}

void Health_BootCaptureReset(void)
{
    if (!s_captured)
    {
        s_boot.reset_flags_raw = RCC->CSR;
        s_boot.reset_reason = DecodeResetFlags(s_boot.reset_flags_raw);
        s_captured = true;
    }
}

bool Health_BootFinalize(void)
{
    bool bkp_ok, ticket = false, stored_ok = true;
    uint32_t flags;
    if (s_finalized) { return s_boot.persistent_state != HEALTH_STORE_BAD_LOCKED; }
    if (!s_captured || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) { return false; }
    flags = s_boot.reset_flags_raw;
    bkp_ok = BkpOpen();
    if (bkp_ok)
    {
        ticket = (uint16_t)BKP->DR1 == RECOVERY_TOKEN &&
                 (uint16_t)BKP->DR2 == RECOVERY_INVERSE;
        bkp_ok = BkpClear(); /* Consume BEFORE any recovery Flash operation. */
        HAL_PWR_DisableBkUpAccess();
    }
    s_boot.persistent_state = FlashScan();
    if ((flags & RCC_CSR_IWDGRSTF) != 0U)
    {
        if (s_boot.persistent_state != HEALTH_STORE_BAD_LOCKED &&
            s_boot.persistent_state != HEALTH_STORE_IWDG_LOCKED)
        { stored_ok = FlashAppend(RECORD_LOCK); }
    }
    /* PINRSTF can accompany other reset sources; it is not an exclusive
     * cause. Reject POR/PDR, WWDG and low-power ambiguity, retain raw CSR. */
    else if (bkp_ok && ticket &&
             ((flags & RESET_MASK & ~RCC_CSR_PINRSTF) == RCC_CSR_SFTRSTF) &&
             (s_boot.persistent_state == HEALTH_STORE_EMPTY_LOCKED ||
              s_boot.persistent_state == HEALTH_STORE_IWDG_LOCKED))
    {
        stored_ok = FlashAppend(s_boot.persistent_state == HEALTH_STORE_EMPTY_LOCKED ?
                                RECORD_AUTO : RECORD_MANUAL);
        s_boot.manual_recovery_boot = stored_ok;
    }
    if (!stored_ok || !bkp_ok) { s_boot.persistent_state = HEALTH_STORE_BAD_LOCKED; }
    s_boot.authorization_allowed = s_boot.persistent_state == HEALTH_STORE_AUTO ||
                                   s_boot.persistent_state == HEALTH_STORE_MANUAL_ONLY;
    s_boot.auto_local_allowed = s_boot.persistent_state == HEALTH_STORE_AUTO &&
                               !s_boot.manual_recovery_boot;
    /* Retain IWDG evidence on failed persistence so NRST/SW reset can retry.
     * Total power loss before successful LOCK remains a documented limit. */
    if (stored_ok && s_boot.persistent_state != HEALTH_STORE_BAD_LOCKED)
    { __HAL_RCC_CLEAR_RESET_FLAGS(); }
    s_finalized = true;
    return s_boot.persistent_state != HEALTH_STORE_BAD_LOCKED;
}

void Health_GetBootSnapshot(HealthBootSnapshot *out)
{
    if (out != NULL) { *out = s_boot; }
}

bool Health_ControlAuthorizationAllowed(void)
{ return s_finalized && s_boot.authorization_allowed; }

bool Health_BootAutoLocalAllowed(void)
{ return s_finalized && s_boot.auto_local_allowed; }

bool Health_CancelLocalRecovery(void)
{
    bool ok;
    if (__get_IPSR() != 0U || !BkpOpen()) { return false; }
    ok = BkpClear();
    HAL_PWR_DisableBkUpAccess();
    return ok;
}

bool Health_StageLocalRecovery(void)
{
    bool ok;
    if (!s_finalized || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        (s_boot.persistent_state != HEALTH_STORE_EMPTY_LOCKED &&
         s_boot.persistent_state != HEALTH_STORE_IWDG_LOCKED) ||
        s_next >= STORE_HALFWORDS - 1U || !BkpOpen()) { return false; }
    /* Invalidated first, inverse last; no partial pair is accepted on boot. */
    ok = BkpClear();
    if (ok)
    {
        BKP->DR1 = RECOVERY_TOKEN; BKP->DR2 = RECOVERY_INVERSE;
        __DSB();
        ok = (uint16_t)BKP->DR1 == RECOVERY_TOKEN &&
             (uint16_t)BKP->DR2 == RECOVERY_INVERSE;
    }
    if (!ok) { (void)BkpClear(); }
    HAL_PWR_DisableBkUpAccess();
    return ok;
}
