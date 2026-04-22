#include "ui_screens.h"

#include <stdio.h>
#include <string.h>

#include "factory_assets.h"
#include "factory_sd.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

constexpr lv_coord_t kChromeHeight = 115;
constexpr lv_coord_t kToolbarHeight = 52;
constexpr lv_coord_t kAddressHeight = 42;
constexpr lv_coord_t kStatusbarHeight = 42;
constexpr lv_coord_t kColumnIconWidth = 44;
constexpr lv_coord_t kColumnTypeWidth = 180;
constexpr lv_coord_t kColumnSizeWidth = 210;
constexpr lv_coord_t kToolbarButtonHeight = 44;
constexpr lv_coord_t kToolbarButtonGap = 12;
constexpr lv_coord_t kToolbarButtonWidthUdisk = 176;
constexpr lv_coord_t kToolbarButtonWidthUp = 116;
constexpr lv_coord_t kToolbarButtonWidthRefresh = 148;
constexpr lv_coord_t kMinNameColumnWidth = 120;

static lv_obj_t *s_address_label = nullptr;
static lv_obj_t *s_entry_list = nullptr;
static lv_obj_t *s_header_row = nullptr;
static lv_obj_t *s_udisk_btn = nullptr;
static lv_obj_t *s_udisk_btn_label = nullptr;
static lv_obj_t *s_up_btn = nullptr;
static lv_obj_t *s_up_btn_label = nullptr;
static lv_obj_t *s_refresh_btn = nullptr;
static lv_obj_t *s_refresh_btn_label = nullptr;
static lv_obj_t *s_count_label = nullptr;
static lv_obj_t *s_hint_label = nullptr;
static lv_obj_t *s_detail_overlay = nullptr;
static lv_obj_t *s_detail_card = nullptr;
static lv_obj_t *s_detail_title = nullptr;
static lv_obj_t *s_detail_name = nullptr;
static lv_obj_t *s_detail_path = nullptr;
static lv_obj_t *s_detail_type = nullptr;
static lv_obj_t *s_detail_size = nullptr;

static void refresh_sd_ui(void);

static void style_flat_panel(lv_obj_t *obj, lv_coord_t radius, uint8_t border_width)
{
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, border_width, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
}

