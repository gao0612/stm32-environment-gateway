#include "retry_delay.h"
#include <stdlib.h>
#include "app_watchdog.h"
#include "app_tasks.h"
#include "network.h"
#include "app_display.h"
#include "app_params.h"
#include "main.h"
#include "w5500_port.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "mqtt_packet.h"
#include "mqtt_config.h"
#include "flash_store.h"
#include "task_health.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* 只有这个任务访问W5500。缓存先落盘，PUBACK后再标记已发送。 */
enum
{
    NET_RETRY,
    NET_TCP,
    NET_CONNACK,
    NET_SUBACK,
    NET_ONLINE
};
static osMessageQueueId_t sample_queue, log_queue;
static uint8_t server[4] = MQTT_SERVER_IP;
static uint8_t tx[512], rx[512];
static size_t rx_size;
static uint32_t since, last_send, last_receive;
static uint16_t packet_id = 1, waiting_id;
static uint32_t waiting_record;
static uint32_t waiting_since;
static uint8_t state;
static uint8_t ping_waiting;
static volatile NetworkInfo network_monitor;
static NetworkInfo published_info;
typedef struct
{
    uint8_t level;
    char text[24];
} LogItem;
static GatewaySnapshot_t ram[16];
static unsigned ram_head, ram_count;
static uint32_t ram_since, last_flush, retry_ms;
static uint8_t waiting_live;
static uint32_t online_since;

void Network_GetInfo(NetworkInfo *out)
{
    if (!out)
        return;
    taskENTER_CRITICAL();
    *out = published_info;
    taskEXIT_CRITICAL();
}
static void PublishInfo(void)
{
    network_monitor.ram_pending = ram_count;
    network_monitor.retry_ms = retry_ms;
    network_monitor.state = state;
    network_monitor.min_heap = (uint32_t)xPortGetMinimumEverFreeHeapSize();
    taskENTER_CRITICAL();
    published_info = network_monitor;
    taskEXIT_CRITICAL();
}
static void PopRam(void)
{
    ram_head = (ram_head + 1U) % 16U;
    ram_count--;
    ram_since = HAL_GetTick();
}

