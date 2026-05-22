#include "ui_screens.h"

#include "factory_display.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_theme.h"

namespace {

static lv_obj_t *s_mode_dd = nullptr;
static lv_obj_t *s_rotation_dd = nullptr;
static lv_obj_t *s_mirror_dd = nullptr;
static lv_obj_t *s_partial_dd = nullptr;
static lv_obj_t *s_full_dd = nullptr;
static lv_obj_t *s_dither_sw = nullptr;
static lv_obj_t *s_low_flash_sw = nullptr;
static lv_obj_t *s_auto_full_dd = nullptr;
static lv_obj_t *s_auto_shutdown_dd = nullptr;

static const uint8_t kAutoFullOptions[] = {0, 5, 10, 15, 20, 25, 30};
static const uint8_t kAutoShutdownOptions[] = {0, 1, 5, 10, 30};

static void sync_switch_checked(lv_obj_t *sw, bool checked)
{
    if (sw == nullptr) {
        return;
    }

    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
}

static void sync_disabled_state(lv_obj_t *obj, bool disabled)
{
    if (obj == nullptr) {
        return;
    }

    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
}

static void update_mode_dependency()
{
    const bool use_4bpp = factory_display_get_color_mode();

    sync_disabled_state(s_partial_dd, use_4bpp);
    sync_disabled_state(s_full_dd, use_4bpp);
    sync_disabled_state(s_dither_sw, use_4bpp);
    sync_disabled_state(s_auto_full_dd, use_4bpp);
    sync_disabled_state(s_low_flash_sw, !use_4bpp);
}

static void sync_adjust_widgets()
{
    const factory_display_mode_info_t *info = factory_display_get_mode_info();
    int rotation_idx = 0;
    if (info->rotation_deg == 90U) {
        rotation_idx = 1;
    } else if (info->rotation_deg == 180U) {
        rotation_idx = 2;
    } else if (info->rotation_deg == 270U) {
        rotation_idx = 3;
    }

    lv_dropdown_set_selected(s_mode_dd, factory_display_get_color_mode() ? 1 : 0);
    lv_dropdown_set_selected(s_rotation_dd, rotation_idx);
    lv_dropdown_set_selected(s_mirror_dd, info->mirror_mode & 0x3U);
    lv_dropdown_set_selected(s_partial_dd, info->partial_passes - 1U);
    lv_dropdown_set_selected(s_full_dd, info->full_passes - 1U);
    sync_switch_checked(s_dither_sw, factory_display_get_dither());
    sync_switch_checked(s_low_flash_sw, factory_display_get_low_flash());

    uint8_t auto_full = factory_display_get_max_partial_refreshes_before_full();
    uint16_t auto_full_idx = 0;
    for (uint16_t i = 0; i < sizeof(kAutoFullOptions) / sizeof(kAutoFullOptions[0]); ++i) {
        if (kAutoFullOptions[i] == auto_full) {
            auto_full_idx = i;
            break;
        }
    }
    lv_dropdown_set_selected(s_auto_full_dd, auto_full_idx);

    uint8_t auto_shutdown = factory_ui_get_inactivity_shutdown_minutes();
    uint16_t auto_shutdown_idx = 0;
    for (uint16_t i = 0; i < sizeof(kAutoShutdownOptions) / sizeof(kAutoShutdownOptions[0]); ++i) {
        if (kAutoShutdownOptions[i] == auto_shutdown) {
            auto_shutdown_idx = i;
            break;
        }
    }
    lv_dropdown_set_selected(s_auto_shutdown_dd, auto_shutdown_idx);
    update_mode_dependency();
}

static lv_obj_t *create_row(lv_obj_t *parent, const char *name)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 80);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, name);
    return row;
}

static lv_obj_t *create_dropdown(lv_obj_t *row, const char *opts)
{
    lv_obj_t *dd = lv_dropdown_create(row);
    lv_obj_set_width(dd, lv_pct(48));
    lv_dropdown_set_options(dd, opts);
    return dd;
}

static lv_obj_t *create_switch(lv_obj_t *row)
{
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_style_transform_zoom(sw, 150, LV_PART_MAIN);
    return sw;
}

static void mode_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    factory_display_set_color_mode(lv_dropdown_get_selected(s_mode_dd) == 1);
    update_mode_dependency();
}

static void rotation_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    static const uint16_t kRotations[] = {0, 90, 180, 270};
    int selected = lv_dropdown_get_selected(s_rotation_dd);
    if (selected < 0 || selected > 3) {
        selected = 0;
    }
    factory_display_set_rotation(kRotations[selected]);
}

