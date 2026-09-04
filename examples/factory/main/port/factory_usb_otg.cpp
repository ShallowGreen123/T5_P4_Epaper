#include "factory_usb_otg.h"

#include <stdio.h>
#include <string.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "factory_battery.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

namespace {

constexpr size_t kMaxTrackedDevices = 8;
constexpr uint32_t kHostTaskStackSize = 6144;
constexpr UBaseType_t kHostTaskPriority = 5;
constexpr TickType_t kHostPollTicks = pdMS_TO_TICKS(10);
constexpr size_t kHidConfigDescriptorLength = TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN;

static const char *TAG = "factory_usb_otg";

static const uint8_t kHidReportDescriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

static char kLanguageDescriptor[] = {0x09, 0x04};
static const char *kStringDescriptors[] = {
    kLanguageDescriptor,
    "LILYGO",
    "T5-P4 Factory USB Test",
    "T5P4-FACTORY",
    "Factory HID",
};

static const uint8_t kHidConfigDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1,
                          1,
                          0,
                          kHidConfigDescriptorLength,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          100),
    TUD_HID_DESCRIPTOR(0,
                       4,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(kHidReportDescriptor),
                       0x81,
                       8,
                       10),
};

struct HostDevice {
    usb_device_handle_t handle;
    uint8_t address;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t speed;
    uint16_t vendor_id;
    uint16_t product_id;
};

enum class HostEventType : uint8_t {
    NewDevice,
    DeviceGone,
};

struct HostEvent {
    HostEventType type;
    uint8_t address;
    usb_device_handle_t handle;
};

struct HostContext {
    QueueHandle_t event_queue;
    usb_host_client_handle_t client;
    HostDevice devices[kMaxTrackedDevices];
};

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static factory_usb_otg_state_t s_state = {
    .role = FACTORY_USB_ROLE_HOST,
};
static SemaphoreHandle_t s_host_done = nullptr;
static TaskHandle_t s_host_task = nullptr;
static usb_host_client_handle_t s_host_client = nullptr;
static volatile bool s_host_stop_requested = false;
static bool s_host_started = false;
static bool s_device_installed = false;

static void set_error(const char *message)
{
    portENTER_CRITICAL(&s_state_lock);
    strlcpy(s_state.error, message != nullptr ? message : "", sizeof(s_state.error));
    portEXIT_CRITICAL(&s_state_lock);
}

static void set_stack_state(bool running, bool ready)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.stack_running = running;
    s_state.stack_ready = ready;
    portEXIT_CRITICAL(&s_state_lock);
}

static void clear_peer_state_locked()
{
    s_state.peer_detected = false;
    s_state.device_count = 0;
    s_state.device_address = 0;
    s_state.device_class = 0;
    s_state.device_subclass = 0;
    s_state.device_protocol = 0;
    s_state.device_speed = 0;
    s_state.vendor_id = 0;
    s_state.product_id = 0;
}

static void reset_state(factory_usb_role_t role)
{
    portENTER_CRITICAL(&s_state_lock);
    memset(&s_state, 0, sizeof(s_state));
    s_state.role = role;
    portEXIT_CRITICAL(&s_state_lock);
}

static size_t host_device_count(const HostContext *context)
{
    size_t count = 0;
    for (size_t i = 0; i < kMaxTrackedDevices; ++i) {
        if (context->devices[i].handle != nullptr) {
            ++count;
        }
    }
    return count;
}

static void publish_host_devices(const HostContext *context, const HostDevice *preferred)
{
    const HostDevice *shown = preferred;
    if (shown == nullptr || shown->handle == nullptr) {
        shown = nullptr;
        for (size_t i = 0; i < kMaxTrackedDevices; ++i) {
            if (context->devices[i].handle != nullptr) {
                shown = &context->devices[i];
                break;
            }
        }
    }

    const size_t count = host_device_count(context);
    portENTER_CRITICAL(&s_state_lock);
    clear_peer_state_locked();
    s_state.device_count = (uint8_t)count;
    s_state.peer_detected = count > 0;
    if (shown != nullptr) {
        s_state.device_address = shown->address;
        s_state.device_class = shown->device_class;
        s_state.device_subclass = shown->device_subclass;
        s_state.device_protocol = shown->device_protocol;
        s_state.device_speed = shown->speed;
        s_state.vendor_id = shown->vendor_id;
        s_state.product_id = shown->product_id;
    }
    portEXIT_CRITICAL(&s_state_lock);
}

