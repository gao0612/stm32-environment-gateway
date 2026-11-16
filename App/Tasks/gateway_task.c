#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_tasks.h"
#include "network.h"
#include "app_snapshot.h"
#include "app_display.h"
#include "task_health.h"

static osThreadId_t gatewayTaskHandle;

static const osThreadAttr_t gatewayTask_attributes = {.name = "gatewayTask",
                                                      .stack_size = 256U * 4U, /* 1024字节 */
                                                      .priority = (osPriority_t)osPriorityNormal};

/* 暂时供调试器观察消费者处理结果 */
typedef struct
{
    GatewaySnapshot_t snapshot;

    uint32_t received_count;
    uint32_t display_publish_error_count;
    uint32_t data_age_ticks;

    uint8_t data_fresh;
    uint8_t data_usable;
} GatewayView_t;

static volatile GatewayView_t gateway_view;

static void StartGatewayTask(void *argument)
{
    GatewaySnapshot_t current = {0};
    int last_state = -1;
    osStatus_t status;
    uint32_t now_tick;

    (void)argument;

    for (;;)
    {
        status = AppSnapshot_Wait(&current,             /* 输出收到的快照 */
                                  pdMS_TO_TICKS(500U)); /* 最多等待500ms */

        if (status == osOK)
        {
            gateway_view.snapshot = current;
            gateway_view.received_count++;
            (void)Network_Publish(&current);
            if (last_state != (int)current.health.state)
            {
                Network_LogLevel(current.health.state == DEVICE_ONLINE ? LOG_INFO : LOG_WARN,
                    current.health.state == DEVICE_ONLINE ? "sensor online" : "sensor not online");
                last_state = (int)current.health.state;
            }
        }
        else if (status != osErrorTimeout)
        {
            /* 队列永久存在，其他错误应检查程序配置 */
            Error_Handler();
        }

        /*
         * 即使没收到新消息，也要检查旧数据有没有过期。
         */
        now_tick = osKernelGetTickCount();

        gateway_view.data_age_ticks = current.health.has_sample
                                          ? (uint32_t)(now_tick - current.health.last_success_tick)
                                          : 0U;

        gateway_view.data_fresh = DeviceHealth_IsFresh(&current.health, /* 最近收到的成功时间 */
                                                       now_tick,        /* 消费者自己的当前时间 */
                                                       pdMS_TO_TICKS(2500U)); /* 有效期2.5秒 */

        gateway_view.data_usable =
            (uint8_t)((current.health.state == DEVICE_ONLINE) && (gateway_view.data_fresh != 0U));

        if (AppDisplay_Publish(&current) != osOK) /* 转发本任务保存的完整快照 */
        {
            gateway_view.display_publish_error_count++;
        }

        AppHealth_Report(APP_HEALTH_GATEWAY);
    }
}

void AppGatewayTask_Create(void)
{
    gatewayTaskHandle = osThreadNew(StartGatewayTask,         /* 任务入口 */
                                    NULL,                     /* 启动参数 */
                                    &gatewayTask_attributes); /* 名称、栈、优先级 */

    configASSERT(gatewayTaskHandle != NULL);
}