static void style_chrome_bar(lv_obj_t *obj)
{
    style_flat_panel(obj, 8, 2);
    lv_obj_set_style_pad_all(obj, 10, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_column_header(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_toolbar_button(lv_obj_t *btn)
{
    if (btn == nullptr) {
        return;
    }

    lv_obj_set_height(btn, kToolbarButtonHeight);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DISABLED);
}

static lv_coord_t clamp_name_width(lv_coord_t list_width)
{
    lv_coord_t name_width = list_width - kColumnIconWidth - kColumnTypeWidth - kColumnSizeWidth - 56;
    if (name_width < kMinNameColumnWidth) {
        name_width = kMinNameColumnWidth;
    }
    return name_width;
}

static void format_size_text(uint64_t size_bytes, bool is_directory, char *buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }

    if (is_directory) {
        snprintf(buffer, buffer_size, "--");
        return;
    }

    const double size = (double)size_bytes;
    if (size_bytes == 0ULL) {
        snprintf(buffer, buffer_size, "0 KB");
    } else if (size_bytes < 1024ULL) {
        const double size_kb = size / 1024.0;
        snprintf(buffer, buffer_size, "%.1f KB", size_kb < 0.1 ? 0.1 : size_kb);
    } else if (size_bytes < (1024ULL * 1024ULL)) {
        snprintf(buffer, buffer_size, "%.1f KB", size / 1024.0);
    } else if (size_bytes < (1024ULL * 1024ULL * 1024ULL)) {
        snprintf(buffer, buffer_size, "%.1f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

static void format_breadcrumb_text(const char *path, char *buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }

    if (path == nullptr || path[0] == '\0') {
        snprintf(buffer, buffer_size, "Path:");
        return;
    }

    snprintf(buffer, buffer_size, "Path:");

    const char *suffix = path;
    if (strncmp(path, "/sdcard", 7) == 0) {
        suffix = path + 7;
    }

    while (*suffix == '/') {
        ++suffix;
    }

    if (*suffix == '\0') {
        return;
    }

    char temp[FACTORY_SD_MAX_PATH_LEN] = {};
    snprintf(temp, sizeof(temp), "%s", suffix);

    char *context = nullptr;
    char *token = strtok_r(temp, "/", &context);
    while (token != nullptr) {
        const size_t used = strlen(buffer);
        if (used + 3 >= buffer_size) {
            break;
        }
        strncat(buffer, " > ", buffer_size - used - 1);

        const size_t used_after_sep = strlen(buffer);
        strncat(buffer, token, buffer_size - used_after_sep - 1);
        token = strtok_r(nullptr, "/", &context);
    }
}

static void hide_detail_overlay(void)
{
    if (s_detail_overlay == nullptr) {
        return;
    }

    lv_obj_add_flag(s_detail_overlay, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *create_detail_field(lv_obj_t *parent, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    style_flat_panel(card, 8, 2);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(title_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(card);
    lv_obj_set_width(value, lv_pct(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value, "");
    lv_obj_align_to(value, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_style_text_font(value, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, lv_color_black(), LV_PART_MAIN);

    if (value_label != nullptr) {
        *value_label = value;
    }

    return card;
}

static void show_detail_overlay(const factory_sd_entry_info_t *entry)
{
    if (entry == nullptr || s_detail_overlay == nullptr || s_detail_card == nullptr ||
        s_detail_title == nullptr || s_detail_name == nullptr || s_detail_path == nullptr || s_detail_type == nullptr ||
        s_detail_size == nullptr) {
        return;
    }

    char size_text[64] = {};
    format_size_text(entry->size_bytes, entry->is_directory, size_text, sizeof(size_text));

    lv_label_set_text_fmt(s_detail_title, "Properties - %s", entry->name);
    lv_label_set_text(s_detail_name, entry->name);
    lv_label_set_text(s_detail_path, entry->path);
    lv_label_set_text(s_detail_type, entry->type);
    lv_label_set_text(s_detail_size, size_text);
    lv_obj_clear_flag(s_detail_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_detail_overlay);
    lv_obj_align(s_detail_card, LV_ALIGN_CENTER, 0, 0);
}

static void create_empty_state_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
}

static void create_table_header(void)
{
    if (s_header_row == nullptr) {
        return;
    }

    lv_obj_clean(s_header_row);
    lv_obj_update_layout(s_header_row);
    const lv_coord_t row_width = lv_obj_get_content_width(s_header_row);
    const lv_coord_t name_width = clamp_name_width(row_width);

    lv_obj_t *icon_spacer = lv_label_create(s_header_row);
    lv_label_set_text(icon_spacer, "");
    lv_obj_set_width(icon_spacer, kColumnIconWidth);

    lv_obj_t *name_label = lv_label_create(s_header_row);
    lv_label_set_text(name_label, "Name");
    lv_obj_set_width(name_label, name_width);
    lv_obj_set_style_text_font(name_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *type_label = lv_label_create(s_header_row);
    lv_label_set_text(type_label, "Type");
    lv_obj_set_width(type_label, kColumnTypeWidth);
    lv_obj_set_style_text_font(type_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(type_label, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *size_label = lv_label_create(s_header_row);
    lv_label_set_text(size_label, "Size");
    lv_obj_set_width(size_label, kColumnSizeWidth);
    lv_obj_set_style_text_align(size_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(size_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(size_label, lv_color_white(), LV_PART_MAIN);
}

static void rebuild_entry_list(void)
{
    if (s_entry_list == nullptr) {
        return;
    }

    lv_obj_clean(s_entry_list);

    const factory_sd_state_t *state = factory_sd_get_state();
    const size_t entry_count = factory_sd_get_entry_count();
    lv_obj_update_layout(s_entry_list);
    const lv_coord_t row_width = lv_obj_get_content_width(s_entry_list);
    const lv_coord_t name_width = clamp_name_width(row_width);

    if (!state->mounted) {
        create_empty_state_label(s_entry_list, "Insert an SD card, then click Retry to mount and browse /sdcard.");
        return;
    }

    if (entry_count == 0) {
        create_empty_state_label(s_entry_list, "This folder is empty.");
        return;
    }

    for (size_t i = 0; i < entry_count; ++i) {
        const factory_sd_entry_info_t *entry = factory_sd_get_entry(i);
        if (entry == nullptr) {
            continue;
        }

        lv_obj_t *item = lv_btn_create(s_entry_list);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, 50);
        style_flat_panel(item, 6, 1);
        lv_obj_set_style_pad_left(item, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(item, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_top(item, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(item, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_column(item, 8, LV_PART_MAIN);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(
            item,
            [](lv_event_t *e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                    return;
                }

                const size_t index = (size_t)(intptr_t)lv_event_get_user_data(e);
                const factory_sd_entry_info_t *clicked_entry = factory_sd_get_entry(index);
                if (clicked_entry == nullptr) {
                    refresh_sd_ui();
                    return;
                }

                if (clicked_entry->is_directory) {
                    factory_sd_open_entry(index);
                    refresh_sd_ui();
                    return;
                }

                show_detail_overlay(clicked_entry);
            },
            LV_EVENT_CLICKED,
            (void *)(intptr_t)i);

        lv_obj_t *icon_label = lv_label_create(item);
        lv_label_set_text(icon_label, entry->is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_width(icon_label, kColumnIconWidth);
        lv_obj_set_style_text_font(icon_label, FACTORY_FONT_SYMBOL, LV_PART_MAIN);
        lv_obj_set_style_text_color(icon_label, lv_color_black(), LV_PART_MAIN);

        char meta_text[96] = {};
        format_size_text(entry->size_bytes, entry->is_directory, meta_text, sizeof(meta_text));

        lv_obj_t *name_label = lv_label_create(item);
        lv_obj_set_width(name_label, name_width);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(name_label, entry->name);
        lv_obj_set_style_text_font(name_label, FACTORY_FONT_BODY, LV_PART_MAIN);
        lv_obj_set_style_text_color(name_label, lv_color_black(), LV_PART_MAIN);

        lv_obj_t *type_label = lv_label_create(item);
        lv_obj_set_width(type_label, kColumnTypeWidth);
        lv_label_set_long_mode(type_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(type_label, entry->type);
        lv_obj_set_style_text_font(type_label, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
        lv_obj_set_style_text_color(type_label, lv_color_black(), LV_PART_MAIN);

        lv_obj_t *size_label = lv_label_create(item);
        lv_obj_set_width(size_label, kColumnSizeWidth);
        lv_label_set_long_mode(size_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(size_label, meta_text);
        lv_obj_set_style_text_align(size_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_font(size_label, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
        lv_obj_set_style_text_color(size_label, lv_color_black(), LV_PART_MAIN);
    }
}

static void refresh_action_buttons(void)
{
    const factory_sd_state_t *state = factory_sd_get_state();

    if (s_udisk_btn != nullptr) {
        lv_obj_clear_state(s_udisk_btn, LV_STATE_DISABLED);
    }

    if (s_up_btn != nullptr) {
        if (!state->mounted || state->at_root) {
            lv_obj_add_state(s_up_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_up_btn, LV_STATE_DISABLED);
        }
    }

    if (s_up_btn_label != nullptr) {
        lv_label_set_text(s_up_btn_label, "Up");
    }

    if (s_refresh_btn != nullptr && s_refresh_btn_label != nullptr) {
        lv_label_set_text(s_refresh_btn_label, state->mounted ? "Refresh" : "Retry");
    }
}

static void refresh_sd_ui(void)
{
    const factory_sd_state_t *state = factory_sd_get_state();

    char breadcrumb[FACTORY_SD_MAX_PATH_LEN + 64] = {};
    format_breadcrumb_text(state->current_path, breadcrumb, sizeof(breadcrumb));

    if (s_address_label != nullptr) {
        lv_label_set_text(s_address_label, breadcrumb);
    }

    if (s_count_label != nullptr) {
        lv_label_set_text_fmt(s_count_label, "%u item%s", (unsigned)state->entry_count, state->entry_count == 1 ? "" : "s");
    }

    if (s_hint_label != nullptr) {
        lv_label_set_text(s_hint_label, state->status_text);
    }

    refresh_action_buttons();
    create_table_header();
    rebuild_entry_list();
}

static void up_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    factory_sd_go_parent();
    refresh_sd_ui();
}

static void refresh_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    factory_sd_refresh();
    refresh_sd_ui();
}

static void detail_close_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    hide_detail_overlay();
}

static void create_detail_overlay(lv_obj_t *parent)
{
    s_detail_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_detail_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_detail_overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_detail_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_detail_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_detail_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_detail_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_detail_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_detail_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    s_detail_card = lv_obj_create(s_detail_overlay);
    lv_obj_set_size(s_detail_card, lv_pct(78), LV_SIZE_CONTENT);
    lv_obj_align(s_detail_card, LV_ALIGN_CENTER, 0, 0);
    style_flat_panel(s_detail_card, 10, 2);
    lv_obj_set_style_pad_all(s_detail_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_detail_card, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_detail_card, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_detail_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_detail_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_detail_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title_bar = lv_obj_create(s_detail_card);
    lv_obj_set_width(title_bar, lv_pct(100));
    lv_obj_set_height(title_bar, 46);
    lv_obj_set_style_bg_color(title_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(title_bar, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(title_bar, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(title_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(title_bar, 8, LV_PART_MAIN);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_detail_title = lv_label_create(title_bar);
    lv_label_set_text(s_detail_title, "Properties");
    lv_obj_center(s_detail_title);
    lv_obj_set_style_text_font(s_detail_title, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_detail_title, lv_color_white(), LV_PART_MAIN);

    create_detail_field(s_detail_card, "Name", &s_detail_name);
    create_detail_field(s_detail_card, "Path", &s_detail_path);
    create_detail_field(s_detail_card, "Type", &s_detail_type);
    create_detail_field(s_detail_card, "Size", &s_detail_size);
    lv_label_set_long_mode(s_detail_size, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_detail_size, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    lv_obj_t *close_btn = factory_ui_create_action_button(s_detail_card, "Close", detail_close_event_cb, nullptr);
    lv_obj_set_width(close_btn, lv_pct(100));
}

static void create_sd(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "SD Explorer");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 96, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 10, LV_PART_MAIN);

    lv_obj_t *chrome = lv_obj_create(panel);
    lv_obj_set_width(chrome, lv_pct(100));
    lv_obj_set_height(chrome, kChromeHeight);
    style_chrome_bar(chrome);
    lv_obj_set_style_pad_row(chrome, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(chrome, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(chrome, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(chrome, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chrome, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *toolbar_row = lv_obj_create(chrome);
    lv_obj_set_width(toolbar_row, lv_pct(100));
    lv_obj_set_height(toolbar_row, kToolbarHeight);
    lv_obj_set_style_bg_opa(toolbar_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(toolbar_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(toolbar_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(toolbar_row, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(toolbar_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(toolbar_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(toolbar_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *toolbar_btn_row = lv_obj_create(toolbar_row);
    lv_obj_set_width(toolbar_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_height(toolbar_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(toolbar_btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(toolbar_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(toolbar_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(toolbar_btn_row, kToolbarButtonGap, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(toolbar_btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(toolbar_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(toolbar_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar_btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_udisk_btn = factory_ui_create_action_button(toolbar_btn_row, "U disk off", nullptr, nullptr);
    lv_obj_set_width(s_udisk_btn, kToolbarButtonWidthUdisk);
    style_toolbar_button(s_udisk_btn);
    lv_obj_clear_flag(s_udisk_btn, LV_OBJ_FLAG_CLICKABLE);
    s_udisk_btn_label = lv_obj_get_child(s_udisk_btn, 0);

    s_up_btn = factory_ui_create_action_button(toolbar_btn_row, "Up", up_btn_event_cb, nullptr);
    lv_obj_set_width(s_up_btn, kToolbarButtonWidthUp);
    style_toolbar_button(s_up_btn);
    s_up_btn_label = lv_obj_get_child(s_up_btn, 0);

    s_refresh_btn = factory_ui_create_action_button(toolbar_btn_row, "Refresh", refresh_btn_event_cb, nullptr);
    lv_obj_set_width(s_refresh_btn, kToolbarButtonWidthRefresh);
    style_toolbar_button(s_refresh_btn);
    s_refresh_btn_label = lv_obj_get_child(s_refresh_btn, 0);

    lv_obj_t *address_row = lv_obj_create(chrome);
    lv_obj_set_width(address_row, lv_pct(100));
    lv_obj_set_height(address_row, kAddressHeight);
    style_flat_panel(address_row, 8, 2);
    lv_obj_set_style_pad_left(address_row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(address_row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(address_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(address_row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(address_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(address_row, LV_OBJ_FLAG_SCROLLABLE);

    s_address_label = lv_label_create(address_row);
    lv_obj_set_width(s_address_label, lv_pct(100));
    lv_label_set_long_mode(s_address_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_address_label, "Path:");
    lv_obj_center(s_address_label);
    lv_obj_set_style_text_font(s_address_label, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_address_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *body = lv_obj_create(panel);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(body, 0, LV_PART_MAIN);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *main_pane = lv_obj_create(body);
    lv_obj_set_width(main_pane, lv_pct(100));
    lv_obj_set_height(main_pane, lv_pct(100));
    style_flat_panel(main_pane, 10, 2);
    lv_obj_set_style_pad_all(main_pane, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(main_pane, 10, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(main_pane, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(main_pane, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_pane, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_header_row = lv_obj_create(main_pane);
    lv_obj_set_width(s_header_row, lv_pct(100));
    lv_obj_set_height(s_header_row, 42);
    style_column_header(s_header_row);
    lv_obj_set_style_pad_column(s_header_row, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_header_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_entry_list = lv_obj_create(main_pane);
    lv_obj_set_width(s_entry_list, lv_pct(100));
    lv_obj_set_flex_grow(s_entry_list, 1);
    style_flat_panel(s_entry_list, 8, 2);
    lv_obj_set_style_pad_all(s_entry_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_entry_list, 8, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_entry_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_entry_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_entry_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *status_bar = lv_obj_create(panel);
    lv_obj_set_width(status_bar, lv_pct(100));
    lv_obj_set_height(status_bar, kStatusbarHeight);
    style_chrome_bar(status_bar);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_count_label = lv_label_create(status_bar);
    lv_label_set_text(s_count_label, "0 items");
    lv_obj_set_style_text_font(s_count_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_count_label, lv_color_black(), LV_PART_MAIN);

    s_hint_label = lv_label_create(status_bar);
    lv_obj_set_width(s_hint_label, lv_pct(65));
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_hint_label, "SD page ready.");
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_hint_label, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_hint_label, lv_color_black(), LV_PART_MAIN);

    create_detail_overlay(parent);
    refresh_sd_ui();
}

static void entry_sd(void)
{
    hide_detail_overlay();
    factory_sd_enter_root();
    refresh_sd_ui();
}

static void exit_sd(void)
{
    hide_detail_overlay();
    factory_sd_leave();
}

static void destroy_sd(void)
{
    s_address_label = nullptr;
    s_entry_list = nullptr;
    s_header_row = nullptr;
    s_udisk_btn = nullptr;
    s_udisk_btn_label = nullptr;
    s_up_btn = nullptr;
    s_up_btn_label = nullptr;
    s_refresh_btn = nullptr;
    s_refresh_btn_label = nullptr;
    s_count_label = nullptr;
    s_hint_label = nullptr;
    s_detail_overlay = nullptr;
    s_detail_card = nullptr;
    s_detail_title = nullptr;
    s_detail_name = nullptr;
    s_detail_path = nullptr;
    s_detail_type = nullptr;
    s_detail_size = nullptr;
}

static scr_lifecycle_t s_sd_lifecycle = {
    .create = create_sd,
    .entry = entry_sd,
    .exit = exit_sd,
    .destroy = destroy_sd,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_sd_lifecycle(void)
{
    return &s_sd_lifecycle;
}
