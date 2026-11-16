#include "wizchip_conf.h"
#include "w5500_port.h"
#include "spi.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"

void W5500_PortReset(void)
{
    /* 复位期间不选中SPI设备 */
    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

    /* RST低电平有效 */
    HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_RESET);

    osDelay(pdMS_TO_TICKS(2U));

    HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_SET);

    /* 给芯片启动留出时间 */
    osDelay(pdMS_TO_TICKS(50U));
}

HAL_StatusTypeDef W5500_PortReadCommonU8(uint16_t address, uint8_t *value)
{
    uint8_t tx[4] = {
        (uint8_t)(address >> 8U), /* 地址高字节 */
        (uint8_t)address,         /* 地址低字节 */
        0x00U,                    /* 公共寄存器、读、VDM */
        0x00U                     /* 提供读取数据需要的时钟 */
    };

    uint8_t rx[4] = {0};
    HAL_StatusTypeDef status;

    if (value == NULL)
    {
        return HAL_ERROR;
    }

    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);

    status = HAL_SPI_TransmitReceive(&hspi1,     /* 使用SPI1 */
                                     tx,         /* 要发送的4个字节 */
                                     rx,         /* 同时接收4个字节 */
                                     sizeof(tx), /* 传输数量 */
                                     100U);      /* HAL超时，单位毫秒 */

    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

    if (status == HAL_OK)
    {
        *value = rx[3];
    }

    return status;
}

/* 在 w5500_port.c 顶部加入 #include "wizchip_conf.h"。 */

/* 本工程仅由 networkTask 访问 SPI1 和这些回调。 */
static HAL_StatusTypeDef port_io_status = HAL_OK;

void W5500_PortClearStatus(void)
{
    port_io_status = HAL_OK;
}

HAL_StatusTypeDef W5500_PortGetStatus(void)
{
    return port_io_status;
}

static void Port_Select(void)
{
    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
}

static void Port_Deselect(void)
{
    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);
}

static uint8_t Port_Exchange(uint8_t tx)
{
    uint8_t rx = 0U;

    /* 保存本批操作的第一个错误，直到任务主动清除。 */
    if (port_io_status == HAL_OK)
    {
        port_io_status = HAL_SPI_TransmitReceive(&hspi1, /* SPI1 */
                                                 &tx,    /* 发送一个字节 */
                                                 &rx,    /* 同时接收一个字节 */
                                                 1U,     /* 字节数 */
                                                 100U);  /* HAL 超时，毫秒 */
    }
    return (port_io_status == HAL_OK) ? rx : 0U;
}

static uint8_t Port_ReadByte(void)
{
    return Port_Exchange(0x00U); /* 发 dummy，产生读取所需时钟。 */
}

static void Port_WriteByte(uint8_t byte)
{
    (void)Port_Exchange(byte); /* 写入时丢弃同时收到的数据。 */
}

void W5500_PortRegister(void)
{
    reg_wizchip_cs_cbfunc(Port_Select, Port_Deselect);
    reg_wizchip_spi_cbfunc(Port_ReadByte, Port_WriteByte);
    /* 调试不注册 burst 回调，库使用这些字节回调完成寄存器/缓冲区操作。 */
}
