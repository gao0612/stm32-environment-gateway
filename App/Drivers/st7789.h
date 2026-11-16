#ifndef ST7789_H
#define ST7789_H

#include "main.h"
#include <stdint.h>

#define ST7789_WIDTH 240U
#define ST7789_HEIGHT 240U

/* RGB565颜色 */
#define ST7789_BLACK 0x0000U
#define ST7789_RED 0xF800U
#define ST7789_GREEN 0x07E0U
#define ST7789_BLUE 0x001FU
#define ST7789_WHITE 0xFFFFU

/* 初始化屏幕；当前在调度器启动前调用 */
HAL_StatusTypeDef ST7789_Init(void);

/* 用指定RGB565颜色填满屏幕 */
HAL_StatusTypeDef ST7789_FillScreen(uint16_t color);

/* 区域必须完整位于屏幕内；越界返回HAL_ERROR */
HAL_StatusTypeDef ST7789_FillRect(uint16_t x, uint16_t y,          /* 左上角坐标 */
                                  uint16_t width, uint16_t height, /* 宽度、高度 */
                                  uint16_t color);                 /* RGB565颜色 */

/*
 * pixels：逐行连续排列的RGB565像素。
 * pixel_count：数组中可用的像素数量，单位不是字节。
 */
HAL_StatusTypeDef ST7789_DrawRGB565(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                    const uint16_t *pixels, uint32_t pixel_count);

/* 显示任务中调用一次，创建DMA完成信号量 */
void ST7789_DmaInit(void);

/*
 * 只允许显示任务调用。
 *
 * bytes：已经按高字节在前排列的RGB565发送数据。
 * byte_count：字节数量，必须等于width×height×2。
 *
 * 函数启动DMA后阻塞等待。
 * 函数返回时，DMA已经不再使用bytes。
 */
HAL_StatusTypeDef ST7789_DrawRGB565_DMA(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                        const uint8_t *bytes, uint16_t byte_count);

/*
 * 启动成功后立即返回。
 * 返回HAL_OK时，bytes必须保持不变，
 * 直到ST7789_WaitDMA()完成。
 */
HAL_StatusTypeDef ST7789_StartRGB565_DMA(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                         const uint8_t *bytes, uint16_t byte_count);

/*
 * 只由显示任务调用。
 * 等待并收尾本次发送；
 * 返回时DMA已经不再使用发送缓冲区。
 */
HAL_StatusTypeDef ST7789_WaitDMA(void);

#endif
