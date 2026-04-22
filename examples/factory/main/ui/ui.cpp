#include "ui.h"

#include "sdkconfig.h"
#include "factory_battery.h"
#include "factory_types.h"
#include "scr_mrg.h"
#include "ui_screens.h"

#include "lvgl.h"

namespace {

constexpr uint32_t kLowPowerThresholdMv = CONFIG_FACTORY_BATTERY_LOW_VOLTAGE_MV;
constexpr uint32_t kLowPowerCountdownMs =
    (uint32_t)CONFIG_FACTORY_BATTERY_LOW_POWER_COUNTDOWN_SEC * 1000U;
constexpr uint32_t kLowPowerPollMs = CONFIG_FACTORY_BATTERY_LOW_POWER_POLL_MS;

static lv_obj_t *s_low_power_overlay = nullptr;
static lv_obj_t *s_low_power_card = nullptr;
static lv_obj_t *s_low_power_title = nullptr;
static lv_obj_t *s_low_power_message = nullptr;
static lv_obj_t *s_low_power_countdown = nullptr;
static lv_timer_t *s_low_power_timer = nullptr;
static bool s_low_power_active = false;
static uint32_t s_low_power_started_at_ms = 0U;

static void refresh_low_power_protection();

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
    lv_label_set_text(s_low_power_message, "Low battery. Please charge now.");
    lv_label_set_text_fmt(s_low_power_countdown, "Automatic shutdown in %u s.", remaining_seconds);
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

    if (factory_battery_shutdown()) {
        return;
    }

    // Keep the protection latched and retry on the next polling cycle unless power is restored.
    s_low_power_started_at_ms = lv_tick_get() - kLowPowerCountdownMs + 1000U;
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

    scr_mgr_register(FACTORY_PAGE_CLOCK, factory_placeholder_lifecycle(FACTORY_PAGE_CLOCK));
    scr_mgr_register(FACTORY_PAGE_LORA, factory_placeholder_lifecycle(FACTORY_PAGE_LORA));
    scr_mgr_register(FACTORY_PAGE_GPS, factory_placeholder_lifecycle(FACTORY_PAGE_GPS));
    scr_mgr_register(FACTORY_PAGE_SHUTDOWN, factory_placeholder_lifecycle(FACTORY_PAGE_SHUTDOWN));
    scr_mgr_register(FACTORY_PAGE_SLEEP, factory_placeholder_lifecycle(FACTORY_PAGE_SLEEP));

#ifdef CONFIG_FACTORY_BOOT_TOUCH_DIAGNOSTICS
    scr_mgr_switch(FACTORY_PAGE_TOUCH, false);
#else
    scr_mgr_switch(FACTORY_PAGE_HOME, false);
#endif

    create_low_power_popup();
    refresh_low_power_protection();
    s_low_power_timer = lv_timer_create(low_power_timer_cb, kLowPowerPollMs, nullptr);
}
