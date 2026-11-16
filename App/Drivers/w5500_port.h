#ifndef W5500_PORT_H
#define W5500_PORT_H

#include "main.h"

/* 调度器启动后，只由网络任务调用 */
void W5500_PortReset(void);

/*
 * 读取公共寄存器中的一个字节。
 * 只有读取成功时，才更新*value。
 */
HAL_StatusTypeDef W5500_PortReadCommonU8(uint16_t address, uint8_t *value);

void W5500_PortRegister(void);
void W5500_PortClearStatus(void);
HAL_StatusTypeDef W5500_PortGetStatus(void);

#endif
