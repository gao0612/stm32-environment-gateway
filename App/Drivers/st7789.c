#include "st7789.h"
#include "spi.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"

#define LCD_SPI_TIMEOUT_MS 100U

#define LCD_DMA_TIMEOUT_MS 100U

/* DMA完成或出错时，用来唤醒显示任务 */
static osSemaphoreId_t lcd_dma_done;

/* 当前是否有一次DMA发送正在等待结果 */
static volatile uint8_t lcd_dma_active;

/* 本次DMA发送结果 */
static volatile HAL_StatusTypeDef lcd_dma_result;

/* 用于确认DMA完成回调确实被执行 */
static volatile uint32_t lcd_dma_complete_count;

/* 启动后尚未被任务收尾，包括“硬件已完成但尚未取结果” */
static uint8_t lcd_dma_pending;

/* 本次DMA启动时的RTOS Tick */
static uint32_t lcd_dma_start_tick;

/*
 * 最大一行：240像素×2字节。
 * FillRect和DrawRGB565复用；运行期只由显示任务调用。
 */
static uint8_t lcd_line_buffer[ST7789_WIDTH * 2U];

/* 发送一条命令，以及这条命令需要的参数 */
static HAL_StatusTypeDef LCD_WriteCommand(uint8_t command,     /* 命令编号 */
                                          const uint8_t *data, /* 参数数组；无参数时可以为NULL */
                                          uint16_t length)     /* 参数字节数；无参数时为0 */
{
    HAL_StatusTypeDef status;

    /* 选中屏幕，切换为命令模式 */
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);

    status = HAL_SPI_Transmit(&hspi3,              /* 使用SPI3 */
                              &command,            /* 命令字节的地址 */
                              1U,                  /* 发送1个字节 */
                              LCD_SPI_TIMEOUT_MS); /* 超时单位是毫秒 */

    if ((status == HAL_OK) && (length != 0U))
    {
        /* 命令发完，切换为参数数据模式 */
        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

        status = HAL_SPI_Transmit(&hspi3, data, /* 参数数组 */
                                  length,       /* 参数字节数 */
                                  LCD_SPI_TIMEOUT_MS);
    }

    /* 成功或失败，都释放片选 */
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

    return status;
}

/* 调试只由FillScreen传入有效的全屏坐标 */
static HAL_StatusTypeDef LCD_SetWindow(uint16_t x0, uint16_t y0, /* 起点 */
                                       uint16_t x1, uint16_t y1) /* 终点，包含该坐标 */
{
    uint8_t columns[4] = {(uint8_t)(x0 >> 8U), (uint8_t)x0, (uint8_t)(x1 >> 8U), (uint8_t)x1};

    uint8_t rows[4] = {(uint8_t)(y0 >> 8U), (uint8_t)y0, (uint8_t)(y1 >> 8U), (uint8_t)y1};

    HAL_StatusTypeDef status;

    /* 0x2A：设置列范围，也就是X方向 */
    status = LCD_WriteCommand(0x2AU, columns, sizeof(columns));

    if (status != HAL_OK)
    {
        return status;
    }

    /* 0x2B：设置行范围，也就是Y方向 */
    status = LCD_WriteCommand(0x2BU, rows, sizeof(rows));

    if (status != HAL_OK)
    {
        return status;
    }

    /* 0x2C：后续数据开始写入显示显存 */
    return LCD_WriteCommand(0x2CU, NULL, 0U);
}

static uint8_t LCD_RectIsValid(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    return (uint8_t)((width != 0U) && (height != 0U) && (x < ST7789_WIDTH) && (y < ST7789_HEIGHT) &&
                     ((uint32_t)x + width <= ST7789_WIDTH) &&
                     ((uint32_t)y + height <= ST7789_HEIGHT));
}

