#ifndef APP_UI_H
#define APP_UI_H

#include "app_snapshot.h"

/* 以下函数只由显示任务调用 */

void AppUi_Init(void);

void AppUi_Update(const GatewaySnapshot_t *snapshot, uint32_t now_tick);

void AppUi_NextPage(void);

void AppUi_OlderLog(void);
#endif
