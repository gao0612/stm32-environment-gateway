#include "mqtt_config.h"
#include "screen_ui.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_tasks.h"
#include "app_display.h"
#include "screen_lvgl.h"
#include "task_health.h"

#define DISPLAY_COMMAND_NEXT_PAGE 1U

static osMessageQueueId_t displayCommandQueueHandle;

static osMessageQueueId_t displayQueueHandle;
static osThreadId_t displayTaskHandle;

/* 显示任务自启动以来，观察到的最小剩余栈空间 */
static volatile uint32_t display_stack_free_min_bytes;

static const osThreadAttr_t displayTask_attributes = {.name = "displayTask",
                                                      .stack_size = 1536U * 4U, /* 6144字节 */
                                                      .priority =
                                                          (osPriority_t)osPriorityBelowNormal};

void AppDisplay_Init(void)
{
    displayQueueHandle = osMessageQueueNew(1U,                        /* 只保留最新一份消息 */
                                           sizeof(GatewaySnapshot_t), /* 每条消息复制的大小 */
                                           NULL);                     /* 默认属性 */

    configASSERT(displayQueueHandle != NULL);

    displayCommandQueueHandle = osMessageQueueNew(4U,              /* 最多缓存4个命令 */
                                                  sizeof(uint8_t), /* 每个命令1字节 */
                                                  NULL);

    configASSERT(displayCommandQueueHandle != NULL);
}

osStatus_t AppDisplay_Publish(const GatewaySnapshot_t *snapshot)
{
    osStatus_t status;
    GatewaySnapshot_t discarded;

    if (snapshot == NULL)
    {
        return osErrorParameter;
    }

    status = osMessageQueuePut(displayQueueHandle, /* 目标队列 */
                               snapshot,           /* 复制这份快照 */
                               0U,                 /* 消息优先级 */
                               0U);                /* 不等待空位 */

    if (status != osErrorResource)
    {
        return status;
    }

    /* 队列已满，尝试移除旧显示消息 */
    status = osMessageQueueGet(displayQueueHandle, &discarded, /* 接收被丢弃的旧消息 */
                               NULL,                           /* 不读取消息优先级 */
                               0U);                            /* 不等待 */

    if ((status != osOK) && (status != osErrorResource))
    {
        return status;
    }

    return osMessageQueuePut(displayQueueHandle, snapshot, 0U, 0U);
}

osStatus_t AppDisplay_RequestOlderLog(void)
{
    const uint8_t command = 2;
    return osMessageQueuePut(displayCommandQueueHandle, &command, 0, 0);
}
osStatus_t AppDisplay_RequestNextPage(void)
{
    const uint8_t command = DISPLAY_COMMAND_NEXT_PAGE;

    return osMessageQueuePut(displayCommandQueueHandle, &command, 0U, 0U);
}

static uint8_t Display_ProcessCommand(void)
{
    uint8_t command;

    osStatus_t status = osMessageQueueGet(displayCommandQueueHandle, &command, NULL, 0U);

    if (status == osErrorResource)
    {
        return 0U; /* 当前没有命令 */
    }

    if ((status != osOK) || (command != DISPLAY_COMMAND_NEXT_PAGE && command != 2U))
    {
        Error_Handler();
    }

    if (command == 2U)
        AppUi_OlderLog();
    else
        AppLvgl_NextPage();

    return 1U;
}

static void StartDisplayTask(void *argument)
{
    GatewaySnapshot_t current = {0};

    osStatus_t queue_status;
    uint32_t now_tick;
    uint32_t last_ui_tick;
    uint32_t last_stack_check_tick;
    uint8_t page_changed;
    uint32_t last_key = HAL_GetTick();

    (void)argument;

    /* 只初始化一次 */
    AppLvgl_Init();

    last_ui_tick = osKernelGetTickCount();

    last_stack_check_tick = last_ui_tick;

    /* 首次没有数据，先显示等待状态 */
    AppLvgl_Update(&current, last_ui_tick);

    for (;;)
    {
        queue_status = osMessageQueueGet(displayQueueHandle,       /* 显示队列 */
                                         &current,                 /* 本任务保存的快照 */
                                         NULL, pdMS_TO_TICKS(5U)); /* 最多等待5ms */

        if ((queue_status != osOK) && (queue_status != osErrorTimeout))
        {
            Error_Handler();
        }

        page_changed = Display_ProcessCommand();
        if (page_changed)
            last_key = HAL_GetTick();
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin,
                          (uint32_t)(HAL_GetTick() - last_key) < SCREEN_SLEEP_MS ? GPIO_PIN_SET
                                                                                 : GPIO_PIN_RESET);

        now_tick = osKernelGetTickCount();

        /*
         * 有新消息时立即更新对象。
         * 没有新消息时，每100ms重新检查数据是否过期。
         */
        if ((page_changed != 0U) || (queue_status == osOK) ||
            ((uint32_t)(now_tick - last_ui_tick) >= pdMS_TO_TICKS(100U)))
        {
            AppLvgl_Update(&current, now_tick);
            last_ui_tick = now_tick;
        }
        /* 持续处理LVGL的绘图和刷新 */
        AppLvgl_Process();

        /* 每秒查询一次，避免每轮都扫描任务栈 */
        now_tick = osKernelGetTickCount();

        if ((uint32_t)(now_tick - last_stack_check_tick) >= pdMS_TO_TICKS(1000U))
        {
            display_stack_free_min_bytes =
                (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t);

            last_stack_check_tick = now_tick;
        }
        AppHealth_Report(APP_HEALTH_DISPLAY);
    }
}
void AppDisplayTask_Create(void)
{
    displayTaskHandle = osThreadNew(StartDisplayTask,         /* 任务入口 */
                                    NULL,                     /* 启动参数 */
                                    &displayTask_attributes); /* 名称、栈、优先级 */

    configASSERT(displayTaskHandle != NULL);
}
