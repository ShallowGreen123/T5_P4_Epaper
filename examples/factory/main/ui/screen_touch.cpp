#include "ui_screens.h"

#include <stdio.h>

#include "driver/factory_display.h"
#include "driver/factory_touch.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

constexpr uint8_t kTrailDots = 8;

static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_coord_label = nullptr;
static lv_obj_t *s_raw_label = nullptr;
static lv_obj_t *s_surface = nullptr;
static lv_obj_t *s_cursor = nullptr;
static lv_obj_t *s_dots[kTrailDots] = {};
static lv_timer_t *s_timer = nullptr;
static uint32_t s_last_sample_count = 0;
static uint8_t s_dot_index = 0;

static void move_dot(lv_obj_t *dot, int16_t x, int16_t y)
{
    const factory_display_mode_info_t *info = factory_display_get_mode_info();
    const lv_coord_t surface_w = lv_obj_get_width(s_surface);
    const lv_coord_t surface_h = lv_obj_get_height(s_surface);
    const lv_coord_t px = (lv_coord_t)(((int32_t)x * (surface_w - 14)) / (info->width - 1));
    const lv_coord_t py = (lv_coord_t)(((int32_t)y * (surface_h - 14)) / (info->height - 1));
    lv_obj_set_pos(dot, px, py);
}

static void update_touch_state()
{
    factory_touch_diag_state_t state = {};
    factory_touch_get_diag_state(&state);

    lv_label_set_text_fmt(s_status_label, "Status: %s | Pressed: %s", state.status_text, state.pressed ? "yes" : "no");
    lv_label_set_text_fmt(s_coord_label, "Mapped coordinates: %d, %d", state.x, state.y);
    lv_label_set_text_fmt(s_raw_label, "Raw coordinates: %d, %d | Samples: %u", state.raw_x, state.raw_y, state.sample_count);

    if (!state.ready) {
        lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    move_dot(s_cursor, state.x, state.y);

    if (state.sample_count != s_last_sample_count && state.pressed) {
        move_dot(s_dots[s_dot_index], state.x, state.y);
        lv_obj_clear_flag(s_dots[s_dot_index], LV_OBJ_FLAG_HIDDEN);
        s_dot_index = (uint8_t)((s_dot_index + 1U) % kTrailDots);
        s_last_sample_count = state.sample_count;
    }
}

static void touch_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_touch_state();
}

static void create_touch(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Touch");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 82);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_status_label = factory_ui_create_info_label(panel, "Status:");
    s_coord_label = factory_ui_create_info_label(panel, "Mapped coordinates:");
    s_raw_label = factory_ui_create_info_label(panel, "Raw coordinates:");

    s_surface = lv_obj_create(panel);
    lv_obj_set_width(s_surface, lv_pct(100));
    lv_obj_set_flex_grow(s_surface, 1);
    lv_obj_set_style_bg_color(s_surface, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_surface, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_surface, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_surface, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_surface, 12, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_surface, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_surface, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hint = lv_label_create(s_surface);
    lv_label_set_text(hint, "Tap or drag to update the cursor and trail.");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 12, -10);

    for (uint8_t i = 0; i < kTrailDots; ++i) {
        s_dots[i] = lv_obj_create(s_surface);
        lv_obj_set_size(s_dots[i], 10, 10);
        lv_obj_set_style_bg_color(s_dots[i], lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dots[i], 0, LV_PART_MAIN);
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_cursor = lv_obj_create(s_surface);
    lv_obj_set_size(s_cursor, 14, 14);
    lv_obj_set_style_bg_color(s_cursor, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cursor, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_cursor, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
}

static void entry_touch(void)
{
    s_last_sample_count = 0;
    s_dot_index = 0;
    update_touch_state();
    s_timer = lv_timer_create(touch_timer_cb, 60, nullptr);
}

static void exit_touch(void)
{
    if (s_timer != nullptr) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
}

static void destroy_touch(void)
{
    s_status_label = nullptr;
    s_coord_label = nullptr;
    s_raw_label = nullptr;
    s_surface = nullptr;
    s_cursor = nullptr;
    for (uint8_t i = 0; i < kTrailDots; ++i) {
        s_dots[i] = nullptr;
    }
}

static scr_lifecycle_t s_touch_lifecycle = {
    .create = create_touch,
    .entry = entry_touch,
    .exit = exit_touch,
    .destroy = destroy_touch,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_touch_lifecycle(void)
{
    return &s_touch_lifecycle;
}
