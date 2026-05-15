#include "ui_screens.h"

#include <stdio.h>

#include "factory_assets.h"
#include "factory_port.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

struct BacklightControl {
    uint8_t index;
    const char *title;
    const char *pin_name;
    lv_obj_t *card;
    lv_obj_t *state_label;
    lv_obj_t *sw;
};

static BacklightControl s_backlight_controls[] = {
    {.index = 1, .title = "Screen Backlight 1", .pin_name = "IO11", .card = nullptr, .state_label = nullptr, .sw = nullptr},
};

static void sync_backlight_controls();

static lv_obj_t *create_backlight_card(lv_obj_t *parent, BacklightControl *control)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *top_row = lv_obj_create(card);
    lv_obj_set_width(top_row, lv_pct(100));
    lv_obj_set_height(top_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(top_row, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(top_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_label = lv_label_create(top_row);
    lv_label_set_text(title_label, control->title);
    lv_obj_set_style_text_font(title_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_black(), LV_PART_MAIN);

    control->sw = lv_switch_create(top_row);
    lv_obj_set_style_transform_zoom(control->sw, 150, LV_PART_MAIN);
    lv_obj_add_event_cb(control->sw, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
            return;
        }

        BacklightControl *item = static_cast<BacklightControl *>(lv_event_get_user_data(e));
        if (item == nullptr) {
            return;
        }

        factory_port_set_backlight_enabled(item->index, lv_obj_has_state(item->sw, LV_STATE_CHECKED));
        sync_backlight_controls();
    }, LV_EVENT_VALUE_CHANGED, control);

    control->state_label = lv_label_create(card);
    lv_obj_set_width(control->state_label, lv_pct(100));
    lv_label_set_long_mode(control->state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(control->state_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(control->state_label, lv_color_black(), LV_PART_MAIN);

    return card;
}

static void sync_backlight_controls()
{
    for (size_t i = 0; i < sizeof(s_backlight_controls) / sizeof(s_backlight_controls[0]); ++i) {
        BacklightControl &control = s_backlight_controls[i];
        if (control.sw == nullptr || control.state_label == nullptr) {
            continue;
        }

        const bool enabled = factory_port_get_backlight_enabled(control.index);
        if (enabled) {
            lv_obj_add_state(control.sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(control.sw, LV_STATE_CHECKED);
        }
        lv_label_set_text_fmt(control.state_label, "Pin %s output: %s", control.pin_name, enabled ? "HIGH" : "LOW");
    }
}

static void create_setting(lv_obj_t *parent)
{
    const factory_runtime_info_t *runtime = factory_port_get_runtime_info();

    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Setting");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    lv_obj_set_width(factory_ui_create_value_card(panel, "Application", runtime->app_name), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Version", runtime->app_version), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Target", runtime->target), lv_pct(100));

    char resolution_text[32];
    snprintf(resolution_text, sizeof(resolution_text), "%ux%u", runtime->width, runtime->height);
    lv_obj_set_width(factory_ui_create_value_card(panel, "Resolution", resolution_text), lv_pct(100));

    lv_obj_set_width(factory_ui_create_value_card(panel, "Display Mode", runtime->display_mode), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Touch Status", runtime->touch_status), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Boot Target", runtime->boot_mode), lv_pct(100));

    for (size_t i = 0; i < sizeof(s_backlight_controls) / sizeof(s_backlight_controls[0]); ++i) {
        s_backlight_controls[i].card = create_backlight_card(panel, &s_backlight_controls[i]);
    }

    sync_backlight_controls();
}

static void entry_setting(void)
{
    sync_backlight_controls();
}
static void exit_setting(void) {}
static void destroy_setting(void)
{
    for (size_t i = 0; i < sizeof(s_backlight_controls) / sizeof(s_backlight_controls[0]); ++i) {
        s_backlight_controls[i].card = nullptr;
        s_backlight_controls[i].state_label = nullptr;
        s_backlight_controls[i].sw = nullptr;
    }
}

static scr_lifecycle_t s_setting_lifecycle = {
    .create = create_setting,
    .entry = entry_setting,
    .exit = exit_setting,
    .destroy = destroy_setting,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_setting_lifecycle(void)
{
    return &s_setting_lifecycle;
}
