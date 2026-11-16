#include "clock_us.h"
#include "main.h"
#include "tim.h"

void AppTime_Init(void)
{
    /*
     * 调试器暂停CPU时，同时暂停TIM2。
     * 方便断点调试；正常运行时不影响计数。
     */
    __HAL_DBGMCU_FREEZE_TIM2();

    /* 只在启动阶段清零，业务运行过程中不要随意清零 */
    __HAL_TIM_SET_COUNTER(&htim2, /* 操作TIM2 */
                          0U);    /* 起始计数值 */

    /*
     * 启动计数器，不开启更新中断。
     * 1MHz是硬件计数频率，不是每秒进入100万次中断。
     */
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
    {
        Error_Handler();
    }
}

uint32_t AppTime_NowUs(void)
{
    /* 读取TIM2当前的CNT计数寄存器 */
    return __HAL_TIM_GET_COUNTER(&htim2);
}