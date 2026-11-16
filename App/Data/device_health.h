#ifndef DEVICE_HEALTH_H
#define DEVICE_HEALTH_H

#include <stdint.h>

typedef enum
{
    DEVICE_UNKNOWN = 0,
    DEVICE_ONLINE,
    DEVICE_OFFLINE
} DeviceState_t;

typedef struct
{
    DeviceState_t state;
    uint32_t consecutive_failures;
    uint32_t last_success_tick;
    uint8_t has_sample;
} DeviceHealth_t;

/* 初始化状态；health必须指向一个有效的结构体对象 */
void DeviceHealth_Init(DeviceHealth_t *health);

/* 每轮采集结束后调用一次，success非0表示本次成功 */
void DeviceHealth_Record(DeviceHealth_t *health, uint8_t success, uint32_t now_tick);

/* 新鲜返回1，否则返回0；时间参数的单位都是Tick */
uint8_t DeviceHealth_IsFresh(const DeviceHealth_t *health, uint32_t now_tick,
                             uint32_t max_age_ticks);

#endif