void Network_InitQueue(void)
{
    sample_queue = osMessageQueueNew(16, sizeof(GatewaySnapshot_t), NULL);
    log_queue = osMessageQueueNew(16, sizeof(LogItem), NULL);
    configASSERT(sample_queue != NULL && log_queue != NULL);
}
osStatus_t Network_Publish(const GatewaySnapshot_t *s)
{
    if (!s)
        return osErrorParameter;
    osStatus_t status = osMessageQueuePut(sample_queue, s, 0, 0);
    if (status != osOK)
        network_monitor.queue_drops++;
    return status;
}
void Network_LogLevel(uint8_t level, const char *text)
{
    LogItem item = {0};
    if (!text || !log_queue)
        return;
    item.level = level;
    strncpy(item.text, text, sizeof(item.text) - 1);
    if (osMessageQueuePut(log_queue, &item, 0, 0) != osOK)
        network_monitor.log_drops++;
}
void Network_Log(const char *text)
{
    Network_LogLevel(LOG_INFO, text);
}
uint32_t Network_IsOnline(void)
{
    return network_monitor.online;
}
static uint32_t Now(void)
{
    return HAL_GetTick();
}
static int Send(uint8_t *data, size_t n)
{
    if (!n || n > sizeof(tx))
        return -1;
    uint32_t start = Now();
    size_t done = 0;
    while (done < n && (uint32_t)(Now() - start) < 1000U)
    {
        int32_t count = send(0, data + done, (uint16_t)(n - done));
        if (count < 0)
        {
            network_monitor.reason = NET_REASON_TCP;
            return -1;
        }
        done += (size_t)count;
        if (done < n)
            osDelay(1);
    }
    if (done != n || W5500_PortGetStatus() != HAL_OK)
    {
        network_monitor.reason = NET_REASON_TCP;
        return -1;
    }
    last_send = Now();
    return 0;
}
static void Retry(void)
{
    if (state == NET_ONLINE)
        (void)Store_LogLevel(LOG_WARN, "mqtt offline");
    close(0);
    state = NET_RETRY;
    retry_ms = Retry_Next(retry_ms);
    since = Now();
    rx_size = 0;
    network_monitor.online = 0;
    waiting_id = 0;
    ping_waiting = 0;
}
static int InitChip(void)
{
    uint8_t sizes[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    const AppParams_t *p = AppParams_Get();
    wiz_NetInfo wanted = {0}, actual = {0};
    W5500_PortReset();
    W5500_PortClearStatus();
    if (getVERSIONR() != 4 || wizchip_init(sizes, sizes) != 0)
        return -1;
    memcpy(wanted.mac, p->mac, 6);
    memcpy(wanted.ip, p->ip, 4);
    memcpy(wanted.sn, p->mask, 4);
    memcpy(wanted.gw, p->gateway, 4);
    wanted.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&wanted);
    wizchip_getnetinfo(&actual);
    setRTR(2000);
    setRCR(3); /* 200ms重试间隔，3次；软件仍有总超时。 */
    return W5500_PortGetStatus() == HAL_OK && !memcmp(actual.mac, wanted.mac, 6) &&
                   !memcmp(actual.ip, wanted.ip, 4) && !memcmp(actual.sn, wanted.sn, 4) &&
                   !memcmp(actual.gw, wanted.gw, 4)
               ? 0
               : -1;
}
static int ProcessPacket(const uint8_t *b, size_t n, size_t header)
{
    uint8_t kind = b[0] >> 4;
    const uint8_t *body = b + header;
    size_t count = n - header;
    if (state == NET_CONNACK)
    {
        if (b[0] != 0x20 || count != 2 || body[0] != 0)
            return -1;
        network_monitor.connack_code = body[1];
        if (body[1])
        {
            network_monitor.reason = NET_REASON_AUTH;
            return -1;
        }
        size_t len = Mqtt_Subscribe(tx, sizeof(tx), MQTT_COMMAND_TOPIC, 1);
        if (Send(tx, len))
            return -1;
        state = NET_SUBACK;
        since = Now();
        return 0;
    }
    if (state == NET_SUBACK)
    {
        if (b[0] != 0x90 || count != 3 || body[0] != 0 || body[1] != 1 || body[2] != 0)
            return -1;
        state = NET_ONLINE;
        network_monitor.online = 1;
        network_monitor.reason = NET_REASON_NONE;
        online_since = Now();
        (void)Store_Log("mqtt online");
        return 0;
    }
    if (state != NET_ONLINE)
        return -1;
    if (kind == 4)
    {
        if (b[0] != 0x40 || count != 2)
            return -1;
        uint16_t id = (uint16_t)((body[0] << 8) | body[1]);
        if (waiting_id && id == waiting_id)
        {
            if (!waiting_live && Store_Ack(waiting_record))
            {
                network_monitor.store_errors++;
                network_monitor.reason = NET_REASON_STORAGE;
                return -1;
            }
            if (waiting_live)
            {
                PopRam();
                network_monitor.live_uploaded++;
            }
            waiting_id = 0;
            network_monitor.uploaded++;
        }
    }
    else if (kind == 13)
    {
        if (b[0] != 0xD0 || count)
            return -1;
        ping_waiting = 0;
    }
    else if (kind == 3)
    {
        /* 命令只显示/记录，不允许远程擦除Flash或改写任意内存。 */
        if (count < 2)
            return -1;
        size_t topic_len = ((size_t)body[0] << 8) | body[1], at = 2U + topic_len;
        if (at > count)
            return -1;
        uint8_t qos = (b[0] >> 1) & 3U;
        if (qos > 1)
            return -1;
        uint16_t id = 0;
        if (qos)
        {
            if (at + 2 > count)
                return -1;
            id = (uint16_t)((body[at] << 8) | body[at + 1]);
            at += 2;
            if (!id)
                return -1;
        }
        if (topic_len == strlen(MQTT_COMMAND_TOPIC) &&
            !memcmp(body + 2, MQTT_COMMAND_TOPIC, topic_len))
        {
            if (count - at == 4 && !memcmp(body + at, "next", 4))
                (void)AppDisplay_RequestNextPage();
            (void)Store_Log("mqtt command received");
        }
        if (qos)
        {
            uint8_t ack[] = {0x40, 2, (uint8_t)(id >> 8), (uint8_t)id};
            if (Send(ack, 4))
                return -1;
        }
    }
    else
        return -1;
    return 0;
}
static int ReceivePackets(void)
{
    uint16_t available = getSn_RX_RSR(0);
    if (available)
    {
        if (rx_size == sizeof(rx))
            return -1;
        size_t space = sizeof(rx) - rx_size;
        if (available > space)
            available = (uint16_t)space;
        int32_t n = recv(0, rx + rx_size, available);
        if (n < 0)
            return -1;
        rx_size += (size_t)n;
    }
    while (rx_size)
    {
        size_t total = 0, header = 0;
        int result = Mqtt_FrameSize(rx, rx_size, sizeof(rx), &total, &header);
        if (result < 0)
            return -1;
        if (!result)
            break;
        if (ProcessPacket(rx, total, header))
            return -1;
        last_receive = Now();
        rx_size -= total;
        memmove(rx, rx + total, rx_size);
    }
    return W5500_PortGetStatus() == HAL_OK ? 0 : -1;
}
static int UploadOne(void)
{
    StoreRecord r;
    int result = Store_Peek(&r);
    waiting_live = 0;
    if (result == -2)
    {
        if (!ram_count)
            return 0;
        const GatewaySnapshot_t *sample = &ram[ram_head];
        memset(&r, 0, sizeof(r));
        r.boot = Store_Boot();
        r.tick = sample->publish_tick;
        r.temperature = sample->data.temperature_x10;
        r.humidity = sample->data.humidity_x10;
        r.type = sample->health.state;
        r.number = sample->sequence;
        snprintf(r.text, sizeof(r.text), "%lu", (unsigned long)sample->sequence);
        waiting_live = 1;
        result = 0;
    }
    if (result)
    {
        network_monitor.store_errors++;
        return -1;
    }
    char json[240];
    /* 整数带单位，避免嵌入式printf浮点开销；boot+number供服务端去重。 */
    int len = snprintf(
        json, sizeof(json),
        "{\"device\":\"%s\",\"boot\":%lu,\"number\":%lu,\"tick\":%lu,\"sample\":%lu,\"temperature_"
        "x10\":%ld,\"humidity_x10\":%ld,\"state\":%lu}",
        MQTT_CLIENT_ID, (unsigned long)r.boot, (unsigned long)r.number, (unsigned long)r.tick,
        (unsigned long)strtoul(r.text, NULL, 10), (long)r.temperature, (long)r.humidity,
        (unsigned long)r.type);
    if (len < 0 || (size_t)len >= sizeof(json))
        return -1;
    if (++packet_id == 0)
        packet_id = 1;
    size_t n = Mqtt_Publish(tx, sizeof(tx), MQTT_TOPIC, json, packet_id);
    if (Send(tx, n))
        return -1;
    waiting_id = packet_id;
    waiting_record = r.number;
    waiting_since = Now();
    return 0;
}
#ifndef NETWORK_HOST_TEST
static void NetworkTask(void *arg)
{
    (void)arg;
    W5500_PortRegister();
    network_monitor.storage_ok = (uint32_t)(Store_Init() == 0);
    if (!network_monitor.storage_ok)
        network_monitor.store_errors++;
    char reset[24];
    snprintf(reset, sizeof(reset), "reset: %08lX", (unsigned long)AppWatchdog_ResetFlags());
    (void)Store_Log(reset);
    int chip_ready = 0;
    uint32_t probe_period = AppParams_Get()->network_probe_period_ms;
    uint32_t last_probe = Now() - probe_period;
    state = NET_RETRY;
    retry_ms = 1000U;
    since = Now() - retry_ms;
    for (;;)
    {
        LogItem event;
        for (unsigned i = 0; i < 4 && osMessageQueueGet(log_queue, &event, NULL, 0) == osOK; i++)
            if (Store_LogLevel(event.level, event.text))
                network_monitor.store_errors++;
        GatewaySnapshot_t sample;
        for (unsigned i = 0;
             i < 4 && ram_count < 16 && osMessageQueueGet(sample_queue, &sample, NULL, 0) == osOK;
             i++)
        {
            if (!ram_count)
                ram_since = Now();
            ram[(ram_head + ram_count) % 16] = sample;
            ram_count++;
        }
        int must_save = Buffer_NeedSave(ram_count, network_monitor.online, Now() - ram_since) ||
                        (network_monitor.online && Store_Pending() && ram_count >= 4);
        /* QoS1的RAM消息未确认时不可弹出；先断开，再统一转存。 */
        if (must_save && (!waiting_id || !waiting_live) && (uint32_t)(Now() - last_flush) >= 1000U)
        {
            last_flush = Now();
            for (unsigned i = 0; i < 4 && ram_count; i++)
            {
                if (Store_SaveSample(&ram[ram_head]))
                {
                    network_monitor.store_errors++;
                    network_monitor.reason = NET_REASON_STORAGE;
                    break;
                }
                network_monitor.flash_saved++;
                PopRam();
            }
        }
        network_monitor.pending = Store_Pending();
        AppHealth_Report(APP_HEALTH_NETWORK);
        if (!chip_ready && (uint32_t)(Now() - since) >= retry_ms)
        {
            chip_ready = (InitChip() == 0);
            network_monitor.chip_ok = (uint32_t)chip_ready;
            if (!chip_ready)
            {
                network_monitor.reason = NET_REASON_CHIP;
                Retry();
            }
        }
        if (chip_ready)
        {
            if ((uint32_t)(Now() - last_probe) >= probe_period)
            {
                W5500_PortClearStatus();
                network_monitor.link_up = (getPHYCFGR() & 1U) ? 1U : 0U;
                if (W5500_PortGetStatus() != HAL_OK || getVERSIONR() != 4)
                {
                    chip_ready = 0;
                    network_monitor.chip_ok = 0;
                }
                last_probe = Now();
            }
            if (!chip_ready)
            {
                network_monitor.reason = NET_REASON_CHIP;
                Retry();
            }
            else if (!network_monitor.link_up)
            {
                network_monitor.reason = NET_REASON_CABLE;
                if (state != NET_RETRY)
                    Retry();
            }
            else if (state == NET_RETRY && (uint32_t)(Now() - since) >= retry_ms)
            {
                network_monitor.reason = NET_REASON_TCP;
                network_monitor.reconnects++;
                close(0);
                if (socket(0, Sn_MR_TCP, 0, SF_IO_NONBLOCK) != 0 ||
                    connect(0, server, MQTT_SERVER_PORT) < 0)
                    Retry();
                else
                {
                    state = NET_TCP;
                    since = Now();
                }
            }
            else if (state == NET_TCP)
            {
                if (getSn_SR(0) == SOCK_ESTABLISHED)
                {
                    size_t n =
                        Mqtt_Connect(tx, sizeof(tx), MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
                    if (Send(tx, n))
                        Retry();
                    else
                    {
                        state = NET_CONNACK;
                        since = Now();
                        last_receive = Now();
                    }
                }
                else if (getSn_SR(0) == SOCK_CLOSED || (uint32_t)(Now() - since) > 3000U)
                    Retry();
            }
            else if (state >= NET_CONNACK)
            {
                if (getSn_SR(0) != SOCK_ESTABLISHED || ReceivePackets())
                {
                    network_monitor.protocol_errors++;
                    if (network_monitor.reason != NET_REASON_AUTH &&
                        network_monitor.reason != NET_REASON_STORAGE)
                        network_monitor.reason = NET_REASON_PROTOCOL;
                    Retry();
                }
                else if (state != NET_ONLINE && (uint32_t)(Now() - since) > 3000U)
                    Retry();
                else if (state == NET_ONLINE)
                {
                    if ((uint32_t)(Now() - online_since) >= 30000U)
                        retry_ms = 0;
                    if (waiting_id && (uint32_t)(Now() - waiting_since) > 3000U)
                    {
                        network_monitor.reason = NET_REASON_ACK;
                        Retry();
                    }
                    else if (ping_waiting && (uint32_t)(Now() - last_receive) > 30000U)
                    {
                        network_monitor.reason = NET_REASON_PING;
                        Retry();
                    }
                    else if ((uint32_t)(Now() - last_send) > 10000U)
                    {
                        uint8_t ping[] = {0xC0, 0};
                        if (Send(ping, 2))
                            Retry();
                        else
                            ping_waiting = 1;
                    }
                    else if (!waiting_id && UploadOne())
                        Retry();
                }
            }
        }
        PublishInfo();
        osDelay(pdMS_TO_TICKS(20));
    }
}
void AppNetworkTask_Create(void)
{
    const osThreadAttr_t attr = {
        .name = "network", .stack_size = 4096, .priority = osPriorityBelowNormal};
    configASSERT(osThreadNew(NetworkTask, NULL, &attr) != NULL);
}

#endif /* NETWORK_HOST_TEST */
