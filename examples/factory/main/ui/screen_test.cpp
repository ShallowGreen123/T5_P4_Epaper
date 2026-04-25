#include "ui_screens.h"

#include "factory_port.h"
#include "scr_mrg.h"
#include "ui_theme.h"

namespace {

static void diag_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_page_id_t page_id = (factory_page_id_t)(intptr_t)lv_event_get_user_data(e);
    scr_mgr_push((int)page_id, false);
}

static void create_test(lv_obj_t *parent)
{
    const factory_runtime_info_t *runtime = factory_port_get_runtime_info();

    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Test");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *summary = factory_ui_create_info_label(panel, "Display and touch diagnostics are the live hardware pages in this migration build.");
    lv_obj_set_width(summary, lv_pct(100));

    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *display_btn = factory_ui_create_menu_tile(grid, LV_SYMBOL_IMAGE, "Display", diag_btn_event_cb, (void *)(intptr_t)FACTORY_PAGE_DISPLAY);
    lv_obj_set_size(display_btn, lv_pct(48), 180);

    lv_obj_t *touch_btn = factory_ui_create_menu_tile(grid, LV_SYMBOL_EYE_OPEN, "Touch", diag_btn_event_cb, (void *)(intptr_t)FACTORY_PAGE_TOUCH);
    lv_obj_set_size(touch_btn, lv_pct(48), 180);

    lv_obj_t *status = factory_ui_create_value_card(panel, "Touch ready", runtime->touch_ready ? "yes" : "no");
    lv_obj_set_width(status, lv_pct(100));
}

static void entry_test(void) {}
static void exit_test(void) {}
static void destroy_test(void) {}

static scr_lifecycle_t s_test_lifecycle = {
    .create = create_test,
    .entry = entry_test,
    .exit = exit_test,
    .destroy = destroy_test,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_test_lifecycle(void)
{
    return &s_test_lifecycle;
}