static HostDevice *find_free_host_device(HostContext *context)
{
    for (size_t i = 0; i < kMaxTrackedDevices; ++i) {
        if (context->devices[i].handle == nullptr) {
            return &context->devices[i];
        }
    }
    return nullptr;
}

static HostDevice *find_host_device(HostContext *context, usb_device_handle_t handle)
{
    for (size_t i = 0; i < kMaxTrackedDevices; ++i) {
        if (context->devices[i].handle == handle) {
            return &context->devices[i];
        }
    }
    return nullptr;
}

static void host_open_device(HostContext *context, uint8_t address)
{
    HostDevice *device = find_free_host_device(context);
    if (device == nullptr) {
        set_error("Too many USB devices (maximum 8)");
        return;
    }

    usb_device_handle_t handle = nullptr;
    esp_err_t err = usb_host_device_open(context->client, address, &handle);
    if (err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "Cannot open device %u: %s", address, esp_err_to_name(err));
        set_error(message);
        return;
    }

    memset(device, 0, sizeof(*device));
    device->handle = handle;
    device->address = address;

    usb_device_info_t device_info = {};
    if (usb_host_device_info(handle, &device_info) == ESP_OK) {
        device->speed = (uint8_t)device_info.speed;
    }

    const usb_device_desc_t *device_descriptor = nullptr;
    if (usb_host_get_device_descriptor(handle, &device_descriptor) == ESP_OK && device_descriptor != nullptr) {
        device->vendor_id = device_descriptor->idVendor;
        device->product_id = device_descriptor->idProduct;
        device->device_class = device_descriptor->bDeviceClass;
        device->device_subclass = device_descriptor->bDeviceSubClass;
        device->device_protocol = device_descriptor->bDeviceProtocol;
    }

    const usb_config_desc_t *config_descriptor = nullptr;
    if (device->device_class == USB_CLASS_PER_INTERFACE &&
        usb_host_get_active_config_descriptor(handle, &config_descriptor) == ESP_OK &&
        config_descriptor != nullptr) {
        int offset = 0;
        const usb_standard_desc_t *descriptor = usb_parse_next_descriptor_of_type(
            (const usb_standard_desc_t *)config_descriptor,
            config_descriptor->wTotalLength,
            USB_B_DESCRIPTOR_TYPE_INTERFACE,
            &offset);
        if (descriptor != nullptr) {
            const usb_intf_desc_t *interface_descriptor = (const usb_intf_desc_t *)descriptor;
            device->device_class = interface_descriptor->bInterfaceClass;
            device->device_subclass = interface_descriptor->bInterfaceSubClass;
            device->device_protocol = interface_descriptor->bInterfaceProtocol;
        }
    }

    set_error("");
    publish_host_devices(context, device);
    ESP_LOGI(TAG,
             "USB device enumerated: address=%u VID=%04X PID=%04X class=%02X speed=%u",
             device->address,
             device->vendor_id,
             device->product_id,
             device->device_class,
             device->speed);
}

static void host_close_device(HostContext *context, usb_device_handle_t handle)
{
    HostDevice *device = find_host_device(context, handle);
    if (device == nullptr) {
        return;
    }

    const uint8_t address = device->address;
    esp_err_t err = usb_host_device_close(context->client, device->handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot close USB device %u: %s", address, esp_err_to_name(err));
    }
    memset(device, 0, sizeof(*device));
    publish_host_devices(context, nullptr);
    ESP_LOGI(TAG, "USB device removed: address=%u", address);
}

static void host_client_event_cb(const usb_host_client_event_msg_t *event_message, void *arg)
{
    HostContext *context = (HostContext *)arg;
    HostEvent event = {};

    if (event_message->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        event.type = HostEventType::NewDevice;
        event.address = event_message->new_dev.address;
    } else if (event_message->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        event.type = HostEventType::DeviceGone;
        event.handle = event_message->dev_gone.dev_hdl;
    } else {
        return;
    }

    if (xQueueSend(context->event_queue, &event, 0) != pdTRUE) {
        set_error("USB event queue overflow");
    }
}

