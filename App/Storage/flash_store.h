#ifndef FLASH_STORE_H
#define FLASH_STORE_H
#include <stdint.h>
#include "app_snapshot.h"
typedef struct
{
    uint32_t magic, number, boot, tick;
    int32_t temperature, humidity;
    uint32_t type;
    char text[24];
    uint32_t crc, commit, sent;
} StoreRecord;
/* 0成功；负值失败。缓存满时拒绝新数据，不擦掉未上报的数据。 */
int Store_Init(void);
int Store_Log(const char *text);
int Store_LogLevel(uint8_t level, const char *text);
int Store_LogReadLevel(uint32_t offset, uint8_t min_level, StoreRecord *out);
int Store_LogRead(uint32_t newest_offset, StoreRecord *out);
int Store_SaveSample(const GatewaySnapshot_t *sample);
int Store_Peek(StoreRecord *out);
int Store_Ack(uint32_t number);
uint32_t Store_Pending(void);
uint32_t Store_Boot(void);
#endif
