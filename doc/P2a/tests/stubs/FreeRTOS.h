#ifndef P2A_HOST_FREERTOS_H
#define P2A_HOST_FREERTOS_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define configTICK_RATE_HZ 1000
#define configUSE_16_BIT_TICKS 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configASSERT(x) assert(x)
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct { uint32_t placeholder; } StaticTask_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;
#define tskIDLE_PRIORITY 0U
#define pdFALSE 0
#define pdTRUE 1
void HostEnter(void);
void HostExit(void);
#define taskENTER_CRITICAL() HostEnter()
#define taskEXIT_CRITICAL() HostExit()
#endif
