#include "ui_screens.h"

#include "factory_port.h"
#include "lvgl.h"
#include "scr_mrg.h"
#include "ui_theme.h"

namespace {

static const struct {
    const char *label;
    factory_page_id_t page_id;
} kMenuItems[] = {
    {"Display", FACTORY_PAGE_DISPLAY},
    {"Touch", FACTORY_PAGE_TOUCH},
    {"Battery", FACTORY_PAGE_BATTERY},
    {"WiFi", FACTORY_PAGE_WIFI},
    {"SD", FACTORY_PAGE_SD},
    {"GPS", FACTORY_PAGE_GPS},
    {"LoRa", FACTORY_PAGE_LORA},
    {"Device", FACTORY_PAGE_DEVICE},
};

static void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_page_id_t page_id = (factory_page_id_t)(intptr_t)lv_event_get_user_data(e);
    scr_mgr_push((int)page_id, false);
}

static void create_home(lv_obj_t *parent)
{
    const factory_runtime_info_t *runtime = factory_port_get_runtime_info();

    factory_ui_apply_screen(parent);
    factory_ui_create_title(parent, "Factory");

    lv_obj_t *subtitle = factory_ui_create_subtitle(parent, "Display and touch are live. Other entries are reserved mock endpoints for later hardware bring-up.");
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 28, 72);

    lv_obj_t *grid = factory_ui_create_content_panel(parent, 94, 78);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 14, LV_PART_MAIN);

    for (size_t i = 0; i < sizeof(kMenuItems) / sizeof(kMenuItems[0]); ++i) {
        lv_obj_t *btn = factory_ui_create_action_button(grid, kMenuItems[i].label, menu_btn_event_cb, (void *)(intptr_t)kMenuItems[i].page_id);
        lv_obj_set_size(btn, lv_pct(48), 96);
    }

    lv_obj_t *footer = lv_label_create(parent);
    lv_label_set_text_fmt(footer, "%s  |  %ux%u  |  %s", runtime->target, runtime->width, runtime->height, runtime->touch_status);
    lv_obj_set_style_text_color(footer, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void entry_home(void) {}
static void exit_home(void) {}
static void destroy_home(void) {}

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
