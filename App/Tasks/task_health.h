#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include <stdint.h>

typedef enum
{
    APP_HEALTH_UART = 0,

    APP_HEALTH_GATEWAY = 1,

    APP_HEALTH_DISPLAY = 2,

    APP_HEALTH_NETWORK = 3,

    APP_HEALTH_COUNT = 4

} AppHealthTaskId_t;

void AppHealth_Init(uint32_t network_probe_period_ms);

void AppHealth_Report(AppHealthTaskId_t id);

void AppHealthTask_Create(void);

#endif
