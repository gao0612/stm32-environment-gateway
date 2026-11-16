#ifndef APP_TASKS_H
#define APP_TASKS_H
#include "cmsis_os.h"
/* Called once before starting the scheduler. */
void AppButtonTask_Create(void);
void AppUartTask_Create(void);
void AppGatewayTask_Create(void);
void AppDisplayTask_Create(void);
void AppNetworkTask_Create(void);
#endif
