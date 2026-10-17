#ifndef HOST_STM32_HAL_H
#define HOST_STM32_HAL_H
#include <stdint.h>
typedef struct { volatile uint32_t CTRL, CYCCNT; } HostDwt;
typedef struct { volatile uint32_t DEMCR; } HostDebug;
typedef struct { volatile uint32_t CTRL, LOAD; } HostSysTick;
extern HostDwt host_dwt;
extern HostDebug host_debug;
extern HostSysTick host_systick;
extern uint32_t SystemCoreClock;
#define DWT (&host_dwt)
#define CoreDebug (&host_debug)
#define SysTick (&host_systick)
#define DWT_CTRL_NOCYCCNT_Msk (1UL << 25)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
#define NVIC_PRIORITYGROUP_4 3U
void HostNop(void);
#define __NOP() HostNop()
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __get_IPSR() 0U
#define __get_PRIMASK() 0U
#define __get_BASEPRI() 0U
uint32_t HAL_NVIC_GetPriorityGrouping(void);
#endif
