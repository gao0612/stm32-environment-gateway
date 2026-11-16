#include "flash_store.h"
#include "w25q64.h"
#include "cmsis_os.h"
#include "task_health.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>
/* 8MB分为两个4MB区域。每条64字节，CRC校验内容，commit最后写。 */
#define AREA_BYTES (4U * 1024U * 1024U)
#define RECORDS (AREA_BYTES / 64U)
#define MAGIC 0x47575232U
#define COMMIT 0x1234ABCDU
_Static_assert(sizeof(StoreRecord) == 64, "record must be 64 bytes");
static osMutexId_t lock;
static uint32_t next[2], serial[2], pending, oldest, boot_id;
static uint16_t sector_pending[1024];
static uint8_t scan[4096];
static volatile int ready;
static uint32_t Crc(const void *data, size_t n)
{
    const uint8_t *b = data;
    uint32_t crc = 0xFFFFFFFFU;
    while (n--)
    {
        crc ^= *b++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0);
    }
    return ~crc;
}
static int Valid(const StoreRecord *r)
{
    return r->magic == MAGIC && r->commit == COMMIT && r->crc == Crc(r, offsetof(StoreRecord, crc));
}
static int Newer(uint32_t a, uint32_t b)
{
    return a != b && (uint32_t)(a - b) < 0x80000000U;
}
static uint32_t Address(int area, uint32_t i)
{
    return (uint32_t)area * AREA_BYTES + i * 64U;
}
static int Read(int area, uint32_t i, StoreRecord *r)
{
    return Flash_Read(Address(area, i), r, sizeof(*r));
}
static int Take(void)
{
    return ready && osMutexAcquire(lock, 2000U) == osOK;
}
int Store_Init(void)
{
    ready = 0;
    lock = osMutexNew(NULL);
    if (!lock || Flash_Init())
        return -1;
    memset(next, 0, sizeof(next));
    memset(serial, 0, sizeof(serial));
    memset(sector_pending, 0, sizeof(sector_pending));
    pending = 0;
    uint32_t old_number = 0, max_boot = 0;
    int old_found = 0;
    for (int area = 0; area < 2; area++)
    {
        int found = 0;
        for (uint32_t sector = 0; sector < 1024; sector++)
        {
            AppHealth_Report(APP_HEALTH_NETWORK);
            if (sector % 32U == 0)
                osDelay(1);
            if (Flash_Read((uint32_t)area * AREA_BYTES + sector * 4096U, scan, sizeof(scan)))
                return -1;
            for (uint32_t slot = 0; slot < 64; slot++)
            {
                StoreRecord r;
                memcpy(&r, scan + slot * 64U, sizeof(r));
                if (!Valid(&r))
                    continue;
                uint32_t i = sector * 64U + slot;
                if (!found || Newer(r.number, serial[area]))
                {
                    found = 1;
                    serial[area] = r.number;
                    next[area] = (i + 1U) % RECORDS;
                }
                if (Newer(r.boot, max_boot))
                    max_boot = r.boot;
                if (area == 1 && r.sent != 0xFFFFFFFEU)
                {
                    pending++;
                    sector_pending[sector]++;
                    if (!old_found || Newer(old_number, r.number))
                    {
                        old_found = 1;
                        old_number = r.number;
                        oldest = i;
                    }
                }
            }
        }
    }
    boot_id = max_boot + 1U;
    ready = 1;
    return Store_Log("boot");
}
static int Append(int area, StoreRecord *r)
{
    for (uint32_t tries = 0; tries < RECORDS; tries++)
    {
        uint32_t i = next[area], address = Address(area, i);
        if (i % 64U == 0)
        {
            if (area == 1 && sector_pending[i / 64U])
                return -2;
            if (Flash_Erase(address))
                return -1;
        }
        else
        {
            StoreRecord old;
            if (Read(area, i, &old))
                return -1;
            /* 跳过断电留下的半条记录，不在未擦除地址重写。 */
            int blank = 1;
            const uint8_t *b = (const uint8_t *)&old;
            for (size_t k = 0; k < sizeof(old); k++)
                if (b[k] != 0xFFU)
                {
                    blank = 0;
                    break;
                }
            if (!blank)
            {
                next[area] = (i + 1U) % RECORDS;
                continue;
            }
        }
        r->magic = MAGIC;
        r->number = serial[area] + 1U;
        r->boot = boot_id;
        r->crc = Crc(r, offsetof(StoreRecord, crc));
        r->commit = COMMIT;
        r->sent = UINT32_MAX;
        /* 先写内容和CRC，再读回比较，最后写提交标志。 */
        StoreRecord check;
        if (Flash_Write(address, r, offsetof(StoreRecord, commit)) || Read(area, i, &check) ||
            memcmp(&check, r, offsetof(StoreRecord, commit)) ||
            Flash_Write(address + offsetof(StoreRecord, commit), &r->commit, 4) ||
            Read(area, i, &check) || !Valid(&check))
        {
            next[area] = (i + 1U) % RECORDS;
            return -1;
        }
        serial[area] = r->number;
        next[area] = (i + 1U) % RECORDS;
        if (area == 1)
        {
            if (!pending)
                oldest = i;
            pending++;
            sector_pending[i / 64U]++;
        }
        return 0;
    }
    return -2;
}
int Store_LogLevel(uint8_t level, const char *text)
{
    if (!text || !Take())
        return -1;
    StoreRecord r = {0};
    r.tick = osKernelGetTickCount();
    r.type = level >= 1 && level <= 3 ? level : 1;
    strncpy(r.text, text, sizeof(r.text) - 1);
    int status = Append(0, &r);
    osMutexRelease(lock);
    return status;
}
int Store_SaveSample(const GatewaySnapshot_t *s)
{
    if (!s || !Take())
        return -1;
    StoreRecord r = {0};
    r.tick = s->publish_tick;
    r.temperature = s->data.temperature_x10;
    r.humidity = s->data.humidity_x10;
    r.type = (uint32_t)s->health.state;
    snprintf(r.text, sizeof(r.text), "%lu", (unsigned long)s->sequence);
    int status = Append(1, &r);
    osMutexRelease(lock);
    return status;
}
static int FindOldest(StoreRecord *r)
{
    if (!pending)
        return -2;
    for (uint32_t n = 0; n < RECORDS; n++)
    {
        if (Read(1, oldest, r))
            return -1;
        if (Valid(r) && r->sent != 0xFFFFFFFEU)
            return 0;
        oldest = (oldest + 1U) % RECORDS;
    }
    return -1;
}
int Store_Peek(StoreRecord *r)
{
    if (!r || !Take())
        return -1;
    int status = FindOldest(r);
    osMutexRelease(lock);
    return status;
}
int Store_Ack(uint32_t number)
{
    if (!Take())
        return -1;
    StoreRecord r;
    int status = FindOldest(&r);
    if (!status && r.number != number)
        status = -1;
    uint32_t sent = 0xFFFFFFFEU, check = UINT32_MAX;
    if (!status)
    {
        uint32_t a = Address(1, oldest) + offsetof(StoreRecord, sent);
        status = Flash_Write(a, &sent, 4) || Flash_Read(a, &check, 4) || (check != sent);
        if (!status)
        {
            sector_pending[oldest / 64U]--;
            pending--;
            oldest = (oldest + 1U) % RECORDS;
        }
    }
    osMutexRelease(lock);
    return status ? -1 : 0;
}
int Store_LogReadLevel(uint32_t offset, uint8_t min_level, StoreRecord *r)
{
    if (!r || !Take())
        return -1;
    int status = -2;
    uint32_t i = next[0];
    for (uint32_t n = 0; n < RECORDS; n++)
    {
        i = (i + RECORDS - 1U) % RECORDS;
        if (Read(0, i, r))
        {
            status = -1;
            break;
        }
        if (Valid(r) && r->type >= min_level && offset-- == 0)
        {
            status = 0;
            break;
        }
        /* UI只读取最近的64条，防止读空区时长时间占锁。 */
        if (n >= 63U)
            break;
    }
    osMutexRelease(lock);
    return status;
}
uint32_t Store_Pending(void)
{
    if (!Take())
        return 0;
    uint32_t n = pending;
    osMutexRelease(lock);
    return n;
}
uint32_t Store_Boot(void)
{
    return boot_id;
}

int Store_Log(const char *text)
{
    return Store_LogLevel(1, text);
}
int Store_LogRead(uint32_t offset, StoreRecord *out)
{
    return Store_LogReadLevel(offset, 1, out);
}
