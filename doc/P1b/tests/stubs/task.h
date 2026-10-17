#ifndef HOST_TASK_H
#define HOST_TASK_H
#include "FreeRTOS.h"
#define taskSCHEDULER_RUNNING 2
TaskHandle_t xTaskCreateStatic(void (*task)(void *), const char *name,
                              uint32_t words, void *arg, UBaseType_t priority,
                              StackType_t *stack, StaticTask_t *tcb);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskDelayUntil(TickType_t *previous, TickType_t period);
BaseType_t xTaskGetSchedulerState(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
TaskHandle_t xTaskGetIdleTaskHandle(void);
#endif
