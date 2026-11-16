#include "app_watchdog.h"
#include "iwdg.h"

typedef struct
{
    uint32_t reset_flags;
    uint32_t feed_count;
    uint32_t feed_error_count;
    HAL_StatusTypeDef init_status;
    uint8_t reset_by_iwdg;
    uint8_t started;
} WatchdogDiag_t;


static volatile WatchdogDiag_t watchdog_diag;

void AppWatchdog_CaptureReset(void)
{
#if defined(DEBUG)
    __HAL_DBGMCU_FREEZE_IWDG();
#endif
    watchdog_diag.reset_flags = RCC->CSR;
    watchdog_diag.reset_by_iwdg = (uint8_t)(__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET);
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

HAL_StatusTypeDef AppWatchdog_Start(void)
{
    if (watchdog_diag.started != 0U)
    {
        return HAL_ERROR;
    }
    /* 延后到任务创建完成再启动；参数来自CubeMX生成的iwdg.c。 */
    MX_IWDG_Init();
    watchdog_diag.init_status = HAL_OK;
    watchdog_diag.started = 1U;
    return watchdog_diag.init_status;
}

void AppWatchdog_Feed(void)
{
    if (watchdog_diag.started == 0U)
    {
        watchdog_diag.feed_error_count++;
        return;
    }
    if (HAL_IWDG_Refresh(&hiwdg) == HAL_OK)
    {
        watchdog_diag.feed_count++;
    }
    else
    {
        watchdog_diag.feed_error_count++;
    }
}

uint32_t AppWatchdog_ResetFlags(void)
{
    return watchdog_diag.reset_flags;
}
