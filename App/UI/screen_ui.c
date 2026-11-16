#include "app_watchdog.h"
#include "main.h"
#include "version.h"
#include "flash_store.h"
#include "network.h"
#include "screen_ui.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
    UI_PAGE_SENSOR = 0,
    UI_PAGE_DEVICE,
    UI_PAGE_LOG,
    UI_PAGE_DIAG,
    UI_PAGE_COUNT
} UiPage_t;

static lv_obj_t *pages[UI_PAGE_COUNT];
static lv_obj_t *log_label;
static uint32_t log_offset;
static lv_obj_t *diag_label;
static uint8_t log_min_level = 1;

static lv_obj_t *temperature_label;
static lv_obj_t *humidity_label;
static lv_obj_t *status_label;

static lv_obj_t *device_state_label;
static lv_obj_t *age_label;
static lv_obj_t *failure_label;
static lv_obj_t *sequence_label;

static UiPage_t current_page;

/* 每页分别记录已经设置的状态颜色 */
static uint32_t shown_colors[UI_PAGE_COUNT] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};

static volatile struct
{
    UiPage_t page;
    uint32_t switch_count;
} ui_monitor;

static lv_obj_t *Create_Label(lv_obj_t *parent, const char *text, lv_align_t align, lv_coord_t x,
                              lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);

    configASSERT(label != NULL);

    lv_label_set_text(label, text);
    lv_obj_align(label, align, x, y);

    return label;
}

