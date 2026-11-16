#include "gateway.h"
#include "network.h"
#include "app_tasks.h"
#include "app_params_store.h"
#include "app_params.h"
#include "clock_us.h"
#include "st7789.h"
#include "app_snapshot.h"
#include "app_display.h"
#include "task_health.h"

void Gateway_Init(void)
{
    AppParams_t saved;
    if (AppParamsStore_Load(&saved) == APP_STORE_OK)
        (void)AppParams_Init(&saved);
    else
        (void)AppParams_Init(NULL);
    AppTime_Init();
    if (ST7789_Init() != HAL_OK)
        Error_Handler();
}

void Gateway_StartTasks(void)
{
    AppHealth_Init(AppParams_Get()->network_probe_period_ms);
    AppSnapshot_Init();
    AppDisplay_Init();
    Network_InitQueue();
    AppButtonTask_Create();
    AppUartTask_Create();
    AppGatewayTask_Create();
    AppDisplayTask_Create();
    AppNetworkTask_Create();
    AppHealthTask_Create();
    if (AppWatchdog_Start() != HAL_OK)
        Error_Handler();
}
