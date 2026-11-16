#include "app_params_store.h"

#include "internal_flash.h"

#include <stddef.h>

#define STORE_MAGIC 0x31505747U

#define STORE_VERSION 2U

#define STORE_PAYLOAD_BYTES 24U

#define STORE_COMMIT 0xC0DEC0DEU

_Static_assert(INTERNAL_FLASH_WORD_COUNT == 12U, "Dual-slot record requires 12 Flash words");

typedef struct
{
    AppParams_t params;

    uint32_t sequence;

    uint8_t version;

} StoreSlot_t;

typedef struct
{
    AppStoreStatus_t slot_status[2];

    uint32_t slot_sequence[2];

    uint32_t selected_sequence;

    uint32_t target_sequence;

    uint8_t selected_slot;

    uint8_t selected_version;

    uint8_t target_slot;

} StoreDiag_t;

static volatile StoreDiag_t store_diag = {.selected_slot = 255U, .target_slot = 255U};

static uint32_t Store_Pack4(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void Store_Unpack4(uint32_t word, uint8_t bytes[4])
{
    bytes[0] = (uint8_t)word;

    bytes[1] = (uint8_t)(word >> 8U);

    bytes[2] = (uint8_t)(word >> 16U);

    bytes[3] = (uint8_t)(word >> 24U);
}

static uint32_t Store_Crc32(const uint32_t *words, uint32_t word_count)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0U; i < word_count * 4U; i++)
    {
        uint8_t byte = (uint8_t)(words[i / 4U] >> ((i % 4U) * 8U));

        crc ^= byte;

        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

static AppStoreStatus_t Store_ReadSlot(uint8_t slot, StoreSlot_t *output)
{
    uint32_t record[INTERNAL_FLASH_WORD_COUNT];

    uint8_t all_erased = 1U;

    uint32_t crc_index;

    StoreSlot_t decoded = {0};

    if (InternalFlash_Read(slot, record) != HAL_OK)
    {
        return APP_STORE_ERR_FLASH;
    }

    for (uint32_t i = 0U; i < INTERNAL_FLASH_WORD_COUNT; i++)
    {
        if (record[i] != 0xFFFFFFFFU)
        {
            all_erased = 0U;
        }
    }

    if (all_erased != 0U)
    {
        return APP_STORE_ERR_EMPTY;
    }

    if ((record[0] != STORE_MAGIC) || (record[2] != STORE_PAYLOAD_BYTES))
    {
        return APP_STORE_ERR_FORMAT;
    }

    if (record[1] == 1U)
    {
        crc_index = 9U;

        decoded.sequence = 0U;
    }
    else if (record[1] == STORE_VERSION)
    {
        if (record[11] != STORE_COMMIT)
        {
            return APP_STORE_ERR_INCOMPLETE;
        }

        if (record[9] == 0U)
        {
            return APP_STORE_ERR_SEQUENCE;
        }

        crc_index = 10U;

        decoded.sequence = record[9];
    }
    else
    {
        return APP_STORE_ERR_VERSION;
    }

    if (record[crc_index] != Store_Crc32(record, crc_index))
    {
        return APP_STORE_ERR_CRC;
    }

    if ((record[4] & 0xFFFF0000U) != 0U)
    {
        return APP_STORE_ERR_FORMAT;
    }

    Store_Unpack4(record[3], decoded.params.mac);

    decoded.params.mac[4] = (uint8_t)record[4];

    decoded.params.mac[5] = (uint8_t)(record[4] >> 8U);

    Store_Unpack4(record[5], decoded.params.ip);

    Store_Unpack4(record[6], decoded.params.mask);

    Store_Unpack4(record[7], decoded.params.gateway);

    decoded.params.network_probe_period_ms = record[8];

    if (AppParams_Validate(&decoded.params) != APP_PARAMS_OK)
    {
        return APP_STORE_ERR_PARAMS;
    }

    decoded.version = (uint8_t)record[1];

    *output = decoded;

    return APP_STORE_OK;
}

static AppStoreStatus_t Store_Select(StoreSlot_t *latest, uint8_t *slot)
{
    StoreSlot_t a = {0};
    StoreSlot_t b = {0};

    AppStoreStatus_t sa = Store_ReadSlot(APP_FLASH_SLOT_A, &a);

    AppStoreStatus_t sb = Store_ReadSlot(APP_FLASH_SLOT_B, &b);

    store_diag.slot_status[0] = sa;
    store_diag.slot_status[1] = sb;

    store_diag.slot_sequence[0] = a.sequence;
    store_diag.slot_sequence[1] = b.sequence;

    store_diag.selected_slot = 255U;
    store_diag.selected_sequence = 0U;
    store_diag.selected_version = 0U;

    if ((sa != APP_STORE_OK) && (sb != APP_STORE_OK))
    {
        if ((sa == APP_STORE_ERR_FLASH) || (sb == APP_STORE_ERR_FLASH))
        {
            return APP_STORE_ERR_FLASH;
        }

        if ((sa == APP_STORE_ERR_EMPTY) && (sb == APP_STORE_ERR_EMPTY))
        {
            return APP_STORE_ERR_EMPTY;
        }

        return (sb != APP_STORE_ERR_EMPTY) ? sb : sa;
    }

    if ((sb == APP_STORE_OK) && ((sa != APP_STORE_OK) || (b.sequence >= a.sequence)))
    {
        *latest = b;

        *slot = APP_FLASH_SLOT_B;
    }
    else
    {
        *latest = a;

        *slot = APP_FLASH_SLOT_A;
    }

    store_diag.selected_slot = *slot;

    store_diag.selected_sequence = latest->sequence;

    store_diag.selected_version = latest->version;

    return APP_STORE_OK;
}

AppStoreStatus_t AppParamsStore_Load(AppParams_t *params)
{
    StoreSlot_t latest;

    uint8_t slot;

    if (params == NULL)
    {
        return APP_STORE_ERR_ARGUMENT;
    }

    AppStoreStatus_t status = Store_Select(&latest, &slot);

    if (status == APP_STORE_OK)
    {
        *params = latest.params;
    }

    return status;
}

AppStoreStatus_t AppParamsStore_Save(const AppParams_t *params)
{
    uint32_t record[INTERNAL_FLASH_WORD_COUNT] = {0U};

    StoreSlot_t latest;
    StoreSlot_t verified;

    uint8_t selected_slot;

    uint8_t target = APP_FLASH_SLOT_A;

    uint32_t sequence = 1U;

    store_diag.target_slot = 255U;
    store_diag.target_sequence = 0U;

    if (params == NULL)
    {
        return APP_STORE_ERR_ARGUMENT;
    }

    if (AppParams_Validate(params) != APP_PARAMS_OK)
    {
        return APP_STORE_ERR_PARAMS;
    }

    AppStoreStatus_t status = Store_Select(&latest, &selected_slot);

    if ((store_diag.slot_status[0] == APP_STORE_ERR_FLASH) ||
        (store_diag.slot_status[1] == APP_STORE_ERR_FLASH))
    {
        return APP_STORE_ERR_FLASH;
    }

    if (status == APP_STORE_OK)
    {
        if (latest.sequence == UINT32_MAX)
        {
            return APP_STORE_ERR_SEQUENCE;
        }

        target = (uint8_t)(1U - selected_slot);

        sequence = latest.sequence + 1U;
    }

    store_diag.target_slot = target;
    store_diag.target_sequence = sequence;

    record[0] = STORE_MAGIC;
    record[1] = STORE_VERSION;
    record[2] = STORE_PAYLOAD_BYTES;

    record[3] = Store_Pack4(params->mac);

    record[4] = (uint32_t)params->mac[4] | ((uint32_t)params->mac[5] << 8U);

    record[5] = Store_Pack4(params->ip);
    record[6] = Store_Pack4(params->mask);
    record[7] = Store_Pack4(params->gateway);

    record[8] = params->network_probe_period_ms;

    record[9] = sequence;

    record[10] = Store_Crc32(record, 10U);

    record[11] = STORE_COMMIT;

    if (InternalFlash_Save(target, record) != HAL_OK)
    {
        return APP_STORE_ERR_FLASH;
    }

    return Store_ReadSlot(target, &verified);
}