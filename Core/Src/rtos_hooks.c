#include "FreeRTOS.h"
#include "task.h"
#include "p1a_safety.h"

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name);

void AppFatal_Assert(const char *file, int line)
{
    /* No formatting, HAL delay, RTOS API, or recovery on the fatal path. */
    (void)file;
    (void)line;
    P1a_FatalError(); /* Hardware first, latch, disable IRQs, never return. */
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* task/name may be damaged: never dereference them. */
    (void)task;
    (void)task_name;
    P1a_FatalError();
}
