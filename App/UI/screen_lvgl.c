#include "screen_lvgl.h"
#include "lvgl.h"
#include "st7789.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include "clock_us.h"
#include "screen_ui.h"

#if (LVGL_VERSION_MAJOR != 8) || (LVGL_VERSION_MINOR != 2)
#error "This lesson uses LVGL 8.2.x"
#endif

#if (LV_COLOR_DEPTH != 16) || (LV_COLOR_16_SWAP != 0)
#error "The ST7789 driver expects native RGB565 values"
#endif

/* LVGL需要长期使用，所以这些对象不能放在函数的临时栈中 */
static lv_disp_draw_buf_t draw_buffer;
static lv_disp_drv_t display_driver;

static lv_color_t draw_pixels_a[ST7789_WIDTH * 10U];
static lv_color_t draw_pixels_b[ST7789_WIDTH * 10U];

/*
 * 最大一块：240×10像素，每个像素2字节。
 * 等待DMA结束之前，不覆盖这个数组。
 */
static uint8_t flush_bytes[ST7789_WIDTH * 10U * 2U];

static uint8_t retry_requested;
static uint32_t last_retry_ms;

typedef struct
{
    uint32_t flush_count;
    uint32_t error_count;
    HAL_StatusTypeDef last_status;

    /* 最近一次刷新请求的区域大小，单位像素 */
    uint32_t last_flush_pixels;

    /* 最近一次、历史最长的区域发送耗时 */
    uint32_t last_flush_us;
    uint32_t max_flush_us;

    /* 最近一次、历史最长的LVGL处理耗时 */
    uint32_t last_handler_us;
    uint32_t max_handler_us;

    uint32_t buffer_a_start_count;
    uint32_t buffer_b_start_count;

} LvglMonitor_t;

static volatile LvglMonitor_t lvgl_monitor;

static lv_disp_drv_t *pending_driver;
static uint32_t pending_start_us;
static uint32_t pending_pixels;

static void Display_RecordResult(HAL_StatusTypeDef status, uint32_t pixels, uint32_t start_us)
{
    uint32_t elapsed_us = (uint32_t)(AppTime_NowUs() - start_us);

    lvgl_monitor.last_flush_pixels = pixels;
    lvgl_monitor.last_flush_us = elapsed_us;

    if (elapsed_us > lvgl_monitor.max_flush_us)
    {
        lvgl_monitor.max_flush_us = elapsed_us;
    }

    lvgl_monitor.last_status = status;

    if (status == HAL_OK)
    {
        lvgl_monitor.flush_count++;
    }
    else
    {
        lvgl_monitor.error_count++;
        retry_requested = 1U;
    }
}

static void Display_FinishPending(void)
{
    if (pending_driver != NULL)
    {
        lv_disp_drv_t *driver = pending_driver;

        HAL_StatusTypeDef status = ST7789_WaitDMA();

        Display_RecordResult(status, pending_pixels, pending_start_us);

        pending_driver = NULL;

        /* DMA已经结束，此时才释放LVGL缓冲区 */
        lv_disp_flush_ready(driver);
    }
}

static void Display_WaitFlush(lv_disp_drv_t *driver)
{
    configASSERT(pending_driver == driver);

    Display_FinishPending();
}

static void Display_Flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors)
{
    uint16_t width = (uint16_t)(area->x2 - area->x1 + 1);

    uint16_t height = (uint16_t)(area->y2 - area->y1 + 1);

    uint32_t pixels = (uint32_t)width * height;

    uint32_t start_us = AppTime_NowUs();
    uint32_t index;
    uint16_t color;

    HAL_StatusTypeDef status = HAL_ERROR;

    /* LVGL应先通过wait_cb收尾上一块 */
    configASSERT(pending_driver == NULL);

    if ((pixels != 0U) && (pixels <= sizeof(flush_bytes) / 2U))
    {
        for (index = 0U; index < pixels; index++)
        {
            color = colors[index].full;

            flush_bytes[index * 2U] = (uint8_t)(color >> 8U);

            flush_bytes[index * 2U + 1U] = (uint8_t)color;
        }

        status = ST7789_StartRGB565_DMA((uint16_t)area->x1, (uint16_t)area->y1, width, height,
                                        flush_bytes, (uint16_t)(pixels * 2U));
    }

    if (status == HAL_OK)
    {
        if (colors == draw_pixels_a)
        {
            lvgl_monitor.buffer_a_start_count++;
        }
        else if (colors == draw_pixels_b)
        {
            lvgl_monitor.buffer_b_start_count++;
        }

        pending_driver = driver;
        pending_start_us = start_us;
        pending_pixels = pixels;

        /*
         * 已启动DMA，直接返回。
         * 暂时不调用lv_disp_flush_ready。
         * LVGL接下来可以绘制另一个缓冲区。
         */
        return;
    }

    /* 启动失败，没有正在使用的发送缓冲区 */
    Display_RecordResult(status, pixels, start_us);

    lv_disp_flush_ready(driver);
}

void AppLvgl_Init(void)
{
    ST7789_DmaInit();

    lv_init();

    lv_disp_draw_buf_init(&draw_buffer, draw_pixels_a, draw_pixels_b, ST7789_WIDTH * 10U);

    lv_disp_drv_init(&display_driver);

    display_driver.hor_res = ST7789_WIDTH;
    display_driver.ver_res = ST7789_HEIGHT;
    display_driver.draw_buf = &draw_buffer;
    display_driver.flush_cb = Display_Flush;
    display_driver.wait_cb = Display_WaitFlush;

    if (lv_disp_drv_register(&display_driver) == NULL)
    {
        Error_Handler();
    }

    AppUi_Init();
}

void AppLvgl_Update(const GatewaySnapshot_t *snapshot, uint32_t now_tick)
{
    AppUi_Update(snapshot, now_tick);
}

void AppLvgl_NextPage(void)
{
    AppUi_NextPage();
}

void AppLvgl_Process(void)
{
    uint32_t now_ms = HAL_GetTick();

    uint32_t start_us;
    uint32_t elapsed_us;

    /* 保留原有的SPI失败重绘机制 */
    if ((retry_requested != 0U) && ((uint32_t)(now_ms - last_retry_ms) >= 1000U))
    {
        retry_requested = 0U;
        last_retry_ms = now_ms;

        lv_obj_invalidate(lv_scr_act());
    }

    /* 从这里开始测量一次LVGL处理 */
    start_us = AppTime_NowUs();

    (void)lv_timer_handler();

    Display_FinishPending();

    elapsed_us = (uint32_t)(AppTime_NowUs() - start_us);

    lvgl_monitor.last_handler_us = elapsed_us;

    if (elapsed_us > lvgl_monitor.max_handler_us)
    {
        lvgl_monitor.max_handler_us = elapsed_us;
    }
}
