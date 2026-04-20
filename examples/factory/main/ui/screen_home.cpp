#include "ui_screens.h"

#include "factory_assets.h"
#include "factory_port.h"
#include "lvgl.h"
#include "scr_mrg.h"
#include "ui_theme.h"

namespace {

struct MenuItem {
    const char *symbol;
    const char *title;
    factory_page_id_t page_id;
    uint8_t page;
};

static const MenuItem kMenuItems[] = {
    {LV_SYMBOL_BELL, "Clock", FACTORY_PAGE_CLOCK, 0},
    {LV_SYMBOL_BLUETOOTH, "LoRa", FACTORY_PAGE_LORA, 0},
    {LV_SYMBOL_SD_CARD, "SD", FACTORY_PAGE_SD, 0},
    {LV_SYMBOL_SETTINGS, "Setting", FACTORY_PAGE_SETTING, 0},
    {LV_SYMBOL_EYE_OPEN, "Test", FACTORY_PAGE_TEST, 0},
    {LV_SYMBOL_WIFI, "WiFi", FACTORY_PAGE_WIFI, 0},
    {LV_SYMBOL_BATTERY_FULL, "Battery", FACTORY_PAGE_BATTERY, 0},
    {LV_SYMBOL_GPS, "GPS", FACTORY_PAGE_GPS, 0},
    {LV_SYMBOL_REFRESH, "Adjust", FACTORY_PAGE_ADJUST, 0},
    {LV_SYMBOL_POWER, "Shutdown", FACTORY_PAGE_SHUTDOWN, 1},
    {LV_SYMBOL_PAUSE, "Sleep", FACTORY_PAGE_SLEEP, 1},
};

static lv_obj_t *s_pages[2] = {};
static lv_obj_t *s_dots[2] = {};
static lv_obj_t *s_prev_btn = nullptr;
static lv_obj_t *s_next_btn = nullptr;
static uint8_t s_page_index = 0;

static void apply_page_visibility()
{
    for (uint8_t i = 0; i < 2; ++i) {
        if (s_pages[i] == nullptr) {
            continue;
        }
        if (i == s_page_index) {
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_black(), LV_PART_MAIN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_white(), LV_PART_MAIN);
        }
    }

    if (s_prev_btn != nullptr) {
        if (s_page_index == 0) {
            lv_obj_add_state(s_prev_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_prev_btn, LV_STATE_DISABLED);
        }
    }
    if (s_next_btn != nullptr) {
        if (s_page_index >= 1) {
            lv_obj_add_state(s_next_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_next_btn, LV_STATE_DISABLED);
        }
    }
}

static void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_page_id_t page_id = (factory_page_id_t)(intptr_t)lv_event_get_user_data(e);
    scr_mgr_push((int)page_id, false);
}

static void nav_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    int next_page = (int)s_page_index + (int)delta;
    if (next_page < 0) {
        next_page = 0;
    }
    if (next_page > 1) {
        next_page = 1;
    }
    s_page_index = (uint8_t)next_page;
    apply_page_visibility();
}

static void create_page_items(lv_obj_t *parent, uint8_t page)
{
    for (size_t i = 0; i < sizeof(kMenuItems) / sizeof(kMenuItems[0]); ++i) {
        if (kMenuItems[i].page != page) {
            continue;
        }

        lv_obj_t *tile = factory_ui_create_menu_tile(parent, kMenuItems[i].symbol, kMenuItems[i].title, menu_btn_event_cb, (void *)(intptr_t)kMenuItems[i].page_id);
        lv_obj_set_size(tile, page == 0 ? lv_pct(31) : lv_pct(46), 150);
    }
}

static void create_home(lv_obj_t *parent)
{
    const factory_runtime_info_t *runtime = factory_port_get_runtime_info();

    factory_ui_apply_screen(parent);

    lv_obj_t *badge = lv_img_create(parent);
    lv_img_set_src(badge, &img_factory_badge);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 34, 30);

    lv_obj_t *title = factory_ui_create_title(parent, "Factory");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 74, 26);

    lv_obj_t *subtitle = factory_ui_create_subtitle(parent, "FastEPD 1bpp factory shell. Menu flow follows the demo layout while refresh speed stays prioritized.");
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 36, 74);

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 76);
    lv_obj_set_style_pad_bottom(panel, 72, LV_PART_MAIN);

    for (uint8_t page = 0; page < 2; ++page) {
        s_pages[page] = lv_obj_create(panel);
        lv_obj_set_size(s_pages[page], lv_pct(100), lv_pct(100));
        lv_obj_set_style_border_width(s_pages[page], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_pages[page], 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_pages[page], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_pages[page], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(s_pages[page], LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(s_pages[page], LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(s_pages[page], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(s_pages[page], 14, LV_PART_MAIN);
        lv_obj_set_style_pad_column(s_pages[page], 14, LV_PART_MAIN);
        create_page_items(s_pages[page], page);
    }

    s_prev_btn = factory_ui_create_action_button(panel, LV_SYMBOL_LEFT, nav_btn_event_cb, (void *)-1);
    lv_obj_set_size(s_prev_btn, 68, 48);
    lv_obj_align(s_prev_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_next_btn = factory_ui_create_action_button(panel, LV_SYMBOL_RIGHT, nav_btn_event_cb, (void *)1);
    lv_obj_set_size(s_next_btn, 68, 48);
    lv_obj_align(s_next_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *dot_wrap = lv_obj_create(panel);
    lv_obj_set_size(dot_wrap, 120, 28);
    lv_obj_align(dot_wrap, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(dot_wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot_wrap, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot_wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dot_wrap, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(dot_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(dot_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_wrap, 12, LV_PART_MAIN);

    for (uint8_t i = 0; i < 2; ++i) {
        s_dots[i] = lv_obj_create(dot_wrap);
        lv_obj_set_size(s_dots[i], 16, 16);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_dots[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dots[i], 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_dots[i], 0, LV_PART_MAIN);
    }

    lv_obj_t *footer = lv_label_create(parent);
    lv_label_set_text_fmt(footer, "%s | %ux%u | %s", runtime->target, runtime->width, runtime->height, runtime->touch_status);
    lv_obj_set_style_text_color(footer, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_page_index = 0;
    apply_page_visibility();
}

static void entry_home(void) {}
static void exit_home(void) {}

static void destroy_home(void)
{
    for (uint8_t i = 0; i < 2; ++i) {
        s_pages[i] = nullptr;
        s_dots[i] = nullptr;
    }
    s_prev_btn = nullptr;
    s_next_btn = nullptr;
    s_page_index = 0;
}

static scr_lifecycle_t s_home_lifecycle = {
    .create = create_home,
    .entry = entry_home,
    .exit = exit_home,
    .destroy = destroy_home,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_home_lifecycle(void)
{
    return &s_home_lifecycle;
}
