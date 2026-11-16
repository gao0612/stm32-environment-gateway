#ifndef W25Q64_H
#define W25Q64_H
#include <stdint.h>
#include <stddef.h>
#define FLASH_BYTES (8U * 1024U * 1024U)
#define FLASH_SECTOR_BYTES 4096U
/* 由存储模块统一加锁，其他任务不要直接调用底层。0成功，-1失败。 */
int Flash_Init(void);
int Flash_Read(uint32_t address, void *data, size_t size);
int Flash_Write(uint32_t address, const void *data, size_t size);
int Flash_Erase(uint32_t address);
#endif
