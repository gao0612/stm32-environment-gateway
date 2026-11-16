#include "app_snapshot.h"
#include "FreeRTOS.h"
#include "main.h"
#include <stddef.h>

static osMessageQueueId_t snapshotQueueHandle;

void AppSnapshot_Init(void)
{
    snapshotQueueHandle =
        osMessageQueueNew(1U,                        /* 最多保存1份快照 */
                          sizeof(GatewaySnapshot_t), /* 每份消息的大小 整个结构体拷贝*/
                          NULL);                     /* 默认属性 */

    configASSERT(snapshotQueueHandle != NULL);
}

/*
 * 单生产者：只有UART任务调用。
 * 只能从任务上下文调用。
 */
osStatus_t AppSnapshot_Publish(const GatewaySnapshot_t *snapshot)
{
    osStatus_t status;
    GatewaySnapshot_t discarded;

    if (snapshot == NULL)
    {
        return osErrorParameter;
    }

    status = osMessageQueuePut(snapshotQueueHandle, /* 向哪个队列发布 */
                               snapshot,            /* 待复制的结构体地址 */
                               0U,                  /* 消息优先级 */
                               0U);                 /* 不等待空位 */

    if (status != osErrorResource)
    {
        return status;
    }

    /* 队列满：尝试取走旧快照 */
    status = osMessageQueueGet(snapshotQueueHandle, &discarded, /* 接收被丢弃的旧快照 */
                               NULL,                            /* 不读取消息优先级 */
                               0U);                             /* 不等待 */

    if ((status != osOK) && (status != osErrorResource))
    {
        return status;
    }

    return osMessageQueuePut(snapshotQueueHandle, snapshot, 0U, 0U);
}

osStatus_t AppSnapshot_Wait(GatewaySnapshot_t *snapshot, uint32_t timeout_ticks)
{
    if (snapshot == NULL)
    {
        return osErrorParameter;
    }

    return osMessageQueueGet(snapshotQueueHandle, snapshot, /* 把消息复制到调用者的结构体 */
                             NULL, timeout_ticks);
}
