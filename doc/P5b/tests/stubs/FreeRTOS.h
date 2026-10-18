#ifndef P5B_HOST_FREERTOS_H
#define P5B_HOST_FREERTOS_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef uint32_t UBaseType_t;
typedef int BaseType_t;
typedef int StaticTask_t;
typedef void *TaskHandle_t;
#define configTICK_RATE_HZ 1000
#define configUSE_16_BIT_TICKS 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configMINIMAL_STACK_SIZE 128
#define configASSERT(x) assert(x)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define tskIDLE_PRIORITY 0U
#define pdFALSE 0
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_RUNNING 2
#endif
