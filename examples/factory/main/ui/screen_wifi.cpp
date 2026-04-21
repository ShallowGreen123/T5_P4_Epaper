#include "ui_screens.h"

#include <cstring>

#include "factory_assets.h"
#include "factory_wifi.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

constexpr lv_coord_t kWifiItemHeight = 60;
constexpr uint32_t kWifiRefreshMs = 300;

static lv_obj_t *s_root = nullptr;
static lv_obj_t *s_state_label = nullptr;
static lv_obj_t *s_summary_label = nullptr;
static lv_obj_t *s_scan_list = nullptr;
static lv_obj_t *s_scan_btn = nullptr;
static lv_obj_t *s_scan_btn_label = nullptr;
static lv_obj_t *s_password_overlay = nullptr;
static lv_obj_t *s_password_card = nullptr;
static lv_obj_t *s_password_title = nullptr;
static lv_obj_t *s_password_hint = nullptr;
static lv_obj_t *s_password_textarea = nullptr;
static lv_obj_t *s_password_keyboard = nullptr;
static lv_timer_t *s_scan_timer = nullptr;
static bool s_scan_list_dirty = true;
static int s_last_scan_count = -1;
static int s_last_selected_index = -2;
static bool s_last_scan_busy = false;
static bool s_last_scan_started = false;
static bool s_last_connecting = false;

static void refresh_scan_list();

static void invalidate_scan_list()
{
    s_scan_list_dirty = true;
}

static bool password_prompt_visible()
{
    return s_password_overlay != nullptr && !lv_obj_has_flag(s_password_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_scan_button()
{
    if (s_scan_btn == nullptr || s_scan_btn_label == nullptr) {
        return;
    }

    lv_label_set_text(
        s_scan_btn_label,
        factory_wifi_has_scan_started() ? "Rescan" : "Scan WiFi");

    if (factory_wifi_scan_busy() || factory_wifi_is_connecting() || password_prompt_visible()) {
        lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
    }
}

static void hide_password_prompt(bool clear_text)
{
    if (s_password_overlay == nullptr) {
        return;
    }

    if (clear_text && s_password_textarea != nullptr) {
        lv_textarea_set_text(s_password_textarea, "");
    }

    if (s_password_keyboard != nullptr) {
        lv_keyboard_set_textarea(s_password_keyboard, nullptr);
    }

    lv_obj_add_flag(s_password_overlay, LV_OBJ_FLAG_HIDDEN);
    refresh_scan_button();
}

static void show_password_prompt()
{
    if (s_password_overlay == nullptr || s_password_title == nullptr || s_password_hint == nullptr ||
        s_password_textarea == nullptr || s_password_keyboard == nullptr || s_password_card == nullptr) {
        return;
    }

    lv_label_set_text_fmt(s_password_title, "Password: %s", factory_wifi_get_ssid());
    lv_label_set_text(
        s_password_hint,
        "Enter the WiFi password, then tap Connect or the keyboard OK key.");
    lv_textarea_set_text(s_password_textarea, "");
    lv_keyboard_set_textarea(s_password_keyboard, s_password_textarea);
    lv_obj_clear_flag(s_password_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_password_overlay);
    lv_obj_align(s_password_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_state(s_password_textarea, LV_STATE_FOCUSED);
    refresh_scan_button();
}

static void refresh_wifi_ui()
{
    if (s_state_label != nullptr) {
        lv_label_set_text(s_state_label, factory_wifi_get_state_text());
    }
    if (s_summary_label != nullptr) {
        lv_label_set_text(s_summary_label, factory_wifi_get_summary());
    }

    refresh_scan_list();
    refresh_scan_button();
}

static void try_connect_selected_with_password()
{
    if (s_password_textarea == nullptr || s_password_hint == nullptr) {
        return;
    }

    const char *password = lv_textarea_get_text(s_password_textarea);
    if (password == nullptr || std::strlen(password) == 0) {
        lv_label_set_text(s_password_hint, "Password cannot be empty.");
        return;
    }

    if (factory_wifi_connect_selected_with_password(password)) {
        hide_password_prompt(true);
        refresh_wifi_ui();
        return;
    }

    lv_label_set_text(s_password_hint, "Connection did not start. Check the password and try again.");
    refresh_wifi_ui();
}

static const char *get_selected_badge_text()
{
    if (factory_wifi_is_connecting()) {
        return "CONNECTING";
    }

    if (factory_wifi_selected_requires_password()) {
        return "PASSWORD";
    }

    return "SELECTED";
}

static void scan_item_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || factory_wifi_scan_busy() || factory_wifi_is_connecting() ||
        password_prompt_visible()) {
        return;
    }

    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    factory_wifi_select_item(index);
    invalidate_scan_list();

    if (factory_wifi_selected_requires_password()) {
        show_password_prompt();
    } else {
        factory_wifi_connect_selected();
    }

    refresh_wifi_ui();
}

static void password_textarea_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED && s_password_keyboard != nullptr && s_password_textarea != nullptr) {
        lv_keyboard_set_textarea(s_password_keyboard, s_password_textarea);
        return;
    }

    if (code == LV_EVENT_READY) {
        try_connect_selected_with_password();
        return;
    }

    if (code == LV_EVENT_CANCEL) {
        hide_password_prompt(true);
        refresh_wifi_ui();
    }
}

