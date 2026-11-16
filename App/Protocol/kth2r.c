#include "kth2r.h"
#include "modbus_rtu.h"
#include <stddef.h>

uint16_t KTH2R_BuildReadRequest(uint8_t slave, uint8_t *request, uint16_t capacity)
{
    return Modbus_BuildRead03(slave,     /* 从站地址 */
                              0x0300U,   /* 温度寄存器起始地址 */
                              2U,        /* 连续读取温度、湿度两个寄存器 */
                              request,   /* 输出请求的数组 */
                              capacity); /* 输出数组容量 */
}

KTH2R_ParseStatus_t KTH2R_ParseResponse(const uint8_t *frame, uint16_t length, uint8_t slave,
                                        KTH2R_Data_t *data, uint8_t *exception_code)
{
    uint16_t raw_temperature;

    if ((frame == NULL) || (data == NULL) || (exception_code == NULL) || (slave < 1U) ||
        (slave > 247U))
    {
        return KTH2R_PARSE_ARGUMENT_ERROR;
    }

    *exception_code = 0U;

    /* 本次请求：正常响应9字节，异常响应5字节 */
    if ((length != 9U) && (length != 5U))
    {
        return KTH2R_PARSE_LENGTH_ERROR;
    }

    if (Modbus_CheckCRC(frame, length) == 0U)
    {
        return KTH2R_PARSE_CRC_ERROR;
    }

    if (frame[0] != slave)
    {
        return KTH2R_PARSE_ADDRESS_ERROR;
    }

    /* 03请求的异常响应，其功能码为83 */
    if (frame[1] == 0x83U)
    {
        if (length != 5U)
        {
            return KTH2R_PARSE_LENGTH_ERROR;
        }

        *exception_code = frame[2];
        return KTH2R_PARSE_EXCEPTION;
    }

    if (frame[1] != 0x03U)
    {
        return KTH2R_PARSE_FUNCTION_ERROR;
    }

    if ((length != 9U) || (frame[2] != 4U))
    {
        return KTH2R_PARSE_LENGTH_ERROR;
    }

    /* 温度高字节在前，先拼成原始16位值 */
    raw_temperature = (uint16_t)(((uint16_t)frame[3] << 8U) | frame[4]);

    /* 最高位为1时，按16位补码还原负温度 */
    if ((raw_temperature & 0x8000U) != 0U)
    {
        data->temperature_x10 = (int32_t)raw_temperature - 65536L;
    }
    else
    {
        data->temperature_x10 = (int32_t)raw_temperature;
    }

    data->humidity_x10 = (uint16_t)(((uint16_t)frame[5] << 8U) | frame[6]);

    return KTH2R_PARSE_OK;
}

/* 只在候选区非空时调用。 */
static void KTH2R_DropFirst(uint8_t *frame, uint16_t *length)
{
    uint16_t index;

    for (index = 1U; index < *length; index++)
    {
        frame[index - 1U] = frame[index];
    }

    (*length)--;
}

KTH2R_RxResult_t KTH2R_CollectByte(uint8_t slave, uint8_t byte, uint8_t *frame, uint16_t capacity,
                                   uint16_t *length)
{
    uint16_t expected_length;

    if ((frame == NULL) || (length == NULL) || (capacity < 9U) || (slave < 1U) || (slave > 247U))
    {
        return KTH2R_RX_ARGUMENT_ERROR;
    }

    /*
     * 正常响应最多9字节。
     * 上次若已返回READY，应结束本次；
     * 新一轮由调用者把*length清零。
     */
    if (*length >= 9U)
    {
        return KTH2R_RX_ARGUMENT_ERROR;
    }

    /* 把新字节追加到候选区末尾 */
    frame[*length] = byte;
    (*length)++;

    while (*length != 0U)
    {
        /* 起点不是期望的从站地址：排除这个起点 */
        if (frame[0] != slave)
        {
            KTH2R_DropFirst(frame, length);
            continue;
        }

        if (*length < 2U)
        {
            return KTH2R_RX_MORE; /* 有地址，等待功能码 */
        }

        if (frame[1] == 0x83U)
        {
            expected_length = 5U; /* 异常响应 */
        }
        else if (frame[1] == 0x03U)
        {
            if (*length < 3U)
            {
                return KTH2R_RX_MORE; /* 等待数据字节数 */
            }

            /* 当前查询读取两个寄存器，应返回4字节数据 */
            if (frame[2] != 4U)
            {
                KTH2R_DropFirst(frame, length);
                continue;
            }

            expected_length = 9U;
        }
        else
        {
            /* 功能码不匹配，继续寻找后面的潜在起点 */
            KTH2R_DropFirst(frame, length);
            continue;
        }

        if (*length < expected_length)
        {
            return KTH2R_RX_MORE; /* 结构暂时正确，继续等 */
        }

        if (Modbus_CheckCRC(frame,           /* 当前候选起点 */
                            expected_length) /* 正常9字节、异常5字节 */
            != 0U)
        {
            /* 本次只取找到的第一个有效响应 */
            *length = expected_length;
            return KTH2R_RX_READY;
        }

        /* CRC错误：只排除当前起点，保留后面的潜在起点 */
        KTH2R_DropFirst(frame, length);
    }

    return KTH2R_RX_MORE;
}