static void host_process_pending_events(HostContext *context)
{
    HostEvent event = {};
    while (xQueueReceive(context->event_queue, &event, 0) == pdTRUE) {
        if (event.type == HostEventType::NewDevice) {
            host_open_device(context, event.address);
        } else {
            host_close_device(context, event.handle);
        }
    }
}

static void host_close_all_devices(HostContext *context)
{
    for (size_t i = 0; i < kMaxTrackedDevices; ++i) {
        if (context->devices[i].handle != nullptr) {
            esp_err_t err = usb_host_device_close(context->client, context->devices[i].handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Cannot close USB device during shutdown: %s", esp_err_to_name(err));
            }
            memset(&context->devices[i], 0, sizeof(context->devices[i]));
        }
    }
    publish_host_devices(context, nullptr);
}

static void host_task(void *arg)
{
    (void)arg;
    HostContext context = {};
    bool library_installed = false;
    bool client_registered = false;

    usb_host_config_t host_config = {};
    usb_host_client_config_t client_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    host_config.peripheral_map = 0;

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "USB Host install failed: %s", esp_err_to_name(err));
        set_error(message);
        goto cleanup;
    }
    library_installed = true;

    context.event_queue = xQueueCreate(12, sizeof(HostEvent));
    if (context.event_queue == nullptr) {
        set_error("Cannot allocate USB event queue");
        goto cleanup;
    }

    client_config.is_synchronous = false;
    client_config.max_num_event_msg = 12;
    client_config.async.client_event_callback = host_client_event_cb;
    client_config.async.callback_arg = &context;
    err = usb_host_client_register(&client_config, &context.client);
    if (err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "USB Host client failed: %s", esp_err_to_name(err));
        set_error(message);
        goto cleanup;
    }
    client_registered = true;
    s_host_client = context.client;
    set_stack_state(true, true);
    set_error("");
    ESP_LOGI(TAG, "USB Host stack ready");

    while (!s_host_stop_requested) {
        uint32_t event_flags = 0;
        (void)usb_host_lib_handle_events(kHostPollTicks, &event_flags);
        (void)usb_host_client_handle_events(context.client, kHostPollTicks);
        host_process_pending_events(&context);
    }

cleanup:
    if (client_registered) {
        host_process_pending_events(&context);
        host_close_all_devices(&context);
        err = usb_host_client_deregister(context.client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "USB Host client deregister failed: %s", esp_err_to_name(err));
        }
        context.client = nullptr;
        s_host_client = nullptr;
    }

    if (context.event_queue != nullptr) {
        vQueueDelete(context.event_queue);
    }

    if (library_installed) {
        const esp_err_t free_result = usb_host_device_free_all();
        if (free_result == ESP_ERR_NOT_FINISHED) {
            for (int i = 0; i < 100; ++i) {
                uint32_t event_flags = 0;
                (void)usb_host_lib_handle_events(kHostPollTicks, &event_flags);
                if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0) {
                    break;
                }
            }
        }
        err = usb_host_uninstall();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "USB Host uninstall failed: %s", esp_err_to_name(err));
        }
    }

    set_stack_state(false, false);
    (void)factory_battery_set_otg_enabled(false);
    s_host_task = nullptr;
    xSemaphoreGive(s_host_done);
    ESP_LOGI(TAG, "USB Host stack stopped");
    vTaskDelete(nullptr);
}

static void device_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&s_state_lock);
    if (event->id == TINYUSB_EVENT_ATTACHED) {
        s_state.peer_detected = true;
    } else if (event->id == TINYUSB_EVENT_DETACHED) {
        s_state.peer_detected = false;
    }
    portEXIT_CRITICAL(&s_state_lock);
}

