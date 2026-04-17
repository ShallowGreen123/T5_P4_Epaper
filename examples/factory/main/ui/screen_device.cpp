#include "ui_screens.h"

#include <stdio.h>

#include "factory_port.h"
#include "ui_theme.h"

namespace {

static void create_device(lv_obj_t *parent)
{
    const factory_runtime_info_t *runtime = factory_port_get_runtime_info();

    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Device");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 82);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_set_width(factory_ui_create_value_card(panel, "Application", runtime->app_name), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Version", runtime->app_version), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Target", runtime->target), lv_pct(100));

    char resolution_text[32];
    snprintf(resolution_text, sizeof(resolution_text), "%ux%u", runtime->width, runtime->height);
    lv_obj_set_width(factory_ui_create_value_card(panel, "Resolution", resolution_text), lv_pct(100));

    lv_obj_set_width(factory_ui_create_value_card(panel, "Display Mode", runtime->display_mode), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Touch Status", runtime->touch_status), lv_pct(100));
    lv_obj_set_width(factory_ui_create_value_card(panel, "Boot Target", runtime->boot_mode), lv_pct(100));
}

static void entry_device(void) {}
static void exit_device(void) {}
static void destroy_device(void) {}

static scr_lifecycle_t s_device_lifecycle = {
    .create = create_device,
    .entry = entry_device,
    .exit = exit_device,
    .destroy = destroy_device,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_device_lifecycle(void)
{
    return &s_device_lifecycle;
}
