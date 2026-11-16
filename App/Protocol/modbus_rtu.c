/* Modbus RTU纯协议算法：寄存器字段高字节在前，CRC低字节在前。 */
#include "modbus_rtu.h"
#include "stddef.h"

uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        /* 把当前字节异或到CRC的低8位 */
        crc ^= (uint16_t)data[index];

        /* 每个字节处理8次 */
        for (bit = 0U; bit < 8U; bit++)
        {
            /* 判断右移之前的最低位 */
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }

    return crc;
}

uint16_t Modbus_BuildRead03(uint8_t slave_address, uint16_t start_address, uint16_t quantity,
                            uint8_t *frame, uint16_t capacity)
{
    uint16_t crc;

    /* 先检查目标数组，避免越界写入 */
    if ((frame == NULL) || (capacity < 8U))
    {
        return 0U;
    }

    /*
     * 这里构造单播读取请求。
     * 地址0是广播地址，不用于这种读取请求。
     */
    if ((slave_address < 1U) || (slave_address > 247U) || (quantity < 1U) || (quantity > 125U))
    {
        return 0U;
    }

    /* 防止请求的寄存器范围超过0xFFFF */
    if (((uint32_t)start_address + quantity) > 65536UL)
    {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = 0x03U;

    /* 起始地址：高字节在前 */
    frame[2] = (uint8_t)(start_address >> 8U);
    frame[3] = (uint8_t)(start_address & 0x00FFU);

    /* 读取数量：高字节在前 */
    frame[4] = (uint8_t)(quantity >> 8U);
    frame[5] = (uint8_t)(quantity & 0x00FFU);

    crc = Modbus_CRC16(frame, /* 从数组开头计算 */
                       6U);   /* 只计算前6字节，不包含CRC自身 */

    /* CRC：低字节在前 */
    frame[6] = (uint8_t)(crc & 0x00FFU);
    frame[7] = (uint8_t)(crc >> 8U);

    return 8U;
}

uint8_t Modbus_CheckCRC(const uint8_t *frame, uint16_t length)
{
    uint16_t calculated_crc;
    uint16_t received_crc;

    /*
     * 至少包含地址、功能码、两个CRC字节。
     * Modbus RTU完整报文最大256字节。
     */
    if ((frame == NULL) || (length < 4U) || (length > 256U))
    {
        return 0U;
    }

    calculated_crc = Modbus_CRC16(frame,                    /* 报文开头 */
                                  (uint16_t)(length - 2U)); /* 排除末尾两个CRC字节 */

    /*
     * 倒数第2字节是CRC低字节；
     * 最后1字节是CRC高字节。
     */
    received_crc = (uint16_t)((uint16_t)frame[length - 2U] | ((uint16_t)frame[length - 1U] << 8U));

    return (calculated_crc == received_crc) ? 1U : 0U;
}
