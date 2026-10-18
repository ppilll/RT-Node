#ifndef P6A_HOST_TASK_H
#define P6A_HOST_TASK_H
#include "FreeRTOS.h"
#define vTaskNotifyGiveFromISR(task, woken) ((void)0)
#define portYIELD_FROM_ISR(woken) ((void)0)
#define ulTaskNotifyTake(clear, ticks) 0U
#define taskYIELD() ((void)0)
#define pdTRUE 1
extern uint32_t host_tick;
extern int host_scheduler;
static TickType_t xTaskGetTickCount(void) { return host_tick; }
static int xTaskGetSchedulerState(void) { return host_scheduler; }
static TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name,
    uint32_t size, void *arg, uint32_t priority, StackType_t *stack, StaticTask_t *tcb)
{ (void)fn; (void)name; (void)size; (void)arg; (void)priority; (void)stack; return tcb; }
static UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 128U; }
static TaskHandle_t xTaskGetIdleTaskHandle(void) { return NULL; }
static BaseType_t xTaskDelayUntil(TickType_t *tick, TickType_t delay)
{ *tick += delay; return 1; }
#endif
