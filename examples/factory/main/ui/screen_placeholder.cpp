#include "ui_screens.h"

#include "factory_port.h"
#include "ui_theme.h"

namespace {

static void create_placeholder_page(lv_obj_t *parent, factory_page_id_t page_id)
{
    const factory_placeholder_info_t *info = factory_port_get_placeholder_page(page_id);
    if (info == nullptr) {
        return;
    }

    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, info->title);

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 82);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *status_card = factory_ui_create_value_card(panel, "Status", info->status_text);
    lv_obj_set_width(status_card, lv_pct(100));

    lv_obj_t *summary = factory_ui_create_info_label(panel, info->summary);
    lv_obj_set_width(summary, lv_pct(100));

    lv_obj_t *detail = factory_ui_create_info_label(panel, info->detail);
    lv_obj_set_width(detail, lv_pct(100));
}

static void entry_placeholder(void) {}
static void exit_placeholder(void) {}
static void destroy_placeholder(void) {}

#define FACTORY_PLACEHOLDER_LIFECYCLE(name, page_id)                 \
    static void create_##name(lv_obj_t *parent)                      \
    {                                                                \
        create_placeholder_page(parent, page_id);                    \
    }                                                                \
    static scr_lifecycle_t s_##name##_lifecycle = {                  \
        .create = create_##name,                                     \
        .entry = entry_placeholder,                                  \
        .exit = exit_placeholder,                                    \
        .destroy = destroy_placeholder,                              \
    }

FACTORY_PLACEHOLDER_LIFECYCLE(battery, FACTORY_PAGE_BATTERY);
FACTORY_PLACEHOLDER_LIFECYCLE(wifi, FACTORY_PAGE_WIFI);
FACTORY_PLACEHOLDER_LIFECYCLE(sd, FACTORY_PAGE_SD);
FACTORY_PLACEHOLDER_LIFECYCLE(gps, FACTORY_PAGE_GPS);
FACTORY_PLACEHOLDER_LIFECYCLE(lora, FACTORY_PAGE_LORA);


}  // namespace

extern "C" scr_lifecycle_t *factory_placeholder_lifecycle(factory_page_id_t page_id)
{
    switch (page_id) {
        case FACTORY_PAGE_BATTERY:
            return &s_battery_lifecycle;
        case FACTORY_PAGE_WIFI:
            return &s_wifi_lifecycle;
        case FACTORY_PAGE_SD:
            return &s_sd_lifecycle;
        case FACTORY_PAGE_GPS:
            return &s_gps_lifecycle;
        case FACTORY_PAGE_LORA:
            return &s_lora_lifecycle;
        default:
            return nullptr;
    }
}
