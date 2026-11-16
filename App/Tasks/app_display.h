#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "app_snapshot.h"

/* 调度器启动前创建显示队列 */
void AppDisplay_Init(void);

/* 由gatewayTask调用，复制最新快照，不等待队列空位 */
osStatus_t AppDisplay_Publish(const GatewaySnapshot_t *snapshot);

/* 由按键任务调用，非阻塞地请求切换页面 */
osStatus_t AppDisplay_RequestNextPage(void);

osStatus_t AppDisplay_RequestOlderLog(void);
#endif
