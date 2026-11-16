#ifndef APP_LVGL_H
#define APP_LVGL_H

#include "app_snapshot.h"

/* 以下函数只由displayTask调用 */

void AppLvgl_Init(void);

void AppLvgl_Update(const GatewaySnapshot_t *snapshot, uint32_t now_tick);

void AppLvgl_Process(void);

void AppLvgl_NextPage(void);

#endif
