#ifndef RT_NODE_UART_APP_H
#define RT_NODE_UART_APP_H

#include <stdbool.h>

/* Main only, after the existing module initializers and before scheduler start. */
bool UartApp_Init(void);

#endif
