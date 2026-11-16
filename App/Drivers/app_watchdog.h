#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include "stm32f4xx_hal.h"

void AppWatchdog_CaptureReset(void);
HAL_StatusTypeDef AppWatchdog_Start(void);
void AppWatchdog_Feed(void);

uint32_t AppWatchdog_ResetFlags(void);
#endif
