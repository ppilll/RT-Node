#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef uint32_t UBaseType_t;
typedef int32_t BaseType_t;
typedef struct { uint32_t words[18]; } StaticTask_t;
typedef void *TaskHandle_t;
#define configTICK_RATE_HZ 1000
#define configUSE_16_BIT_TICKS 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configMINIMAL_STACK_SIZE 128U
#define pdFALSE 0
#define pdTRUE 1
#define tskIDLE_PRIORITY 0U
void HostAssert(int condition, int line);
#define configASSERT(x) HostAssert(!!(x), __LINE__)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif
