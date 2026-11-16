#ifndef APP_SNAPSHOT_H
#define APP_SNAPSHOT_H

#include "cmsis_os.h"
#include "kth2r.h"
#include "device_health.h"

/* 一份完整消息：数值、状态、轮次、发布时间 */
typedef struct
{
    KTH2R_Data_t data;
    DeviceHealth_t health;

    uint32_t sequence;
    uint32_t publish_tick;
} GatewaySnapshot_t;

void AppSnapshot_Init(void);

/* 只有UART任务发布；不阻塞，满时替换旧快照 */
osStatus_t AppSnapshot_Publish(const GatewaySnapshot_t *snapshot);

/* 消费者等待并取出一份快照 */
osStatus_t AppSnapshot_Wait(GatewaySnapshot_t *snapshot, uint32_t timeout_ticks);

#endif