HAL_StatusTypeDef ST7789_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  uint16_t color)
{
    uint16_t column;
    uint16_t row;
    uint16_t row_bytes;
    HAL_StatusTypeDef status;

    if (LCD_RectIsValid(x, y, width, height) == 0U)
    {
        return HAL_ERROR;
    }

    /* 一行需要发送的字节数 */
    row_bytes = (uint16_t)(width * 2U);

    /* 准备一行相同颜色 */
    for (column = 0U; column < width; column++)
    {
        lcd_line_buffer[column * 2U] = (uint8_t)(color >> 8U);

        lcd_line_buffer[column * 2U + 1U] = (uint8_t)color;
    }

    status = LCD_SetWindow(x, y,                         /* 左上角 */
                           (uint16_t)(x + width - 1U),   /* 结束X */
                           (uint16_t)(y + height - 1U)); /* 结束Y */

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

    for (row = 0U; row < height; row++)
    {
        status = HAL_SPI_Transmit(&hspi3,          /* 使用SPI3 */
                                  lcd_line_buffer, /* 相同颜色的一行 */
                                  row_bytes,       /* 只发width×2字节 */
                                  LCD_SPI_TIMEOUT_MS);

        if (status != HAL_OK)
        {
            break;
        }
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

    return status;
}

HAL_StatusTypeDef ST7789_FillScreen(uint16_t color)
{
    return ST7789_FillRect(0U, 0U,        /* 左上角 */
                           ST7789_WIDTH,  /* 整屏宽度 */
                           ST7789_HEIGHT, /* 整屏高度 */
                           color);
}

HAL_StatusTypeDef ST7789_DrawRGB565(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                    const uint16_t *pixels, uint32_t pixel_count)
{
    uint16_t column;
    uint16_t row;
    uint16_t row_bytes;
    uint16_t color;
    uint32_t pixel_index = 0U;
    HAL_StatusTypeDef status;

    if ((LCD_RectIsValid(x, y, width, height) == 0U) || (pixels == NULL) ||
        (pixel_count < (uint32_t)width * height))
    {
        return HAL_ERROR;
    }

    row_bytes = (uint16_t)(width * 2U);

    status = LCD_SetWindow(x, y, (uint16_t)(x + width - 1U), (uint16_t)(y + height - 1U));

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

    for (row = 0U; row < height; row++)
    {
        /* 每一行都从源数组读取新的像素 */
        for (column = 0U; column < width; column++)
        {
            color = pixels[pixel_index++];

            lcd_line_buffer[column * 2U] = (uint8_t)(color >> 8U);

            lcd_line_buffer[column * 2U + 1U] = (uint8_t)color;
        }

        status = HAL_SPI_Transmit(&hspi3, lcd_line_buffer, /* 当前这一行的像素 */
                                  row_bytes, LCD_SPI_TIMEOUT_MS);

        if (status != HAL_OK)
        {
            break;
        }
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

    return status;
}

void ST7789_DmaInit(void)
{
    configASSERT(lcd_dma_done == NULL);

    lcd_dma_done = osSemaphoreNew(1U, /* 最多保存一个完成通知 */
                                  0U, /* 初始没有完成通知 */
                                  NULL);

    configASSERT(lcd_dma_done != NULL);
}

static void LCD_DmaFinish(HAL_StatusTypeDef status)
{
    lcd_dma_active = 0U;

    if (status != HAL_OK)
    {
        /* 失败时先停止硬件，避免继续读取旧缓冲区 */
        if (HAL_SPI_Abort(&hspi3) != HAL_OK)
        {
            Error_Handler();
        }

        if (((hspi3.hdmatx->Instance->CR & DMA_SxCR_EN) != 0U) ||
            (HAL_DMA_GetState(hspi3.hdmatx) != HAL_DMA_STATE_READY) ||
            ((hspi3.Instance->CR2 & SPI_CR2_TXDMAEN) != 0U))
        {
            Error_Handler();
        }
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

    lcd_dma_pending = 0U;
}

HAL_StatusTypeDef ST7789_Init(void)
{
    static const struct
    {
        uint8_t command;
        uint8_t length;
        uint8_t data[14];
    } init_commands[] = {
        {0x36U, 1U, {0x00U}}, /* 显存方向 */
        {0x3AU, 1U, {0x05U}}, /* SPI接口使用RGB565 */

        {0xB2U, 5U, {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U}},
        {0xB7U, 1U, {0x35U}},
        {0xBBU, 1U, {0x19U}},
        {0xC0U, 1U, {0x2CU}},
        {0xC2U, 1U, {0x01U}},
        {0xC3U, 1U, {0x12U}},
        {0xC4U, 1U, {0x20U}},
        {0xC6U, 1U, {0x0FU}},
        {0xD0U, 2U, {0xA4U, 0xA1U}},

        {0xE0U,
         14U,
         {0xD0U, 0x04U, 0x0DU, 0x11U, 0x13U, 0x2BU, 0x3FU, 0x54U, 0x4CU, 0x18U, 0x0DU, 0x0BU, 0x1FU,
          0x23U}},

        {0xE1U,
         14U,
         {0xD0U, 0x04U, 0x0CU, 0x11U, 0x13U, 0x2CU, 0x3FU, 0x44U, 0x51U, 0x2FU, 0x1FU, 0x1FU, 0x20U,
          0x23U}},

        {0x21U, 0U, {0U}} /* 配套IPS屏使用显示反转 */
    };

    uint32_t index;
    HAL_StatusTypeDef status;

    /* 初始化过程中关闭背光 */
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

    HAL_Delay(20U);

    /* 没有独立RST控制引脚，使用软件复位 */
    status = LCD_WriteCommand(0x01U, NULL, 0U);

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(150U);

    /* 依次发送参数表 */
    for (index = 0U; index < sizeof(init_commands) / sizeof(init_commands[0]); index++)
    {
        status = LCD_WriteCommand(init_commands[index].command, /* 当前命令 */
                                  init_commands[index].data,    /* 当前参数数组 */
                                  init_commands[index].length); /* 有效参数个数 */

        if (status != HAL_OK)
        {
            return status;
        }
    }

    /* 退出休眠 */
    status = LCD_WriteCommand(0x11U, NULL, 0U);

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(120U);

    /* 开启显示 */
    status = LCD_WriteCommand(0x29U, NULL, 0U);

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(20U);

    /* 先清成黑色，再打开背光 */
    status = ST7789_FillScreen(ST7789_BLACK);

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);

    return HAL_OK;
}

HAL_StatusTypeDef ST7789_StartRGB565_DMA(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                         const uint8_t *bytes, uint16_t byte_count)
{
    HAL_StatusTypeDef status;

    if ((LCD_RectIsValid(x, y, width, height) == 0U) || (bytes == NULL) ||
        ((uint32_t)byte_count != (uint32_t)width * height * 2U) || (lcd_dma_done == NULL) ||
        (osKernelGetState() != osKernelRunning) || (__get_IPSR() != 0U) || (hspi3.hdmatx == NULL))
    {
        return HAL_ERROR;
    }

    if ((lcd_dma_pending != 0U) || (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY) ||
        (HAL_DMA_GetState(hspi3.hdmatx) != HAL_DMA_STATE_READY))
    {
        return HAL_BUSY;
    }

    /* 清除上一轮可能残留的完成通知 */
    while (osSemaphoreAcquire(lcd_dma_done, 0U) == osOK)
    {
    }

    status = LCD_SetWindow(x, y, (uint16_t)(x + width - 1U), (uint16_t)(y + height - 1U));

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

    lcd_dma_result = HAL_BUSY;
    lcd_dma_pending = 1U;
    lcd_dma_active = 1U;

    lcd_dma_start_tick = osKernelGetTickCount();

    status = HAL_SPI_Transmit_DMA(&hspi3, bytes, byte_count);

    if (status == HAL_OK)
    {
        __HAL_DMA_DISABLE_IT(hspi3.hdmatx, DMA_IT_HT);
    }
    else
    {
        LCD_DmaFinish(status);
    }

    return status;
}

HAL_StatusTypeDef ST7789_WaitDMA(void)
{
    osStatus_t wait_status;
    HAL_StatusTypeDef status;

    uint32_t elapsed_ticks;

    const uint32_t timeout_ticks = pdMS_TO_TICKS(LCD_DMA_TIMEOUT_MS);

    configASSERT(__get_IPSR() == 0U);
    configASSERT(osKernelGetState() == osKernelRunning);

    if (lcd_dma_pending == 0U)
    {
        return HAL_ERROR;
    }

    /*
     * LVGL绘制下一块期间，DMA可能已经完成。
     * 先尝试立即取走完成通知。
     */
    wait_status = osSemaphoreAcquire(lcd_dma_done, 0U);

    if (wait_status == osErrorResource)
    {
        elapsed_ticks = (uint32_t)(osKernelGetTickCount() - lcd_dma_start_tick);

        if (elapsed_ticks >= timeout_ticks)
        {
            wait_status = osErrorTimeout;
        }
        else
        {
            wait_status = osSemaphoreAcquire(lcd_dma_done, timeout_ticks - elapsed_ticks);
        }
    }

    status = (wait_status == osOK) ? lcd_dma_result
                                   : ((wait_status == osErrorTimeout) ? HAL_TIMEOUT : HAL_ERROR);

    LCD_DmaFinish(status);

    return status;
}

HAL_StatusTypeDef ST7789_DrawRGB565_DMA(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                        const uint8_t *bytes, uint16_t byte_count)
{
    HAL_StatusTypeDef status;

    status = ST7789_StartRGB565_DMA(x, y, width, height, bytes, byte_count);

    return (status == HAL_OK) ? ST7789_WaitDMA() : status;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((hspi->Instance == SPI3) && (lcd_dma_active != 0U))
    {
        lcd_dma_result = HAL_OK;
        lcd_dma_active = 0U;

        lcd_dma_complete_count++;

        (void)osSemaphoreRelease(lcd_dma_done);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if ((hspi->Instance == SPI3) && (lcd_dma_active != 0U))
    {
        lcd_dma_result = HAL_ERROR;
        lcd_dma_active = 0U;

        (void)osSemaphoreRelease(lcd_dma_done);
    }
}
