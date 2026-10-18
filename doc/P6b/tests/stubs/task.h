#ifndef P6B_HOST_TASK_H
#define P6B_HOST_TASK_H
#if !defined(P6B_RUNTIME_HOST)
#include "../../../P6a/tests/stubs/task.h"
#else
#include "FreeRTOS.h"
extern uint32_t host_tick;
extern int host_scheduler;
static TickType_t xTaskGetTickCount(void) { return host_tick; }
static int xTaskGetSchedulerState(void) { return host_scheduler; }
TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name,
    uint32_t size, void *arg, uint32_t priority, StackType_t *stack, StaticTask_t *tcb);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
BaseType_t xTaskDelayUntil(TickType_t *tick, TickType_t delay);
#endif
#endif
