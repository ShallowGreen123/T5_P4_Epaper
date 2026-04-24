#include "ui_screens.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "factory_hdmi.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_power_label = nullptr;
static lv_obj_t *s_bridge_label = nullptr;
static lv_obj_t *s_mode_label = nullptr;
static lv_obj_t *s_fps_label = nullptr;
static lv_obj_t *s_frame_label = nullptr;
static lv_obj_t *s_memory_label = nullptr;
static lv_obj_t *s_error_label = nullptr;
static lv_obj_t *s_stage = nullptr;
static lv_obj_t *s_probe = nullptr;
static lv_timer_t *s_timer = nullptr;
static factory_hdmi_mode_t s_selected_mode = FACTORY_HDMI_MODE_PATTERN;

static const char *mode_name(factory_hdmi_mode_t mode)
{
    switch (mode) {
        case FACTORY_HDMI_MODE_PATTERN:
            return "Pattern";
        case FACTORY_HDMI_MODE_MOTION:
            return "Motion";
        case FACTORY_HDMI_MODE_CAMERA:
            return "Camera";
        case FACTORY_HDMI_MODE_AUDIO:
            return "Audio";
        case FACTORY_HDMI_MODE_SD_VIDEO:
            return "SD Video";
        default:
            return "Unknown";
    }
}

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

static void refresh_state()
{
    const factory_hdmi_state_t *state = factory_hdmi_get_state();
    if (state == nullptr) {
        return;
    }

    char text[128];

    snprintf(text, sizeof(text), "Status: %s", state->status_text);
    set_text_if_changed(s_status_label, text);

    snprintf(text, sizeof(text), "Power: %s", state->powered ? "on" : "off");
    set_text_if_changed(s_power_label, text);

    snprintf(text, sizeof(text), "Bridge: %s | %ux%u", state->ready ? "ready" : "not ready", state->width, state->height);
    set_text_if_changed(s_bridge_label, text);

    snprintf(text, sizeof(text), "Mode: %s", mode_name(state->mode));
    set_text_if_changed(s_mode_label, text);

    snprintf(text, sizeof(text), "FPS: %u", state->fps);
    set_text_if_changed(s_fps_label, text);

    snprintf(text, sizeof(text), "Frames: %u", state->frame_count);
    set_text_if_changed(s_frame_label, text);

    snprintf(text, sizeof(text), "Free PSRAM: %u KB", (unsigned)(state->free_psram / 1024U));
    set_text_if_changed(s_memory_label, text);

    snprintf(text, sizeof(text), "Last error: %s", state->last_error);
    set_text_if_changed(s_error_label, text);

    if (s_probe != nullptr && s_stage != nullptr) {
        const int32_t usable_w = lv_obj_get_width(s_stage) - 58;
        const int32_t span = usable_w > 0 ? usable_w : 1;
        const int32_t x = 14 + (int32_t)((state->frame_count * 7U) % (uint32_t)span);
        lv_obj_set_pos(s_probe, x, 18);
        lv_obj_set_style_bg_color(s_probe, state->running ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    }
}

static void hdmi_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_state();
}

static void action_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const intptr_t action = (intptr_t)lv_event_get_user_data(e);
    switch (action) {
        case 0:
            factory_hdmi_start(s_selected_mode);
            break;
        case 1:
            s_selected_mode = FACTORY_HDMI_MODE_PATTERN;
            factory_hdmi_start(s_selected_mode);
            factory_hdmi_set_mode(s_selected_mode);
            break;
        case 2:
            s_selected_mode = FACTORY_HDMI_MODE_MOTION;
            factory_hdmi_start(s_selected_mode);
            factory_hdmi_set_mode(s_selected_mode);
            break;
        case 3:
            factory_hdmi_stop();
            break;
        default:
            break;
    }

    refresh_state();
}

static lv_obj_t *create_metric_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = factory_ui_create_info_label(parent, text);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    return label;
}

