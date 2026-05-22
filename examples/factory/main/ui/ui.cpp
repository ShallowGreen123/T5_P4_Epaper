#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "factory_assets.h"
#include "factory_battery.h"
#include "factory_display.h"
#include "factory_touch.h"
#include "factory_types.h"
#include "scr_mrg.h"
#include "ui_screens.h"

#include "lvgl.h"

namespace {

constexpr uint32_t kLowPowerThresholdMv = CONFIG_FACTORY_BATTERY_LOW_VOLTAGE_MV;
constexpr uint32_t kLowPowerCountdownMs =
    (uint32_t)CONFIG_FACTORY_BATTERY_LOW_POWER_COUNTDOWN_SEC * 1000U;
constexpr uint32_t kLowPowerPollMs = CONFIG_FACTORY_BATTERY_LOW_POWER_POLL_MS;
constexpr uint32_t kInactivityPollMs = 1000U;
constexpr uint32_t kPowerOffSettleMs = 250U;
constexpr char kPowerOffMessage[] =
    "Device has been powered off. Please press and hold the `Power` button to turn it on.";
constexpr char kPowerOffFailedMessage[] =
    "Shutdown failed. Please disconnect USB power and try again.";

static lv_obj_t *s_low_power_overlay = nullptr;
static lv_obj_t *s_low_power_card = nullptr;
static lv_obj_t *s_low_power_title = nullptr;
static lv_obj_t *s_low_power_message = nullptr;
static lv_obj_t *s_low_power_countdown = nullptr;
static lv_timer_t *s_low_power_timer = nullptr;
static bool s_low_power_active = false;
static uint32_t s_low_power_started_at_ms = 0U;

static lv_obj_t *s_power_off_overlay = nullptr;
static lv_obj_t *s_power_off_label = nullptr;
static bool s_power_off_requested = false;
static bool s_power_off_request_cancelable = false;
static bool s_power_off_active = false;
static bool s_power_off_failed = false;

static lv_timer_t *s_inactivity_timer = nullptr;
static uint8_t s_inactivity_shutdown_minutes = 1U;
static uint32_t s_last_touch_activity_ms = 0U;

static void refresh_low_power_protection();
static void refresh_inactivity_shutdown();
static bool request_power_off_internal(bool cancelable_by_touch);

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

static uint8_t normalize_inactivity_shutdown_minutes(uint8_t minutes)
{
    switch (minutes) {
        case 0:
        case 1:
        case 5:
        case 10:
        case 30:
            return minutes;
        default:
            return 0;
    }
}

static uint32_t inactivity_shutdown_timeout_ms()
{
    return (uint32_t)s_inactivity_shutdown_minutes * 60U * 1000U;
}

static bool shutdown_supported_now()
{
    factory_battery_refresh();
    const factory_battery_state_t *state = factory_battery_get_state();
    return state != nullptr &&
           state->charger_ready &&
           state->charger_read_ok &&
           !state->vbus_connected;
}

static bool low_power_popup_visible()
{
    return s_low_power_overlay != nullptr &&
           !lv_obj_has_flag(s_low_power_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hide_low_power_popup()
{
    if (s_low_power_overlay == nullptr) {
        return;
    }

    lv_obj_add_flag(s_low_power_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void show_low_power_popup()
{
    if (s_low_power_overlay == nullptr) {
        return;
    }

    lv_obj_clear_flag(s_low_power_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_low_power_overlay);
}

static void update_low_power_popup(uint32_t remaining_ms)
{
    if (s_low_power_message == nullptr || s_low_power_countdown == nullptr) {
        return;
    }

    const uint32_t remaining_seconds = (remaining_ms + 999U) / 1000U;
    char countdown_text[48];

    set_text_if_changed(s_low_power_message, "Low battery. Please charge now.");
    snprintf(countdown_text, sizeof(countdown_text), "Automatic shutdown in %u s.", remaining_seconds);
    set_text_if_changed(s_low_power_countdown, countdown_text);
}

static void start_low_power_countdown()
{
    s_low_power_active = true;
    s_low_power_started_at_ms = lv_tick_get();
    update_low_power_popup(kLowPowerCountdownMs);
    show_low_power_popup();
}

static void cancel_low_power_countdown()
{
    s_low_power_active = false;
    s_low_power_started_at_ms = 0U;
    hide_low_power_popup();
}

static void power_off_overlay_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_power_off_failed || s_power_off_overlay == nullptr) {
        return;
    }

    lv_obj_add_flag(s_power_off_overlay, LV_OBJ_FLAG_HIDDEN);
    s_power_off_failed = false;
    s_power_off_active = false;
    s_last_touch_activity_ms = lv_tick_get();
    factory_display_request_full_refresh();
}

static void create_power_off_overlay()
{
    lv_obj_t *top = lv_layer_top();
    if (top == nullptr) {
        return;
    }

    s_power_off_overlay = lv_obj_create(top);
    lv_obj_set_size(s_power_off_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_power_off_overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_power_off_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_power_off_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_power_off_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_power_off_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_power_off_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_power_off_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_power_off_overlay, power_off_overlay_event_cb, LV_EVENT_CLICKED, nullptr);

    s_power_off_label = lv_label_create(s_power_off_overlay);
    lv_obj_set_width(s_power_off_label, lv_pct(86));
    lv_label_set_long_mode(s_power_off_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_power_off_label, kPowerOffMessage);
    lv_obj_align(s_power_off_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(s_power_off_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_power_off_label, FACTORY_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_power_off_label, lv_color_black(), LV_PART_MAIN);
}

static void show_power_off_overlay(const char *message)
{
    if (s_power_off_overlay == nullptr || s_power_off_label == nullptr) {
        return;
    }

    set_text_if_changed(s_power_off_label, message);
    lv_obj_clear_flag(s_power_off_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_power_off_overlay);
}

static void create_low_power_popup()
{
    lv_obj_t *top = lv_layer_top();
    if (top == nullptr) {
        return;
    }

    s_low_power_overlay = lv_obj_create(top);
    lv_obj_set_size(s_low_power_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_low_power_overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_low_power_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_low_power_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_low_power_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_low_power_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_low_power_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_low_power_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    s_low_power_card = lv_obj_create(s_low_power_overlay);
    lv_obj_set_size(s_low_power_card, lv_pct(88), LV_SIZE_CONTENT);
    lv_obj_align(s_low_power_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_low_power_card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_low_power_card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_low_power_card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_low_power_card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_low_power_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_low_power_card, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_low_power_card, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_low_power_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_low_power_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_low_power_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_low_power_title = lv_label_create(s_low_power_card);
    lv_obj_set_width(s_low_power_title, lv_pct(100));
    lv_label_set_text(s_low_power_title, "Low Battery");
    lv_obj_set_style_text_font(s_low_power_title, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_low_power_title, lv_color_black(), LV_PART_MAIN);

    s_low_power_message = lv_label_create(s_low_power_card);
    lv_obj_set_width(s_low_power_message, lv_pct(100));
    lv_label_set_long_mode(s_low_power_message, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_low_power_message, "Low battery. Please charge now.");
    lv_obj_set_style_text_font(s_low_power_message, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_low_power_message, lv_color_black(), LV_PART_MAIN);

    s_low_power_countdown = lv_label_create(s_low_power_card);
    lv_obj_set_width(s_low_power_countdown, lv_pct(100));
    lv_label_set_long_mode(s_low_power_countdown, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_low_power_countdown, "");
    lv_obj_set_style_text_font(s_low_power_countdown, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_low_power_countdown, lv_color_black(), LV_PART_MAIN);
}

static void low_power_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_low_power_protection();
}

static void inactivity_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_inactivity_shutdown();
}

static void refresh_low_power_protection()
{
    factory_battery_refresh();
    const factory_battery_state_t *state = factory_battery_get_state();
    if (state == nullptr) {
        cancel_low_power_countdown();
        return;
    }

    if (state->vbus_connected) {
        cancel_low_power_countdown();
        return;
    }

    if (!s_low_power_active) {
        const bool low_voltage =
            state->gauge_ready &&
            state->gauge_read_ok &&
            state->gauge_voltage_mv < kLowPowerThresholdMv;
        if (!low_voltage) {
            if (low_power_popup_visible()) {
                hide_low_power_popup();
            }
            return;
        }

        start_low_power_countdown();
    }

    const uint32_t elapsed_ms = lv_tick_elaps(s_low_power_started_at_ms);
    if (elapsed_ms < kLowPowerCountdownMs) {
        update_low_power_popup(kLowPowerCountdownMs - elapsed_ms);
        return;
    }

    update_low_power_popup(0U);
    if (!request_power_off_internal(false)) {
        // Keep the protection latched and retry on the next polling cycle unless power is restored.
        s_low_power_started_at_ms = lv_tick_get() - kLowPowerCountdownMs + 1000U;
    }
}

static void refresh_inactivity_shutdown()
{
    if (s_power_off_requested || s_power_off_active || s_power_off_failed) {
        return;
    }

    if (s_inactivity_shutdown_minutes == 0U || !factory_touch_is_ready()) {
        s_last_touch_activity_ms = lv_tick_get();
        return;
    }

    if (!shutdown_supported_now()) {
        s_last_touch_activity_ms = lv_tick_get();
        return;
    }

    const uint32_t timeout_ms = inactivity_shutdown_timeout_ms();
    if (timeout_ms == 0U || lv_tick_elaps(s_last_touch_activity_ms) < timeout_ms) {
        return;
    }

    if (!request_power_off_internal(true)) {
        s_last_touch_activity_ms = lv_tick_get();
    }
}

static bool request_power_off_internal(bool cancelable_by_touch)
{
    if (s_power_off_requested || s_power_off_active) {
        return false;
    }

    s_power_off_requested = true;
    s_power_off_request_cancelable = cancelable_by_touch;
    return true;
}

}  // namespace

extern "C" void factory_ui_init(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    if (disp != nullptr) {
        disp->theme = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);
    }

    scr_mgr_init();
    scr_mgr_set_bg_color(0xFFFFFF);
    scr_mgr_set_anim(LV_SCR_LOAD_ANIM_NONE, LV_SCR_LOAD_ANIM_NONE, LV_SCR_LOAD_ANIM_NONE);

    scr_mgr_register(FACTORY_PAGE_HOME, factory_screen_home_lifecycle());
    scr_mgr_register(FACTORY_PAGE_SETTING, factory_screen_setting_lifecycle());
    scr_mgr_register(FACTORY_PAGE_TEST, factory_screen_test_lifecycle());
    scr_mgr_register(FACTORY_PAGE_DISPLAY, factory_screen_display_lifecycle());
    scr_mgr_register(FACTORY_PAGE_TOUCH, factory_screen_touch_lifecycle());
    scr_mgr_register(FACTORY_PAGE_ADJUST, factory_screen_adjust_lifecycle());
    scr_mgr_register(FACTORY_PAGE_WIFI, factory_screen_wifi_lifecycle());
    scr_mgr_register(FACTORY_PAGE_SD, factory_screen_sd_lifecycle());
    scr_mgr_register(FACTORY_PAGE_BATTERY, factory_screen_battery_lifecycle());
    scr_mgr_register(FACTORY_PAGE_AUDIO, factory_screen_audio_lifecycle());
    scr_mgr_register(FACTORY_PAGE_CAMERA, factory_screen_camera_lifecycle());
    scr_mgr_register(FACTORY_PAGE_HDMI, factory_screen_hdmi_lifecycle());
    scr_mgr_register(FACTORY_PAGE_ICM20948, factory_screen_icm20948_lifecycle());

    scr_mgr_register(FACTORY_PAGE_CLOCK, factory_placeholder_lifecycle(FACTORY_PAGE_CLOCK));
    scr_mgr_register(FACTORY_PAGE_LORA, factory_placeholder_lifecycle(FACTORY_PAGE_LORA));
    scr_mgr_register(FACTORY_PAGE_GPS, factory_placeholder_lifecycle(FACTORY_PAGE_GPS));
    scr_mgr_register(FACTORY_PAGE_SHUTDOWN, factory_placeholder_lifecycle(FACTORY_PAGE_SHUTDOWN));
    scr_mgr_register(FACTORY_PAGE_SLEEP, factory_placeholder_lifecycle(FACTORY_PAGE_SLEEP));

    scr_mgr_switch(FACTORY_PAGE_HOME, false);

    create_low_power_popup();
    create_power_off_overlay();
    s_last_touch_activity_ms = lv_tick_get();

    refresh_low_power_protection();
    s_low_power_timer = lv_timer_create(low_power_timer_cb, kLowPowerPollMs, nullptr);
    s_inactivity_timer = lv_timer_create(inactivity_timer_cb, kInactivityPollMs, nullptr);
}

extern "C" void factory_ui_task_handler(void)
{
    if (!s_power_off_requested || s_power_off_active) {
        return;
    }

    s_power_off_requested = false;
    s_power_off_request_cancelable = false;
    if (!shutdown_supported_now()) {
        s_last_touch_activity_ms = lv_tick_get();
        return;
    }

    cancel_low_power_countdown();
    show_power_off_overlay(kPowerOffMessage);
    s_power_off_active = true;
    s_power_off_failed = false;

    factory_display_refresh_now_clean();
    vTaskDelay(pdMS_TO_TICKS(kPowerOffSettleMs));

    if (factory_battery_shutdown()) {
        return;
    }

    show_power_off_overlay(kPowerOffFailedMessage);
    s_power_off_active = false;
    s_power_off_failed = true;
    factory_display_request_full_refresh();
    s_last_touch_activity_ms = lv_tick_get();
}

extern "C" void factory_ui_notify_touch_activity(void)
{
    if (s_power_off_requested && s_power_off_request_cancelable && !s_power_off_active) {
        s_power_off_requested = false;
        s_power_off_request_cancelable = false;
    }

    if (!s_power_off_active) {
        s_last_touch_activity_ms = lv_tick_get();
    }
}

extern "C" void factory_ui_set_inactivity_shutdown_minutes(uint8_t minutes)
{
    s_inactivity_shutdown_minutes = normalize_inactivity_shutdown_minutes(minutes);
    s_last_touch_activity_ms = lv_tick_get();
}

extern "C" uint8_t factory_ui_get_inactivity_shutdown_minutes(void)
{
    return s_inactivity_shutdown_minutes;
}

extern "C" bool factory_ui_request_power_off(void)
{
    return request_power_off_internal(false);
}
