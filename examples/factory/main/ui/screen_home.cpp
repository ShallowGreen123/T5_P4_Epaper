#include "ui_screens.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "esp_timer.h"
#include "factory_assets.h"
#include "factory_battery.h"
#include "factory_port.h"
#include "factory_wifi.h"
#include "lvgl.h"
#include "scr_mrg.h"
#include "ui_theme.h"

namespace {

struct MenuItem {
    const char *symbol;
    const char *title;
    factory_page_id_t page_id;
    uint8_t page;
};

static const MenuItem kMenuItems[] = {
    {LV_SYMBOL_BELL, "Clock", FACTORY_PAGE_CLOCK, 1},
    {LV_SYMBOL_BLUETOOTH, "LoRa", FACTORY_PAGE_LORA, 1},
    {LV_SYMBOL_SD_CARD, "SD", FACTORY_PAGE_SD, 0},
    {LV_SYMBOL_SETTINGS, "Setting", FACTORY_PAGE_SETTING, 1},
    {LV_SYMBOL_EYE_OPEN, "Test", FACTORY_PAGE_TEST, 1},
    {LV_SYMBOL_WIFI, "WiFi", FACTORY_PAGE_WIFI, 0},
    {LV_SYMBOL_BATTERY_FULL, "Battery", FACTORY_PAGE_BATTERY, 0},
    {LV_SYMBOL_GPS, "GPS", FACTORY_PAGE_GPS, 1},
    {LV_SYMBOL_REFRESH, "Adjust", FACTORY_PAGE_ADJUST, 0},
    {LV_SYMBOL_AUDIO, "Audio", FACTORY_PAGE_AUDIO, 0},
    {LV_SYMBOL_IMAGE, "Camera", FACTORY_PAGE_CAMERA, 0},
    {LV_SYMBOL_VIDEO, "HDMI", FACTORY_PAGE_HDMI, 0},
    {LV_SYMBOL_GPS, "IMU", FACTORY_PAGE_ICM20948, 0},
    {LV_SYMBOL_POWER, "Shutdown", FACTORY_PAGE_SHUTDOWN, 1},
    {LV_SYMBOL_PAUSE, "Sleep", FACTORY_PAGE_SLEEP, 1},
};

static lv_obj_t *s_pages[2] = {};
static lv_obj_t *s_dots[2] = {};
static lv_obj_t *s_prev_btn = nullptr;
static lv_obj_t *s_next_btn = nullptr;
static lv_obj_t *s_time_label = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_timer_t *s_status_timer = nullptr;
static uint8_t s_page_index = 0;

static void set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == nullptr || text == nullptr) {
        return;
    }

    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void format_status_time(char *buffer, size_t buffer_size)
{
    time_t now = time(nullptr);
    struct tm timeinfo = {};
    if (now > 0 && localtime_r(&now, &timeinfo) != nullptr && timeinfo.tm_year >= (2024 - 1900)) {
        strftime(buffer, buffer_size, "%H:%M", &timeinfo);
        return;
    }

    const uint64_t uptime_seconds = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    const uint32_t hours = (uint32_t)((uptime_seconds / 3600ULL) % 24ULL);
    const uint32_t minutes = (uint32_t)((uptime_seconds / 60ULL) % 60ULL);
    snprintf(buffer, buffer_size, "%02u:%02u", hours, minutes);
}

static void update_status_bar()
{
    const factory_runtime_info_t *runtime = factory_port_get_runtime_info();
    const factory_battery_state_t *battery = factory_battery_get_state();
    const char *wifi_status = factory_wifi_get_status() ? "on" : (factory_wifi_is_connecting() ? "..." : "off");
    char time_text[16];
    char battery_text[8];
    char charge_text[8];
    char status_text[64];
    const char *charge_gap = "";

    format_status_time(time_text, sizeof(time_text));
    if (battery != nullptr && battery->gauge_ready && battery->gauge_read_ok) {
        snprintf(battery_text, sizeof(battery_text), "%u%%", battery->soc_percent);
    } else {
        snprintf(battery_text, sizeof(battery_text), "--%%");
    }
    if (battery != nullptr && battery->vbus_connected) {
        snprintf(charge_text, sizeof(charge_text), "%s", LV_SYMBOL_CHARGE);
        charge_gap = " ";
    } else {
        charge_text[0] = '\0';
    }

    if (s_time_label != nullptr) {
        set_text_if_changed(s_time_label, time_text);
    }
    if (s_status_label != nullptr) {
        snprintf(status_text,
                 sizeof(status_text),
                 "%s %s%s%s  %s %s  TP %s",
                 LV_SYMBOL_BATTERY_FULL,
                 battery_text,
                 charge_gap,
                 charge_text,
                 LV_SYMBOL_WIFI,
                 wifi_status,
                 runtime->touch_ready ? "on" : "off");
        set_text_if_changed(s_status_label, status_text);
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_status_bar();
}

static void apply_page_visibility()
{
    for (uint8_t i = 0; i < 2; ++i) {
        if (s_pages[i] == nullptr) {
            continue;
        }
        if (i == s_page_index) {
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_black(), LV_PART_MAIN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_white(), LV_PART_MAIN);
        }
    }

    if (s_prev_btn != nullptr) {
        if (s_page_index == 0) {
            lv_obj_add_state(s_prev_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_prev_btn, LV_STATE_DISABLED);
        }
    }
    if (s_next_btn != nullptr) {
        if (s_page_index >= 1) {
            lv_obj_add_state(s_next_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_next_btn, LV_STATE_DISABLED);
        }
    }
}

