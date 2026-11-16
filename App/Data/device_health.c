#include "device_health.h"

#define OFFLINE_FAILURE_THRESHOLD 3U

void DeviceHealth_Init(DeviceHealth_t *health)
{
    health->state = DEVICE_UNKNOWN;
    health->consecutive_failures = 0U;
    health->last_success_tick = 0U;
    health->has_sample = 0U;
}

void DeviceHealth_Record(DeviceHealth_t *health, uint8_t success, uint32_t now_tick)
{
    if (success != 0U)
    {
        health->state = DEVICE_ONLINE;
        health->consecutive_failures = 0U;
        health->last_success_tick = now_tick;
        health->has_sample = 1U;
    }
    else
    {
        /* 达到门限后保持3，不让计数一直累加、最终回绕 */
        if (health->consecutive_failures < OFFLINE_FAILURE_THRESHOLD)
        {
            health->consecutive_failures++;
        }

        if (health->consecutive_failures >= OFFLINE_FAILURE_THRESHOLD)
        {
            health->state = DEVICE_OFFLINE;
        }

        /* 失败时不修改上次成功时间，也不清除历史采样 */
    }
}

uint8_t DeviceHealth_IsFresh(const DeviceHealth_t *health, uint32_t now_tick,
                             uint32_t max_age_ticks)
{
    if (health->has_sample == 0U)
    {
        return 0U;
    }

    return ((uint32_t)(now_tick - health->last_success_tick) < max_age_ticks) ? 1U : 0U;
}
