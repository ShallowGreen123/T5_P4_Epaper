#include "ui_screens.h"

#include <stdio.h>
#include <string.h>

#include "factory_assets.h"
#include "factory_usb_otg.h"
#include "ui_theme.h"

namespace {

constexpr uint32_t kRefreshPeriodMs = 500U;

static lv_obj_t *s_host_button = nullptr;
static lv_obj_t *s_device_button = nullptr;
static lv_obj_t *s_result_label = nullptr;
static lv_obj_t *s_message_label = nullptr;
static lv_obj_t *s_role_label = nullptr;
static lv_obj_t *s_vbus_label = nullptr;
static lv_obj_t *s_peer_label = nullptr;
static lv_obj_t *s_descriptor_label = nullptr;
static lv_timer_t *s_refresh_timer = nullptr;
static factory_usb_role_t s_selected_role = FACTORY_USB_ROLE_HOST;

static void set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == nullptr || text == nullptr) {
        return;
    }

    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void style_transparent_row(lv_obj_t *row)
{
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_readout(lv_obj_t *parent, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    style_transparent_row(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_label = lv_label_create(row);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_black(), LV_PART_MAIN);

    *value_label = lv_label_create(row);
    lv_obj_set_width(*value_label, lv_pct(68));
    lv_label_set_long_mode(*value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(*value_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(*value_label, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(*value_label, "--");
    return row;
}

static const char *usb_speed_name(uint8_t speed)
{
    switch (speed) {
        case 0:
            return "Low speed";
        case 1:
            return "Full speed";
        case 2:
            return "High speed";
        default:
            return "Unknown speed";
    }
}

static const char *usb_class_name(uint8_t usb_class)
{
    switch (usb_class) {
        case 0x01:
            return "Audio";
        case 0x02:
            return "CDC";
        case 0x03:
            return "HID";
        case 0x06:
            return "Image";
        case 0x08:
            return "Mass storage";
        case 0x09:
            return "Hub";
        case 0x0E:
            return "Video";
        case 0xE0:
            return "Wireless";
        case 0xEF:
            return "Composite";
        case 0xFF:
            return "Vendor";
        default:
            return "USB device";
    }
}

static void style_role_button(lv_obj_t *button, bool selected)
{
    if (button == nullptr) {
        return;
    }

    lv_obj_set_style_bg_color(button, selected ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, selected ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    }
}

static void refresh_role_buttons()
{
    style_role_button(s_host_button, s_selected_role == FACTORY_USB_ROLE_HOST);
    style_role_button(s_device_button, s_selected_role == FACTORY_USB_ROLE_DEVICE);
}

static void refresh_otg_ui()
{
    factory_usb_otg_refresh();
    factory_usb_otg_state_t state = {};
    factory_usb_otg_get_state(&state);

    const bool has_error = state.error[0] != '\0';
    const char *result = "WAITING";
    if (has_error) {
        result = "ERROR";
    } else if (state.boost_fault) {
        result = "FAULT";
    } else if (!state.stack_ready) {
        result = state.stack_running ? "STARTING" : "STOPPED";
    } else if (state.peer_detected) {
        result = "PASS";
    }
    set_text_if_changed(s_result_label, result);

    char message[128];
    if (has_error) {
        snprintf(message, sizeof(message), "%s", state.error);
    } else if (state.role == FACTORY_USB_ROLE_HOST) {
        if (state.boost_fault) {
            snprintf(message, sizeof(message), "BQ25896 reported a boost fault.");
        } else if (!state.stack_ready) {
            snprintf(message, sizeof(message), "Starting USB Host and 5 V VBUS power...");
        } else if (!state.otg_powered) {
            snprintf(message, sizeof(message), "USB Host is ready; waiting for 5 V VBUS.");
        } else if (!state.peer_detected) {
            snprintf(message, sizeof(message), "Insert a USB Device into the OTG port.");
        } else {
            snprintf(message,
                     sizeof(message),
                     "USB Device enumerated; %u device%s connected.",
                     state.device_count,
                     state.device_count == 1 ? "" : "s");
        }
    } else if (!state.stack_ready) {
        snprintf(message, sizeof(message), "Starting the USB Device test interface...");
    } else if (state.peer_detected) {
        snprintf(message, sizeof(message), "USB Host completed Device enumeration.");
    } else if (state.external_vbus) {
        snprintf(message, sizeof(message), "Host VBUS detected; waiting for enumeration.");
    } else {
        snprintf(message, sizeof(message), "Connect the OTG port to a USB Host.");
    }
    set_text_if_changed(s_message_label, message);

    set_text_if_changed(s_role_label,
                        state.role == FACTORY_USB_ROLE_HOST
                            ? "Host - board supplies VBUS"
                            : "Device - USB Host supplies VBUS");

    char vbus[48];
    if (state.vbus_voltage_mv > 0) {
        snprintf(vbus,
                 sizeof(vbus),
                 "%u.%03u V (%s)",
                 state.vbus_voltage_mv / 1000U,
                 state.vbus_voltage_mv % 1000U,
                 state.otg_powered ? "OTG output" : (state.external_vbus ? "external" : "idle"));
    } else {
        snprintf(vbus, sizeof(vbus), "--");
    }
    set_text_if_changed(s_vbus_label, vbus);

    char peer[64];
    char descriptor[96];
    if (state.role == FACTORY_USB_ROLE_HOST && state.peer_detected) {
        snprintf(peer,
                 sizeof(peer),
                 "Address %u (%u connected)",
                 state.device_address,
                 state.device_count);
        snprintf(descriptor,
                 sizeof(descriptor),
                 "%04X:%04X  %s  %s",
                 state.vendor_id,
                 state.product_id,
                 usb_class_name(state.device_class),
                 usb_speed_name(state.device_speed));
    } else if (state.role == FACTORY_USB_ROLE_DEVICE) {
        snprintf(peer, sizeof(peer), "%s", state.peer_detected ? "USB Host enumerated" : "Not enumerated");
        snprintf(descriptor, sizeof(descriptor), "LILYGO factory HID keyboard");
    } else {
        snprintf(peer, sizeof(peer), "No USB Device");
        snprintf(descriptor, sizeof(descriptor), "--");
    }
    set_text_if_changed(s_peer_label, peer);
    set_text_if_changed(s_descriptor_label, descriptor);
}

static void role_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_usb_role_t role = (factory_usb_role_t)(intptr_t)lv_event_get_user_data(event);
    if (role == s_selected_role) {
        return;
    }

    s_selected_role = role;
    refresh_role_buttons();
    (void)factory_usb_otg_start(s_selected_role);
    refresh_otg_ui();
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_otg_ui();
}

static void create_usb_otg(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "USB OTG");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);

    lv_obj_t *role_row = lv_obj_create(panel);
    lv_obj_set_size(role_row, lv_pct(100), 58);
    style_transparent_row(role_row);
    lv_obj_set_flex_flow(role_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(role_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_host_button = factory_ui_create_action_button(
        role_row, "Host", role_button_event_cb, (void *)(intptr_t)FACTORY_USB_ROLE_HOST);
    lv_obj_set_size(s_host_button, lv_pct(49), 56);
    s_device_button = factory_ui_create_action_button(
        role_row, "Device", role_button_event_cb, (void *)(intptr_t)FACTORY_USB_ROLE_DEVICE);
    lv_obj_set_size(s_device_button, lv_pct(49), 56);
    refresh_role_buttons();

    s_result_label = lv_label_create(panel);
    lv_obj_set_width(s_result_label, lv_pct(100));
    lv_obj_set_style_text_font(s_result_label, FACTORY_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_result_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_result_label, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(s_result_label, "STOPPED");

    s_message_label = factory_ui_create_info_label(panel, "Select a USB role.");
    lv_obj_set_style_text_align(s_message_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *separator = lv_obj_create(panel);
    lv_obj_set_size(separator, lv_pct(100), 2);
    lv_obj_set_style_bg_color(separator, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(separator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(separator, 0, LV_PART_MAIN);

    (void)create_readout(panel, "Role", &s_role_label);
    (void)create_readout(panel, "VBUS", &s_vbus_label);
    (void)create_readout(panel, "Peer", &s_peer_label);
    (void)create_readout(panel, "Descriptor", &s_descriptor_label);
}

static void entry_usb_otg()
{
    (void)factory_usb_otg_start(s_selected_role);
    refresh_otg_ui();
    if (s_refresh_timer != nullptr) {
        lv_timer_del(s_refresh_timer);
    }
    s_refresh_timer = lv_timer_create(refresh_timer_cb, kRefreshPeriodMs, nullptr);
}

static void exit_usb_otg()
{
    if (s_refresh_timer != nullptr) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    factory_usb_otg_stop();
}

static void destroy_usb_otg()
{
    s_host_button = nullptr;
    s_device_button = nullptr;
    s_result_label = nullptr;
    s_message_label = nullptr;
    s_role_label = nullptr;
    s_vbus_label = nullptr;
    s_peer_label = nullptr;
    s_descriptor_label = nullptr;
    s_refresh_timer = nullptr;
}

static scr_lifecycle_t s_usb_otg_lifecycle = {
    .create = create_usb_otg,
    .entry = entry_usb_otg,
    .exit = exit_usb_otg,
    .destroy = destroy_usb_otg,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_usb_otg_lifecycle(void)
{
    return &s_usb_otg_lifecycle;
}
