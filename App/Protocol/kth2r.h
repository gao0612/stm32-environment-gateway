#ifndef KTH2R_H
#define KTH2R_H

#include <stdint.h>

/* 保留整数形式：283表示28.3℃，584表示58.4%RH */
typedef struct
{
    int32_t temperature_x10;
    uint16_t humidity_x10;
} KTH2R_Data_t;

/* 解析结果：区分具体失败原因 */
typedef enum
{
    KTH2R_PARSE_OK = 0,
    KTH2R_PARSE_ARGUMENT_ERROR,
    KTH2R_PARSE_LENGTH_ERROR,
    KTH2R_PARSE_CRC_ERROR,
    KTH2R_PARSE_ADDRESS_ERROR,
    KTH2R_PARSE_FUNCTION_ERROR,
    KTH2R_PARSE_EXCEPTION
} KTH2R_ParseStatus_t;

/* 成功返回请求长度8；参数不正确返回0 */
uint16_t KTH2R_BuildReadRequest(uint8_t slave, uint8_t *request, uint16_t capacity);

/* 只在解析成功时更新data；异常响应通过exception_code返回异常码 */
KTH2R_ParseStatus_t KTH2R_ParseResponse(const uint8_t *frame,     // 收到的原始RTU帧
                                        uint16_t length,          // 收到多少字节
                                        uint8_t slave,            // 我们刚才查询的从站号，用来校验
                                        KTH2R_Data_t *data,       // 解析结果输出：温度、湿度（×10）
                                        uint8_t *exception_code); // 输出：异常码，无异常填0

/* 当前查询专用：正常响应9字节，异常响应5字节。 */
typedef enum
{
    KTH2R_RX_MORE = 0,      /* 尚未找到有效响应，继续接收 */
    KTH2R_RX_READY,         /* 已找到结构和CRC正确的响应 */
    KTH2R_RX_ARGUMENT_ERROR /* 参数或缓冲区状态不正确 */
} KTH2R_RxResult_t;

/*
 * 一轮接收开始前，把*length置0。
 * 每收到一个字节调用一次。
 * 返回READY后，结束本次组帧。
 */
KTH2R_RxResult_t KTH2R_CollectByte(uint8_t slave,     /* 本次查询的从站地址 */
                                   uint8_t byte,      /* 新收到的一个字节 */
                                   uint8_t *frame,    /* 候选响应缓冲区 */
                                   uint16_t capacity, /* 缓冲区容量，至少9字节 */
                                   uint16_t *length); /* 输入/输出：当前保留的字节数 */

#endif