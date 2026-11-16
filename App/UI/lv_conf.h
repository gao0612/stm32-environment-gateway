#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 使用RGB565：每个像素16位，也就是2字节 */
#define LV_COLOR_DEPTH 16

/*
 * 不交换RGB565的两个字节。
 * 我们的ST7789驱动已经负责先发送高字节。
 */
#define LV_COLOR_16_SWAP 0

/* 使用LVGL自己的内存池 */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)

/* LVGL直接读取HAL的毫秒时间 */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "stm32f4xx_hal.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (HAL_GetTick())

/* 默认约每30ms检查一次显示刷新 */
#define LV_DISP_DEF_REFR_PERIOD 30

/* 启用字体，默认使用20号字体 */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif
