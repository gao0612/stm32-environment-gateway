#include "w25q64.h"
#include "main.h"
#include "spi.h"
#include "board_pins.h"
#include "cmsis_os.h"

static void Select(int active)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
static int Send(const void *p, uint16_t n)
{
    return HAL_SPI_Transmit(&hspi2, (uint8_t *)p, n, 100) == HAL_OK ? 0 : -1;
}
static int Receive(void *p, uint16_t n)
{
    return HAL_SPI_Receive(&hspi2, p, n, 100) == HAL_OK ? 0 : -1;
}
static void Pause(void)
{
    if (osKernelGetState() == osKernelRunning)
        osDelay(1);
}
static int WaitReady(void)
{
    uint32_t start = HAL_GetTick();
    do
    {
        uint8_t command = 5, status = 0;
        Select(1);
        int ok = Send(&command, 1) || Receive(&status, 1);
        Select(0);
        if (ok)
            return -1;
        if (!(status & 1U))
            return 0;
        Pause();
    } while ((uint32_t)(HAL_GetTick() - start) < 1000U);
    return -1;
}
static int WriteEnable(void)
{
    uint8_t cmd = 6, status = 0;
    Select(1);
    int ok = Send(&cmd, 1);
    Select(0);
    if (ok)
        return -1;
    cmd = 5;
    Select(1);
    ok = Send(&cmd, 1) || Receive(&status, 1);
    Select(0);
    return (!ok && (status & 2U)) ? 0 : -1;
}
static void Address(uint8_t *cmd, uint32_t a)
{
    cmd[1] = (uint8_t)(a >> 16);
    cmd[2] = (uint8_t)(a >> 8);
    cmd[3] = (uint8_t)a;
}
int Flash_Init(void)
{
    /* main调用MX_SPI2_Init；这里不再重复配置GPIO或SPI句柄。 */
    if (hspi2.Instance != SPI2 || WaitReady()) return -1;
    uint8_t cmd = 0x9F, id[3] = {0};
    Select(1);
    int ok = Send(&cmd, 1) || Receive(id, 3);
    Select(0);
    /* Winbond 64Mbit，容量字节为0x17。拒绝错误容量，避免越界擦写。 */
    return (!ok && id[0] == 0xEF && id[2] == 0x17) ? 0 : -1;
}
int Flash_Read(uint32_t a, void *data, size_t n)
{
    uint8_t cmd[4] = {3};
    if (!data || a >= FLASH_BYTES || n > FLASH_BYTES - a || n > 65535U)
        return -1;
    if (WaitReady())
        return -1;
    Address(cmd, a);
    Select(1);
    int ok = Send(cmd, 4) || Receive(data, (uint16_t)n);
    Select(0);
    return ok ? -1 : 0;
}
int Flash_Write(uint32_t a, const void *data, size_t n)
{
    const uint8_t *bytes = data;
    if (!bytes || a >= FLASH_BYTES || n > FLASH_BYTES - a)
        return -1;
    while (n)
    {
        size_t part = 256U - (a % 256U);
        if (part > n)
            part = n;
        if (WaitReady() || WriteEnable())
            return -1;
        uint8_t cmd[4] = {2};
        Address(cmd, a);
        Select(1);
        int ok = Send(cmd, 4) || Send(bytes, (uint16_t)part);
        Select(0);
        if (ok || WaitReady())
            return -1;
        a += (uint32_t)part;
        bytes += part;
        n -= part;
    }
    return 0;
}
int Flash_Erase(uint32_t a)
{
    if (a >= FLASH_BYTES || a % FLASH_SECTOR_BYTES || WaitReady() || WriteEnable())
        return -1;
    uint8_t cmd[4] = {0x20};
    Address(cmd, a);
    Select(1);
    int ok = Send(cmd, 4);
    Select(0);
    return (ok || WaitReady()) ? -1 : 0;
}