static void password_cancel_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    hide_password_prompt(true);
    refresh_wifi_ui();
}

static void password_connect_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    try_connect_selected_with_password();
}

static void refresh_scan_list()
{
    if (s_scan_list == nullptr) {
        return;
    }
    const int count = factory_wifi_get_scan_count();
    const int selected_index = factory_wifi_get_selected_index();
    const bool scan_busy = factory_wifi_scan_busy();
    const bool scan_started = factory_wifi_has_scan_started();
    const bool connecting = factory_wifi_is_connecting();

    if (!s_scan_list_dirty &&
        s_last_scan_count == count &&
        s_last_selected_index == selected_index &&
        s_last_scan_busy == scan_busy &&
        s_last_scan_started == scan_started &&
        s_last_connecting == connecting) {
        return;
    }

    s_scan_list_dirty = false;
    s_last_scan_count = count;
    s_last_selected_index = selected_index;
    s_last_scan_busy = scan_busy;
    s_last_scan_started = scan_started;
    s_last_connecting = connecting;

    lv_obj_clean(s_scan_list);

    if (count <= 0) {
        lv_obj_t *empty = lv_label_create(s_scan_list);
        lv_obj_set_width(empty, lv_pct(100));
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_label_set_text(
            empty,
            scan_busy
                ? "Scanning..."
                : (scan_started ? "No scan results yet." : "Tap Scan WiFi to start one scan."));
        lv_obj_set_style_text_color(empty, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_font(empty, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
        return;
    }

    for (int i = 0; i < count; ++i) {
        const bool selected = (i == selected_index);

        lv_obj_t *item = lv_obj_class_create_obj(&lv_list_btn_class, s_scan_list);
        lv_obj_class_init_obj(item);
        lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_height(item, kWifiItemHeight);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(item, selected ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(item, selected ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(item, selected ? 3 : 2, LV_PART_MAIN);
        lv_obj_set_style_outline_color(item, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_outline_width(item, selected ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(item, selected ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_radius(item, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(item, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(item, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_right(item, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_top(item, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(item, 8, LV_PART_MAIN);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(item, scan_item_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(item);
        lv_obj_set_width(label, selected ? lv_pct(70) : lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, factory_wifi_get_scan_item(i));
        lv_obj_set_style_text_color(label, selected ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);

        if (selected) {
            lv_obj_t *badge = lv_label_create(item);
            lv_label_set_text(badge, get_selected_badge_text());
            lv_obj_set_style_text_color(badge, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_font(badge, FACTORY_FONT_BODY, LV_PART_MAIN);
        }
    }
}

static void scan_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    factory_wifi_scan_poll();
    refresh_wifi_ui();
}

static void scan_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || password_prompt_visible()) {
        return;
    }

    if (!factory_wifi_scan_busy() && !factory_wifi_is_connecting()) {
        factory_wifi_scan_start();
        invalidate_scan_list();
    }
    refresh_wifi_ui();
}

static void create_password_prompt(lv_obj_t *parent)
{
    s_password_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_password_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_password_overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_password_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_password_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_password_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_password_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_password_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_password_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    s_password_keyboard = lv_keyboard_create(s_password_overlay);
    lv_obj_set_width(s_password_keyboard, lv_pct(100));
    lv_obj_set_height(s_password_keyboard, lv_pct(42));
    lv_obj_align(s_password_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(s_password_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_password_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_password_keyboard, LV_FONT_DEFAULT, LV_PART_ITEMS);
    lv_keyboard_set_mode(s_password_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

    s_password_card = lv_obj_create(s_password_overlay);
    lv_obj_set_size(s_password_card, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_align(s_password_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_password_card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_password_card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_password_card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_password_card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_password_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_password_card, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_password_card, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_password_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_password_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_password_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_password_title = lv_label_create(s_password_card);
    lv_obj_set_width(s_password_title, lv_pct(100));
    lv_label_set_long_mode(s_password_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_password_title, "Password");
    lv_obj_set_style_text_font(s_password_title, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_password_title, lv_color_black(), LV_PART_MAIN);

    s_password_hint = lv_label_create(s_password_card);
    lv_obj_set_width(s_password_hint, lv_pct(100));
    lv_label_set_long_mode(s_password_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_password_hint, "");
    lv_obj_set_style_text_font(s_password_hint, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_password_hint, lv_color_black(), LV_PART_MAIN);

    s_password_textarea = lv_textarea_create(s_password_card);
    lv_obj_set_width(s_password_textarea, lv_pct(100));
    lv_obj_set_height(s_password_textarea, 56);
    lv_textarea_set_one_line(s_password_textarea, true);
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_textarea_set_max_length(s_password_textarea, 64);
    lv_textarea_set_placeholder_text(s_password_textarea, "Enter WiFi password");
    lv_obj_add_event_cb(s_password_textarea, password_textarea_event_cb, LV_EVENT_ALL, nullptr);
    lv_obj_set_style_text_font(s_password_textarea, FACTORY_FONT_UI_WIFI_SUMMARY, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_password_textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_password_textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_password_textarea, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_password_textarea, 0, LV_PART_MAIN);

    lv_obj_t *btn_row = lv_obj_create(s_password_card);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(btn_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel_btn = factory_ui_create_action_button(btn_row, "Cancel", password_cancel_event_cb, nullptr);
    lv_obj_set_width(cancel_btn, lv_pct(48));

    lv_obj_t *connect_btn = factory_ui_create_action_button(btn_row, "Connect", password_connect_event_cb, nullptr);
    lv_obj_set_width(connect_btn, lv_pct(48));
}

static void create_wifi(lv_obj_t *parent)
{
    s_root = parent;
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "WiFi");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_state_label = lv_label_create(panel);
    lv_obj_set_width(s_state_label, lv_pct(100));
    lv_label_set_long_mode(s_state_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_state_label, factory_wifi_get_state_text());
    lv_obj_set_style_text_font(s_state_label, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_state_label, lv_color_black(), LV_PART_MAIN);

    s_summary_label = factory_ui_create_info_label(panel, factory_wifi_get_summary());
    lv_obj_set_width(s_summary_label, lv_pct(100));

    s_scan_list = lv_list_create(panel);
    lv_obj_set_width(s_scan_list, lv_pct(100));
    lv_obj_set_flex_grow(s_scan_list, 1);
    lv_obj_set_style_bg_color(s_scan_list, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_scan_list, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_scan_list, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_scan_list, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_scan_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_scan_list, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_scan_list, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_scan_list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *btn_row = lv_obj_create(panel);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(btn_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_scan_btn = factory_ui_create_action_button(btn_row, "Scan WiFi", scan_btn_event_cb, nullptr);
    lv_obj_set_width(s_scan_btn, lv_pct(42));
    s_scan_btn_label = lv_obj_get_child(s_scan_btn, 0);

    create_password_prompt(parent);
    refresh_wifi_ui();
}

static void entry_wifi(void)
{
    hide_password_prompt(true);
    invalidate_scan_list();
    refresh_wifi_ui();
    s_scan_timer = lv_timer_create(scan_timer_cb, kWifiRefreshMs, nullptr);
}

static void exit_wifi(void)
{
    if (s_scan_timer != nullptr) {
        lv_timer_del(s_scan_timer);
        s_scan_timer = nullptr;
    }

    hide_password_prompt(true);
}

static void destroy_wifi(void)
{
    s_root = nullptr;
    s_state_label = nullptr;
    s_summary_label = nullptr;
    s_scan_list = nullptr;
    s_scan_btn = nullptr;
    s_scan_btn_label = nullptr;
    s_password_overlay = nullptr;
    s_password_card = nullptr;
    s_password_title = nullptr;
    s_password_hint = nullptr;
    s_password_textarea = nullptr;
    s_password_keyboard = nullptr;
    s_scan_list_dirty = true;
    s_last_scan_count = -1;
    s_last_selected_index = -2;
    s_last_scan_busy = false;
    s_last_scan_started = false;
    s_last_connecting = false;
}

static scr_lifecycle_t s_wifi_lifecycle = {
    .create = create_wifi,
    .entry = entry_wifi,
    .exit = exit_wifi,
    .destroy = destroy_wifi,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_wifi_lifecycle(void)
{
    return &s_wifi_lifecycle;
}
