#include "ui_screens.h"

#include "factory_assets.h"
#include "factory_wifi.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

static lv_obj_t *s_state_label = nullptr;
static lv_obj_t *s_summary_label = nullptr;
static lv_obj_t *s_scan_list = nullptr;
static lv_obj_t *s_scan_btn_label = nullptr;
static lv_timer_t *s_scan_timer = nullptr;

static void refresh_scan_button()
{
    if (s_scan_btn_label == nullptr) {
        return;
    }

    lv_label_set_text(s_scan_btn_label, factory_wifi_scan_busy() ? "Scanning..." : "Rescan");
}

static void refresh_scan_list()
{
    if (s_scan_list == nullptr) {
        return;
    }

    lv_obj_clean(s_scan_list);

    const int count = factory_wifi_get_scan_count();
    if (count <= 0) {
        lv_obj_t *empty = lv_label_create(s_scan_list);
        lv_obj_set_width(empty, lv_pct(100));
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_label_set_text(empty, factory_wifi_scan_busy() ? "Scanning..." : "No scan results yet.");
        lv_obj_set_style_text_color(empty, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_font(empty, FACTORY_FONT_BODY, LV_PART_MAIN);
        return;
    }

    for (int i = 0; i < count; ++i) {
        lv_obj_t *item = lv_obj_class_create_obj(&lv_list_btn_class, s_scan_list);
        lv_obj_class_init_obj(item);
        lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_height(item, 54);
        lv_obj_set_style_bg_color(item, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_color(item, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(item, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(item, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(item, 0, LV_PART_MAIN);

        lv_obj_t *label = lv_label_create(item);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, factory_wifi_get_scan_item(i));
        lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, FACTORY_FONT_BODY, LV_PART_MAIN);
        lv_obj_center(label);
    }
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

static void scan_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    factory_wifi_scan_poll();
    refresh_wifi_ui();
}

static void scan_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    factory_wifi_scan_start();
    refresh_wifi_ui();
}

static void create_wifi(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "WiFi");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 82);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_state_label = lv_label_create(panel);
    lv_obj_set_width(s_state_label, lv_pct(100));
    lv_label_set_long_mode(s_state_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_state_label, factory_wifi_get_state_text());
    lv_obj_set_style_text_font(s_state_label, FACTORY_FONT_TITLE, LV_PART_MAIN);
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

    lv_obj_t *scan_btn = factory_ui_create_action_button(btn_row, "Scan WiFi", scan_btn_event_cb, nullptr);
    lv_obj_set_width(scan_btn, lv_pct(42));
    s_scan_btn_label = lv_obj_get_child(scan_btn, 0);

    refresh_wifi_ui();
}

static void entry_wifi(void)
{
    factory_wifi_scan_start();
    refresh_wifi_ui();
    s_scan_timer = lv_timer_create(scan_timer_cb, 5000, nullptr);
}

static void exit_wifi(void)
{
    if (s_scan_timer != nullptr) {
        lv_timer_del(s_scan_timer);
        s_scan_timer = nullptr;
    }
}

static void destroy_wifi(void)
{
    s_state_label = nullptr;
    s_summary_label = nullptr;
    s_scan_list = nullptr;
    s_scan_btn_label = nullptr;
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