static void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_page_id_t page_id = (factory_page_id_t)(intptr_t)lv_event_get_user_data(e);
    scr_mgr_push((int)page_id, false);
}

static void nav_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    int next_page = (int)s_page_index + (int)delta;
    if (next_page < 0) {
        next_page = 0;
    }
    if (next_page > 1) {
        next_page = 1;
    }
    s_page_index = (uint8_t)next_page;
    apply_page_visibility();
}

static void create_page_items(lv_obj_t *parent, uint8_t page)
{
    for (size_t i = 0; i < sizeof(kMenuItems) / sizeof(kMenuItems[0]); ++i) {
        if (kMenuItems[i].page != page) {
            continue;
        }

        lv_obj_t *tile = factory_ui_create_menu_tile(
            parent,
            kMenuItems[i].symbol,
            kMenuItems[i].title,
            menu_btn_event_cb,
            (void *)(intptr_t)kMenuItems[i].page_id);
        lv_obj_set_size(tile, page == 0 ? lv_pct(31) : lv_pct(31), 150);
    }
}

static void create_home(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);

    lv_obj_t *status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, lv_pct(100), 56);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(status_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(status_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(status_bar, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(status_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_time_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(s_time_label, FACTORY_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_time_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(s_time_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_status_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(s_status_label, FACTORY_FONT_SYMBOL, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_align_to(panel, status_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_style_pad_bottom(panel, 72, LV_PART_MAIN);

    for (uint8_t page = 0; page < 2; ++page) {
        s_pages[page] = lv_obj_create(panel);
        lv_obj_set_size(s_pages[page], lv_pct(100), lv_pct(100));
        lv_obj_set_style_border_width(s_pages[page], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_pages[page], 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_pages[page], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_pages[page], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(s_pages[page], LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(s_pages[page], LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(s_pages[page], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(s_pages[page], 14, LV_PART_MAIN);
        lv_obj_set_style_pad_column(s_pages[page], 14, LV_PART_MAIN);
        create_page_items(s_pages[page], page);
    }

    s_prev_btn = factory_ui_create_action_button(panel, LV_SYMBOL_LEFT, nav_btn_event_cb, (void *)-1);
    lv_obj_set_size(s_prev_btn, 68, 48);
    lv_obj_align(s_prev_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_next_btn = factory_ui_create_action_button(panel, LV_SYMBOL_RIGHT, nav_btn_event_cb, (void *)1);
    lv_obj_set_size(s_next_btn, 68, 48);
    lv_obj_align(s_next_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *dot_wrap = lv_obj_create(panel);
    lv_obj_set_size(dot_wrap, 120, 28);
    lv_obj_align(dot_wrap, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(dot_wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot_wrap, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot_wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dot_wrap, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(dot_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(dot_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_wrap, 12, LV_PART_MAIN);

    for (uint8_t i = 0; i < 2; ++i) {
        s_dots[i] = lv_obj_create(dot_wrap);
        lv_obj_set_size(s_dots[i], 16, 16);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_dots[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dots[i], 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_dots[i], 0, LV_PART_MAIN);
    }

    s_page_index = 0;
    update_status_bar();
    apply_page_visibility();
}

static void entry_home(void)
{
    update_status_bar();
    s_status_timer = lv_timer_create(status_timer_cb, 1000, nullptr);
}

static void exit_home(void)
{
    if (s_status_timer != nullptr) {
        lv_timer_del(s_status_timer);
        s_status_timer = nullptr;
    }
}

static void destroy_home(void)
{
    for (uint8_t i = 0; i < 2; ++i) {
        s_pages[i] = nullptr;
        s_dots[i] = nullptr;
    }
    s_prev_btn = nullptr;
    s_next_btn = nullptr;
    s_time_label = nullptr;
    s_status_label = nullptr;
    s_page_index = 0;
}

static scr_lifecycle_t s_home_lifecycle = {
    .create = create_home,
    .entry = entry_home,
    .exit = exit_home,
    .destroy = destroy_home,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_home_lifecycle(void)
{
    return &s_home_lifecycle;
}
