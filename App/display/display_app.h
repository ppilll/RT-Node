#ifndef RT_NODE_DISPLAY_APP_H
#define RT_NODE_DISPLAY_APP_H
#include <stdbool.h>
/* Static software/task creation only. DisplayTask owns I2C1 and KEY1 PE3. */
bool DisplayApp_Init(void);
#endif