static void create_hdmi(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "HDMI Showcase");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 96, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 10, LV_PART_MAIN);

    lv_obj_t *summary = factory_ui_create_info_label(
        panel,
        "External HDMI monitor diagnostics. The e-paper stays as the control console while RGB888 frames are rendered on HDMI.");
    lv_obj_set_style_text_font(summary, LV_FONT_DEFAULT, LV_PART_MAIN);

    lv_obj_t *metrics = lv_obj_create(panel);
    lv_obj_set_width(metrics, lv_pct(100));
    lv_obj_set_height(metrics, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(metrics, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(metrics, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(metrics, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(metrics, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(metrics, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(metrics, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(metrics, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_status_label = create_metric_label(metrics, "Status: --");
    s_power_label = create_metric_label(metrics, "Power: --");
    s_bridge_label = create_metric_label(metrics, "Bridge: --");
    s_mode_label = create_metric_label(metrics, "Mode: --");
    s_fps_label = create_metric_label(metrics, "FPS: --");
    s_frame_label = create_metric_label(metrics, "Frames: --");
    s_memory_label = create_metric_label(metrics, "Free PSRAM: --");
    s_error_label = create_metric_label(metrics, "Last error: None");

    lv_obj_t *button_row = lv_obj_create(panel);
    lv_obj_set_width(button_row, lv_pct(100));
    lv_obj_set_height(button_row, 62);
    lv_obj_set_style_border_width(button_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(button_row, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button_row, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(button_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *start_btn = factory_ui_create_action_button(button_row, "Start", action_btn_event_cb, (void *)0);
    lv_obj_t *pattern_btn = factory_ui_create_action_button(button_row, "Pattern", action_btn_event_cb, (void *)1);
    lv_obj_t *motion_btn = factory_ui_create_action_button(button_row, "Motion", action_btn_event_cb, (void *)2);
    lv_obj_t *stop_btn = factory_ui_create_action_button(button_row, "Stop", action_btn_event_cb, (void *)3);
    lv_obj_set_width(start_btn, lv_pct(23));
    lv_obj_set_width(pattern_btn, lv_pct(23));
    lv_obj_set_width(motion_btn, lv_pct(23));
    lv_obj_set_width(stop_btn, lv_pct(23));

    s_stage = lv_obj_create(panel);
    lv_obj_set_width(s_stage, lv_pct(100));
    lv_obj_set_flex_grow(s_stage, 1);
    lv_obj_set_style_bg_color(s_stage, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_stage, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_stage, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_stage, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_stage, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_stage, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_stage, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *stage_hint = lv_label_create(s_stage);
    lv_label_set_text(stage_hint, "HDMI output: RGB888 800x600");
    lv_obj_align(stage_hint, LV_ALIGN_BOTTOM_LEFT, 14, -14);

    s_probe = lv_obj_create(s_stage);
    lv_obj_set_size(s_probe, 44, 30);
    lv_obj_set_style_bg_color(s_probe, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_probe, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_probe, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_probe, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_probe, 8, LV_PART_MAIN);
    lv_obj_set_pos(s_probe, 14, 18);
}

static void entry_hdmi(void)
{
    factory_hdmi_init();
    refresh_state();
    s_timer = lv_timer_create(hdmi_timer_cb, 500, nullptr);
}

static void exit_hdmi(void)
{
    if (s_timer != nullptr) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    factory_hdmi_stop();
}

static void destroy_hdmi(void)
{
    s_status_label = nullptr;
    s_power_label = nullptr;
    s_bridge_label = nullptr;
    s_mode_label = nullptr;
    s_fps_label = nullptr;
    s_frame_label = nullptr;
    s_memory_label = nullptr;
    s_error_label = nullptr;
    s_stage = nullptr;
    s_probe = nullptr;
}

static scr_lifecycle_t s_hdmi_lifecycle = {
    .create = create_hdmi,
    .entry = entry_hdmi,
    .exit = exit_hdmi,
    .destroy = destroy_hdmi,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_hdmi_lifecycle(void)
{
    return &s_hdmi_lifecycle;
}
