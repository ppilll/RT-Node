#include "FreeRTOS.h"
TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name,
    uint32_t words, void *arg, uint32_t priority, StackType_t *stack, StaticTask_t *tcb);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskDelayUntil(TickType_t *last, TickType_t period);
