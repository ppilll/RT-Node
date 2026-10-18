#ifndef RT_NODE_UART_APP_H
#define RT_NODE_UART_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char last_command[16]; /* Complete parsed line, truncated/NUL terminated. */
    uint32_t rx_bytes, ovf_count, last_command_tick;
    bool valid;
} UartDisplaySnapshot;
/* Published by UartTask; the getter never examines the DMA ring. */
bool UartApp_GetDisplaySnapshot(UartDisplaySnapshot *out);

/* Main only, after the existing module initializers and before scheduler start. */
bool UartApp_Init(void);

#endif