static void mirror_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int selected = lv_dropdown_get_selected(s_mirror_dd);
    if (selected < 0 || selected > 3) {
        selected = 0;
    }
    factory_display_set_mirror((uint8_t)selected);
}

static void passes_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    uint8_t partial = (uint8_t)(lv_dropdown_get_selected(s_partial_dd) + 1);
    uint8_t full = (uint8_t)(lv_dropdown_get_selected(s_full_dd) + 1);
    factory_display_set_passes(partial, full);
}

static void dither_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    factory_display_set_dither(lv_obj_has_state(s_dither_sw, LV_STATE_CHECKED));
}

static void low_flash_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    factory_display_set_low_flash(lv_obj_has_state(s_low_flash_sw, LV_STATE_CHECKED));
}

static void auto_full_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int selected = lv_dropdown_get_selected(s_auto_full_dd);
    if (selected < 0 || selected >= (int)(sizeof(kAutoFullOptions) / sizeof(kAutoFullOptions[0]))) {
        selected = 0;
    }
    factory_display_set_max_partial_refreshes_before_full(kAutoFullOptions[selected]);
}

static void auto_shutdown_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int selected = lv_dropdown_get_selected(s_auto_shutdown_dd);
    if (selected < 0 || selected >= (int)(sizeof(kAutoShutdownOptions) / sizeof(kAutoShutdownOptions[0]))) {
        selected = 0;
    }
    factory_ui_set_inactivity_shutdown_minutes(kAutoShutdownOptions[selected]);
}

static void full_refresh_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        factory_display_request_full_refresh();
    }
}

static void create_adjust(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Adjust");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    lv_obj_t *summary = factory_ui_create_info_label(panel, "Switch between the current 1bpp refresh path and the demo-style 4bpp grayscale path, then tune the mode-specific controls below. Touch inactivity auto shutdown is also configured here.");
    lv_obj_set_width(summary, lv_pct(100));

    lv_obj_t *row_mode = create_row(panel, "Color mode");
    s_mode_dd = create_dropdown(row_mode, "1bpp\n4bpp");

    lv_obj_t *row_rotation = create_row(panel, "Rotation");
    s_rotation_dd = create_dropdown(row_rotation, "0\n90\n180\n270");

    lv_obj_t *row_mirror = create_row(panel, "Mirror");
    s_mirror_dd = create_dropdown(row_mirror, "normal\nmirror X\nmirror Y\nmirror X+Y");

    lv_obj_t *row_partial = create_row(panel, "Partial passes");
    s_partial_dd = create_dropdown(row_partial, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15");

    lv_obj_t *row_full = create_row(panel, "Full passes");
    s_full_dd = create_dropdown(row_full, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15");

    lv_obj_t *row_dither = create_row(panel, "Ordered dither");
    s_dither_sw = create_switch(row_dither);

    lv_obj_t *row_low_flash = create_row(panel, "Low flash");
    s_low_flash_sw = create_switch(row_low_flash);

    lv_obj_t *row_auto_full = create_row(panel, "Full after partials");
    s_auto_full_dd = create_dropdown(row_auto_full, "Disable\n5\n10\n15\n20\n25\n30");

    lv_obj_t *row_auto_shutdown = create_row(panel, "Auto shutdown");
    s_auto_shutdown_dd = create_dropdown(row_auto_shutdown, "Disable\n1 min\n5 min\n10 min\n30 min");

    lv_obj_t *btn = factory_ui_create_action_button(panel, "Run Full Refresh", full_refresh_btn_event_cb, nullptr);
    lv_obj_set_width(btn, 280);

    lv_obj_add_event_cb(s_mode_dd, mode_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_rotation_dd, rotation_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_mirror_dd, mirror_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_partial_dd, passes_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_full_dd, passes_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_dither_sw, dither_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_low_flash_sw, low_flash_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_auto_full_dd, auto_full_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_auto_shutdown_dd, auto_shutdown_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    sync_adjust_widgets();
}

static void entry_adjust(void)
{
    sync_adjust_widgets();
}

static void exit_adjust(void) {}

static void destroy_adjust(void)
{
    s_rotation_dd = nullptr;
    s_mirror_dd = nullptr;
    s_partial_dd = nullptr;
    s_full_dd = nullptr;
    s_dither_sw = nullptr;
    s_mode_dd = nullptr;
    s_low_flash_sw = nullptr;
    s_auto_full_dd = nullptr;
    s_auto_shutdown_dd = nullptr;
}

static scr_lifecycle_t s_adjust_lifecycle = {
    .create = create_adjust,
    .entry = entry_adjust,
    .exit = exit_adjust,
    .destroy = destroy_adjust,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_adjust_lifecycle(void)
{
    return &s_adjust_lifecycle;
}