static bool start_device_stack()
{
    const factory_battery_otg_result_t power_result = factory_battery_set_otg_enabled(false);
    if (power_result != FACTORY_BATTERY_OTG_OK) {
        char message[96];
        snprintf(message,
                 sizeof(message),
                 "Cannot disable OTG power: %s",
                 factory_battery_otg_result_name(power_result));
        set_error(message);
        return false;
    }

    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(device_event_cb);
    config.descriptor.full_speed_config = kHidConfigDescriptor;
    config.descriptor.high_speed_config = kHidConfigDescriptor;
    config.descriptor.string = kStringDescriptors;
    config.descriptor.string_count = sizeof(kStringDescriptors) / sizeof(kStringDescriptors[0]);

    const esp_err_t err = tinyusb_driver_install(&config);
    if (err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "USB Device install failed: %s", esp_err_to_name(err));
        set_error(message);
        return false;
    }

    s_device_installed = true;
    set_stack_state(true, true);
    ESP_LOGI(TAG, "USB Device stack ready");
    return true;
}

}  // namespace

extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return kHidReportDescriptor;
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance,
                                            uint8_t report_id,
                                            hid_report_type_t report_type,
                                            uint8_t *buffer,
                                            uint16_t requested_length)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t instance,
                                        uint8_t report_id,
                                        hid_report_type_t report_type,
                                        uint8_t const *buffer,
                                        uint16_t buffer_size)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)buffer_size;
}

extern "C" bool factory_usb_otg_start(factory_usb_role_t role)
{
    factory_usb_otg_stop();
    reset_state(role);

    if (role == FACTORY_USB_ROLE_DEVICE) {
        return start_device_stack();
    }

    const factory_battery_otg_result_t power_result = factory_battery_set_otg_enabled(true);
    if (power_result != FACTORY_BATTERY_OTG_OK) {
        char message[96];
        snprintf(message,
                 sizeof(message),
                 "Cannot enable OTG power: %s",
                 factory_battery_otg_result_name(power_result));
        set_error(message);
        return false;
    }

    if (s_host_done == nullptr) {
        s_host_done = xSemaphoreCreateBinary();
    }
    if (s_host_done == nullptr) {
        set_error("Cannot allocate USB Host completion signal");
        (void)factory_battery_set_otg_enabled(false);
        return false;
    }
    (void)xSemaphoreTake(s_host_done, 0);

    s_host_stop_requested = false;
    set_stack_state(true, false);
    if (xTaskCreate(host_task,
                    "factory_usb_host",
                    kHostTaskStackSize,
                    nullptr,
                    kHostTaskPriority,
                    &s_host_task) != pdPASS) {
        s_host_task = nullptr;
        set_stack_state(false, false);
        set_error("Cannot create USB Host task");
        (void)factory_battery_set_otg_enabled(false);
        return false;
    }

    s_host_started = true;
    return true;
}

extern "C" void factory_usb_otg_stop(void)
{
    if (s_device_installed) {
        const esp_err_t err = tinyusb_driver_uninstall();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "USB Device uninstall failed: %s", esp_err_to_name(err));
        }
        s_device_installed = false;
        ESP_LOGI(TAG, "USB Device stack stopped");
    }

    if (s_host_started) {
        s_host_stop_requested = true;
        if (s_host_client != nullptr) {
            (void)usb_host_client_unblock(s_host_client);
        }
        (void)usb_host_lib_unblock();
        (void)xSemaphoreTake(s_host_done, portMAX_DELAY);
        s_host_started = false;
        s_host_client = nullptr;
        s_host_task = nullptr;
    }

    (void)factory_battery_set_otg_enabled(false);
    portENTER_CRITICAL(&s_state_lock);
    s_state.stack_running = false;
    s_state.stack_ready = false;
    clear_peer_state_locked();
    portEXIT_CRITICAL(&s_state_lock);
}

extern "C" void factory_usb_otg_refresh(void)
{
    factory_battery_refresh();
    const factory_battery_state_t *battery = factory_battery_get_state();
    if (battery == nullptr) {
        return;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.external_vbus = battery->vbus_connected;
    s_state.otg_powered = battery->otg_active;
    s_state.boost_fault = battery->boost_fault;
    s_state.vbus_voltage_mv = battery->vbus_voltage_mv;
    if (s_state.role == FACTORY_USB_ROLE_DEVICE && !battery->vbus_connected) {
        s_state.peer_detected = false;
    }
    portEXIT_CRITICAL(&s_state_lock);
}

extern "C" void factory_usb_otg_get_state(factory_usb_otg_state_t *state)
{
    if (state == nullptr) {
        return;
    }

    portENTER_CRITICAL(&s_state_lock);
    *state = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}
