/* 按键：EXTI回调只通知，任务中消抖、翻转LED并等待释放。 */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_tasks.h"
#include "app_display.h"

#define THREAD_FLAG_BUTTON (1UL << 0U)

static osThreadId_t buttonTaskHandle;

static volatile uint32_t button_command_drop_count;

static const osThreadAttr_t buttonTask_attributes = {
    .name = "buttonTask", .stack_size = 256U * 4U, .priority = (osPriority_t)osPriorityNormal};

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if ((GPIO_Pin == USER_KEY_Pin) && (buttonTaskHandle != NULL))
    {
        (void)osThreadFlagsSet(buttonTaskHandle, // 通知哪个任务
                               THREAD_FLAG_BUTTON);
    }
}

static void StartButtonTask(void *argument)
{
    uint32_t flags;

    (void)argument;

    for (;;)
    {
        flags = osThreadFlagsWait(THREAD_FLAG_BUTTON, osFlagsWaitAny, osWaitForever);

        if ((flags & osFlagsError) != 0U)
        {
            Error_Handler();
        }

        if (HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin) != GPIO_PIN_RESET)
        {
            continue;
        }

        /* 按下消抖 */
        osDelay(pdMS_TO_TICKS(20U));

        if (HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin) != GPIO_PIN_RESET)
        {
            continue;
        }

        HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);

        uint32_t pressed = HAL_GetTick();
        uint8_t long_press = 0;
        while (HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin) == GPIO_PIN_RESET)
        {
            if (!long_press && (uint32_t)(HAL_GetTick() - pressed) >= 800U)
            {
                if (AppDisplay_RequestOlderLog() != osOK)
                    button_command_drop_count++;
                long_press = 1;
            }
            osDelay(pdMS_TO_TICKS(10));
        }
        if (!long_press && AppDisplay_RequestNextPage() != osOK)
            button_command_drop_count++;

        /* 等待稳定释放，长按期间不会重复发送命令 */
        for (;;)
        {
            while (HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin) == GPIO_PIN_RESET)
            {
                osDelay(pdMS_TO_TICKS(10U));
            }

            osDelay(pdMS_TO_TICKS(20U));

            if (HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin) == GPIO_PIN_SET)
            {
                break;
            }
        }
    }
}

void AppButtonTask_Create(void)
{
    /* 调度器启动前创建；句柄只由本模块保存。 */
    buttonTaskHandle = osThreadNew(StartButtonTask,         /* 任务入口 */
                                   NULL,                    /* 启动参数 */
                                   &buttonTask_attributes); /* 名称、栈字节数、优先级 */
    configASSERT(buttonTaskHandle != NULL);
}
