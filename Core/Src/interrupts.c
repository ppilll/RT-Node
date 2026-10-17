#include "stm32f1xx_hal.h"
#include "p1a_safety.h"
#include "FreeRTOS.h"
#include "task.h"

/* Startup owns vector slots; strong SVC/PendSV definitions come ONLY from
 * portable/RVDS/ARM_CM3/port.c through FreeRTOSConfig.h aliases. */
void xPortSysTickHandler(void);
void SysTick_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

/* One hardware interrupt, one HAL increment. Before scheduler start only
 * HAL runs. During scheduler suspension the port MUST still receive ticks
 * so xTaskIncrementTick can accumulate pending ticks for xTaskResumeAll.
 * No runtime clock/tick reconfiguration or HAL_SuspendTick in C1.
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}

void NMI_Handler(void)
{
    P1a_FatalError();
}

void HardFault_Handler(void)
{
    P1a_FatalError();
}

void MemManage_Handler(void)
{
    P1a_FatalError();
}

void BusFault_Handler(void)
{
    P1a_FatalError();
}

void UsageFault_Handler(void)
{
    P1a_FatalError();
}

void DebugMon_Handler(void)
{
    P1a_FatalError();
}