static void Set_TextIfChanged(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0)
    {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *Create_Panel(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *panel = lv_obj_create(parent);

    configASSERT(panel != NULL);

    lv_obj_remove_style_all(panel);

    lv_obj_set_size(panel, 212, 56);
    lv_obj_set_pos(panel, 14, y);

    lv_obj_set_style_bg_color(panel, lv_color_hex(0x172335), 0);

    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(panel, 8, 0);

    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

static void Apply_PageVisibility(void)
{
    uint32_t index;

    for (index = 0U; index < UI_PAGE_COUNT; index++)
    {
        if (index == (uint32_t)current_page)
        {
            lv_obj_clear_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
        }
    }

    ui_monitor.page = current_page;
}

void AppUi_Init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *label;
    lv_obj_t *panel;

    uint32_t index;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B121E), 0);

    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_set_style_text_color(screen, lv_color_hex(0xFFFFFF), 0);

    lv_obj_set_style_text_font(screen, &lv_font_montserrat_20, 0);

    /* 各页面覆盖整个屏幕 */
    for (index = 0U; index < UI_PAGE_COUNT; index++)
    {
        pages[index] = lv_obj_create(screen);

        configASSERT(pages[index] != NULL);

        lv_obj_remove_style_all(pages[index]);
        lv_obj_set_size(pages[index], 240, 240);
        lv_obj_set_pos(pages[index], 0, 0);

        lv_obj_set_style_bg_color(pages[index], lv_color_hex(0x0B121E), 0);

        lv_obj_set_style_bg_opa(pages[index], LV_OPA_COVER, 0);

        lv_obj_clear_flag(pages[index], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* 第1页：实时温湿度 */
    label = Create_Label(pages[UI_PAGE_SENSOR], "SENSOR", LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_set_style_text_color(label, lv_color_hex(0x6DD5FF), 0);

    panel = Create_Panel(pages[UI_PAGE_SENSOR], 52);

    temperature_label = Create_Label(panel, "T: --.- C", LV_ALIGN_CENTER, 0, 0);

    panel = Create_Panel(pages[UI_PAGE_SENSOR], 116);

    humidity_label = Create_Label(panel, "H: --.- %RH", LV_ALIGN_CENTER, 0, 0);

    status_label = Create_Label(pages[UI_PAGE_SENSOR], "WAITING", LV_ALIGN_BOTTOM_MID, 0, -40);

    label = Create_Label(pages[UI_PAGE_SENSOR], "KEY: NEXT     1/4", LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    /* 第2页：设备状态 */
    label = Create_Label(pages[UI_PAGE_DEVICE], "DEVICE", LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_set_style_text_color(label, lv_color_hex(0x6DD5FF), 0);

    device_state_label =
        Create_Label(pages[UI_PAGE_DEVICE], "STATE: WAITING", LV_ALIGN_TOP_LEFT, 16, 54);

    age_label = Create_Label(pages[UI_PAGE_DEVICE], "AGE: --", LV_ALIGN_TOP_LEFT, 16, 88);

    failure_label = Create_Label(pages[UI_PAGE_DEVICE], "FAIL: 0", LV_ALIGN_TOP_LEFT, 16, 122);

    sequence_label = Create_Label(pages[UI_PAGE_DEVICE], "SEQ: 0", LV_ALIGN_TOP_LEFT, 16, 156);

    label = Create_Label(pages[UI_PAGE_DEVICE], "KEY: NEXT     2/4", LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    Create_Label(pages[UI_PAGE_LOG], "LOG / NETWORK", LV_ALIGN_TOP_MID, 0, 14);
    log_label = Create_Label(pages[UI_PAGE_LOG], "starting...", LV_ALIGN_TOP_LEFT, 12, 55);
    lv_obj_set_style_text_font(log_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(log_label, 216);
    lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
    lv_obj_t *hint =
        Create_Label(pages[UI_PAGE_LOG], "KEY: NEXT   HOLD: OLDER", LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    Create_Label(pages[UI_PAGE_DIAG], "DIAG " GATEWAY_VERSION, LV_ALIGN_TOP_MID, 0, 12);
    diag_label = Create_Label(pages[UI_PAGE_DIAG], "starting...", LV_ALIGN_TOP_LEFT, 10, 44);
    lv_obj_set_style_text_font(diag_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(diag_label, 220);
    lv_obj_t *filter_hint =
        Create_Label(pages[UI_PAGE_DIAG], "HOLD: LOG FILTER", LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_font(filter_hint, &lv_font_montserrat_14, 0);
    current_page = UI_PAGE_SENSOR;
    Apply_PageVisibility();
}

void AppUi_NextPage(void)
{
    current_page = (UiPage_t)(((unsigned)current_page + 1U) % UI_PAGE_COUNT);
    log_offset = 0;

    Apply_PageVisibility();

    ui_monitor.switch_count++;
}

void AppUi_Update(const GatewaySnapshot_t *snapshot, uint32_t now_tick)
{
    char text[320];

    const char *state_text;
    uint32_t state_color;
    uint8_t usable;

    lv_obj_t *state_label;

    configASSERT(snapshot != NULL);

    usable =
        (uint8_t)((snapshot->health.state == DEVICE_ONLINE) &&
                  (DeviceHealth_IsFresh(&snapshot->health, now_tick, pdMS_TO_TICKS(2500U)) != 0U));

    if (snapshot->health.state == DEVICE_OFFLINE)
    {
        state_text = "OFFLINE";
        state_color = 0xFF0000U;
    }
    else if (snapshot->health.has_sample == 0U)
    {
        state_text = "WAITING";
        state_color = 0x4080FFU;
    }
    else if (usable != 0U)
    {
        state_text = "ONLINE";
        state_color = 0x00FF00U;
    }
    else
    {
        state_text = "STALE";
        state_color = 0xFFFF00U;
    }

    if (current_page == UI_PAGE_DIAG)
    {
        NetworkInfo info;
        Network_GetInfo(&info);
        const char *reason[] = {"NONE",     "CHIP/SPI", "CABLE", "TCP",    "AUTH",
                                "PROTOCOL", "PUBACK",   "PING",  "STORAGE"};
        snprintf(text, sizeof(text),
                 "LINK:%lu MQTT:%lu FLASH:%lu\nWHY:%s WAIT:%lus\nRAM:%lu DISK:%lu\nSENT:%lu "
                 "LIVE:%lu\nDROP:%lu IOERR:%lu\nHEAP MIN:%lu\nUP:%lus RESET:%08lX",
                 (unsigned long)info.link_up, (unsigned long)info.online,
                 (unsigned long)info.storage_ok, info.reason < 9 ? reason[info.reason] : "UNKNOWN",
                 (unsigned long)(info.retry_ms / 1000), (unsigned long)info.ram_pending,
                 (unsigned long)info.pending, (unsigned long)info.uploaded,
                 (unsigned long)info.live_uploaded, (unsigned long)info.queue_drops,
                 (unsigned long)info.store_errors, (unsigned long)info.min_heap,
                 (unsigned long)(HAL_GetTick() / 1000), (unsigned long)AppWatchdog_ResetFlags());
        Set_TextIfChanged(diag_label, text);
        return;
    }
    if (current_page == UI_PAGE_LOG)
    {
        StoreRecord log;
        const char *level[] = {"?", "INFO", "WARN", "ERROR"};
        if (Store_LogReadLevel(log_offset, log_min_level, &log) == 0)
            snprintf(text, sizeof(text), "FILTER:%s  OFFSET:%lu\n%s #%lu BOOT:%lu\nTICK:%lu ms\n%s",
                     level[log_min_level], (unsigned long)log_offset,
                     log.type <= 3 ? level[log.type] : "?", (unsigned long)log.number,
                     (unsigned long)log.boot, (unsigned long)log.tick, log.text);
        else
            snprintf(text, sizeof(text), "FILTER:%s OFFSET:%lu\nNo matching log",
                     level[log_min_level], (unsigned long)log_offset);
        Set_TextIfChanged(log_label, text);
        return;
    }
    if (current_page == UI_PAGE_SENSOR)
    {
        if (usable != 0U)
        {
            int32_t temperature = snapshot->data.temperature_x10;

            uint32_t magnitude =
                (uint32_t)((temperature < 0) ? -(int64_t)temperature : (int64_t)temperature);

            snprintf(text, sizeof(text), "T: %s%lu.%lu C", (temperature < 0) ? "-" : "",
                     (unsigned long)(magnitude / 10U), (unsigned long)(magnitude % 10U));

            Set_TextIfChanged(temperature_label, text);

            snprintf(text, sizeof(text), "H: %u.%u %%RH",
                     (unsigned int)(snapshot->data.humidity_x10 / 10U),
                     (unsigned int)(snapshot->data.humidity_x10 % 10U));

            Set_TextIfChanged(humidity_label, text);
        }
        else
        {
            Set_TextIfChanged(temperature_label, "T: --.- C");

            Set_TextIfChanged(humidity_label, "H: --.- %RH");
        }

        state_label = status_label;
        Set_TextIfChanged(state_label, state_text);
    }
    else
    {
        state_label = device_state_label;

        snprintf(text, sizeof(text), "STATE: %s", state_text);

        Set_TextIfChanged(state_label, text);

        if (snapshot->health.has_sample != 0U)
        {
            uint32_t age_ticks = (uint32_t)(now_tick - snapshot->health.last_success_tick);

            uint32_t age_ms = (uint32_t)((uint64_t)age_ticks * 1000U / osKernelGetTickFreq());

            snprintf(text, sizeof(text), "AGE: %lu.%lu s", (unsigned long)(age_ms / 1000U),
                     (unsigned long)((age_ms % 1000U) / 100U));

            Set_TextIfChanged(age_label, text);
        }
        else
        {
            Set_TextIfChanged(age_label, "AGE: --");
        }

        snprintf(text, sizeof(text), "FAIL: %lu",
                 (unsigned long)(snapshot->health.consecutive_failures));

        Set_TextIfChanged(failure_label, text);

        snprintf(text, sizeof(text), "SEQ: %lu", (unsigned long)snapshot->sequence);

        Set_TextIfChanged(sequence_label, text);
    }

    if (shown_colors[current_page] != state_color)
    {
        lv_obj_set_style_text_color(state_label, lv_color_hex(state_color), 0);

        shown_colors[current_page] = state_color;
    }
}

void AppUi_OlderLog(void)
{
    if (current_page == UI_PAGE_DIAG)
    {
        log_min_level = (uint8_t)(log_min_level % 3U + 1U);
        log_offset = 0;
    }
    else
        log_offset = (log_offset + 1U) % 64U;
    current_page = UI_PAGE_LOG;
    Apply_PageVisibility();
}
