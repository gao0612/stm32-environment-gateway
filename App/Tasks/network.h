#ifndef NETWORK_H
#define NETWORK_H
#include "app_snapshot.h"
typedef struct
{
    uint32_t online, chip_ok, link_up, reconnects, uploaded;
    uint32_t queue_drops, log_drops, store_errors, pending, storage_ok, protocol_errors;
    uint32_t ram_pending, retry_ms, reason, live_uploaded, flash_saved, state;
    uint32_t connack_code, min_heap;
} NetworkInfo;
enum
{
    NET_REASON_NONE,
    NET_REASON_CHIP,
    NET_REASON_CABLE,
    NET_REASON_TCP,
    NET_REASON_AUTH,
    NET_REASON_PROTOCOL,
    NET_REASON_ACK,
    NET_REASON_PING,
    NET_REASON_STORAGE
};
enum
{
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};
void Network_InitQueue(void);
osStatus_t Network_Publish(const GatewaySnapshot_t *sample);
uint32_t Network_IsOnline(void);
void Network_GetInfo(NetworkInfo *out);
void Network_Log(const char *text);
void Network_LogLevel(uint8_t level, const char *text);
#endif
