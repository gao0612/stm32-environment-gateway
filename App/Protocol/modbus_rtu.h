#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>

/* data指向至少length个有效字节；不读取UART、不调用RTOS。 */
uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length);

/* 构造03单播请求；成功返回8，参数/容量错误返回0。frame至少有capacity字节。 */
uint16_t Modbus_BuildRead03(uint8_t slave_address, uint16_t start_address, uint16_t quantity,
                            uint8_t *frame, uint16_t capacity);

/* 仅校验候选报文的CRC，正确返回1，否则返回0；不负责分帧或匹配请求。 */
uint8_t Modbus_CheckCRC(const uint8_t *frame, uint16_t length);

#endif
