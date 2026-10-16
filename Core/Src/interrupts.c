#include "stm32f1xx_hal.h"
#include "p1a_safety.h"

/* P1a uses the HAL time base only; no FreeRTOS kernel is linked. */
void SysTick_Handler(void)
{
    HAL_IncTick();
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

/* These exceptions are unexpected in C1; P1b must review RTOS ownership. */
void SVC_Handler(void)
{
    P1a_FatalError();
}

void PendSV_Handler(void)
{
    P1a_FatalError();
}

void DebugMon_Handler(void)
{
    P1a_FatalError();
}
