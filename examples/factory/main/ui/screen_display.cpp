#include "ui_screens.h"

#include "factory_display.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

static lv_obj_t *s_mode_label = nullptr;
static lv_obj_t *s_tick_label = nullptr;
static lv_obj_t *s_refresh_label = nullptr;
static lv_obj_t *s_stage = nullptr;
static lv_obj_t *s_probe = nullptr;
static lv_timer_t *s_timer = nullptr;
static uint32_t s_tick_count = 0;
static int64_t s_last_full_refresh_ms = 0;

static void update_display_state()
{
    const factory_display_mode_info_t *info = factory_display_get_mode_info();
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const int32_t usable_w = lv_obj_get_width(s_stage) - 40;
    const int32_t span = usable_w > 0 ? usable_w : 1;
    const int32_t x = 20 + (int32_t)((s_tick_count * 48U) % (uint32_t)span);
    const int32_t y = 26 + (int32_t)(((s_tick_count % 5U) * 36U));

    lv_label_set_text_fmt(
        s_mode_label,
        "Mode: %s | %ux%u | rot %u | mirror %u | p%u/f%u",
        info->mode_name,
        info->width,
        info->height,
        info->rotation_deg,
        info->mirror_mode,
        info->partial_passes,
        info->full_passes);
    lv_label_set_text_fmt(s_tick_label, "Partial update tick: %u", s_tick_count);
    lv_obj_set_pos(s_probe, x, y);

    const int64_t remaining_ms = CONFIG_FACTORY_DISPLAY_TEST_FULL_REFRESH_MS - (now_ms - s_last_full_refresh_ms);
    if (remaining_ms <= 0) {
        factory_display_request_full_refresh();
        s_last_full_refresh_ms = now_ms;
    }

    const int64_t show_ms = remaining_ms > 0 ? remaining_ms : CONFIG_FACTORY_DISPLAY_TEST_FULL_REFRESH_MS;
    lv_label_set_text_fmt(s_refresh_label, "Next automatic full refresh in %lld ms", show_ms);
}

static void full_refresh_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    factory_display_request_full_refresh();
    s_last_full_refresh_ms = esp_timer_get_time() / 1000;
    lv_label_set_text(s_refresh_label, "Full refresh requested");
}

static void display_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_tick_count++;
    update_display_state();
}

static void create_display(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Display");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_mode_label = factory_ui_create_info_label(panel, "Mode:");
    s_tick_label = factory_ui_create_info_label(panel, "Partial update tick:");
    s_refresh_label = factory_ui_create_info_label(panel, "Next automatic full refresh:");

    lv_obj_t *btn = factory_ui_create_action_button(panel, "Request Full Refresh", full_refresh_btn_event_cb, nullptr);
    lv_obj_set_width(btn, 320);

    s_stage = lv_obj_create(panel);
    lv_obj_set_width(s_stage, lv_pct(100));
    lv_obj_set_flex_grow(s_stage, 1);
    lv_obj_set_style_bg_color(s_stage, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_stage, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_stage, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_stage, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_stage, 12, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_stage, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hint = lv_label_create(s_stage);
    lv_label_set_text_fmt(hint, "Local timer: %d ms\nAuto full refresh: %d ms", CONFIG_FACTORY_DISPLAY_TEST_LOCAL_REFRESH_MS, CONFIG_FACTORY_DISPLAY_TEST_FULL_REFRESH_MS);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 16, -16);

    s_probe = lv_obj_create(s_stage);
    lv_obj_set_size(s_probe, 90, 50);
    lv_obj_set_style_bg_color(s_probe, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_probe, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_probe, 10, LV_PART_MAIN);
    lv_obj_set_pos(s_probe, 20, 26);
}

static void entry_display(void)
{
    s_tick_count = 0;
    s_last_full_refresh_ms = esp_timer_get_time() / 1000;
    update_display_state();
    s_timer = lv_timer_create(display_timer_cb, CONFIG_FACTORY_DISPLAY_TEST_LOCAL_REFRESH_MS, nullptr);
}

static void exit_display(void)
{
    if (s_timer != nullptr) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
}

static void destroy_display(void)
{
    s_mode_label = nullptr;
    s_tick_label = nullptr;
    s_refresh_label = nullptr;
    s_stage = nullptr;
    s_probe = nullptr;
}

static scr_lifecycle_t s_display_lifecycle = {
    .create = create_display,
    .entry = entry_display,
    .exit = exit_display,
    .destroy = destroy_display,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_display_lifecycle(void)
{
    return &s_display_lifecycle;
}
