/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

static const char *TAG = "usb_host_hid_basic";

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void *arg;
} app_hid_event_t;

typedef struct {
    enum key_state {
        KEY_STATE_PRESSED = 0x00,
        KEY_STATE_RELEASED = 0x01,
    } state;
    uint8_t modifier;
    uint8_t key_code;
} key_event_t;

static QueueHandle_t s_app_event_queue;

#define KEYBOARD_ENTER_MAIN_CHAR  '\r'
#define KEYBOARD_ENTER_LF_EXTEND  1
#define KEYBOARD_ENTER_ALT_ESCAPE 1

#if KEYBOARD_ENTER_ALT_ESCAPE
static bool s_is_ansi;
static unsigned int s_alt_code;
#endif

static const uint8_t keycode2ascii[57][2] = {
    {0, 0},   {0, 0},   {0, 0},   {0, 0},
    {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'}, {'e', 'E'}, {'f', 'F'}, {'g', 'G'}, {'h', 'H'},
    {'i', 'I'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'}, {'m', 'M'}, {'n', 'N'}, {'o', 'O'}, {'p', 'P'},
    {'q', 'Q'}, {'r', 'R'}, {'s', 'S'}, {'t', 'T'}, {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'},
    {'y', 'Y'}, {'z', 'Z'},
    {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'}, {'7', '&'}, {'8', '*'},
    {'9', '('}, {'0', ')'},
    {KEYBOARD_ENTER_MAIN_CHAR, KEYBOARD_ENTER_MAIN_CHAR},
    {0, 0},
    {'\b', 0},
    {0, 0},
    {' ', ' '}, {'-', '_'}, {'=', '+'}, {'[', '{'}, {']', '}'}, {'\\', '|'}, {'\\', '|'},
    {';', ':'}, {'\'', '"'}, {'`', '~'}, {',', '<'}, {'.', '>'}, {'/', '?'}
};

static const char *hid_proto_name(hid_protocol_t proto)
{
    switch (proto) {
    case HID_PROTOCOL_KEYBOARD:
        return "KEYBOARD";
    case HID_PROTOCOL_MOUSE:
        return "MOUSE";
    case HID_PROTOCOL_NONE:
    default:
        return "GENERIC";
    }
}

static void hid_keyboard_print_char(unsigned int key_char)
{
    if (!key_char) {
        return;
    }

    putchar((int)key_char);
#if KEYBOARD_ENTER_LF_EXTEND
    if (KEYBOARD_ENTER_MAIN_CHAR == key_char) {
        putchar('\n');
    }
#endif
    fflush(stdout);
}

static void hid_print_new_device_report_header(hid_protocol_t proto)
{
    static hid_protocol_t prev_proto_output = -1;

    if (prev_proto_output == proto) {
        return;
    }

    prev_proto_output = proto;
    printf("\r\n");
    if (proto == HID_PROTOCOL_MOUSE) {
        printf("Mouse\r\n");
    } else if (proto == HID_PROTOCOL_KEYBOARD) {
        printf("Keyboard\r\n");
    } else {
        printf("Generic HID\r\n");
    }
    fflush(stdout);
}

static bool hid_keyboard_is_modifier_shift(uint8_t modifier)
{
    return ((modifier & HID_LEFT_SHIFT) == HID_LEFT_SHIFT) ||
           ((modifier & HID_RIGHT_SHIFT) == HID_RIGHT_SHIFT);
}

#if KEYBOARD_ENTER_ALT_ESCAPE
static bool hid_keyboard_is_modifier_alt(uint8_t modifier)
{
    return ((modifier & HID_LEFT_ALT) == HID_LEFT_ALT) ||
           ((modifier & HID_RIGHT_ALT) == HID_RIGHT_ALT);
}

static bool hid_keyboard_alt_code_processing(uint8_t key_code)
{
    if ((key_code < HID_KEY_KEYPAD_1) || (key_code > HID_KEY_KEYPAD_0)) {
        return false;
    }

    if (key_code == HID_KEY_KEYPAD_0) {
        if (s_alt_code == 0) {
            s_is_ansi = true;
            return true;
        }
        key_code = HID_KEY_KEYPAD_1 - 1;
    }

    s_alt_code = s_alt_code * 10 + (key_code - (HID_KEY_KEYPAD_1 - 1));
    return true;
}

static void hid_keyboard_alt_code_process_complete(void)
{
    if (s_alt_code > 0) {
        char utf8_buffer[8] = {0};

        s_alt_code = s_alt_code & 0xff;
        if (s_is_ansi || s_alt_code == 0) {
            if (s_alt_code == 0) {
                s_alt_code = 0x100;
            }
            if (s_alt_code <= 0x7F) {
                utf8_buffer[0] = (char)s_alt_code;
            } else {
                utf8_buffer[0] = 0xC0 | ((s_alt_code >> 6) & 0x1F);
                utf8_buffer[1] = 0x80 | (s_alt_code & 0x3F);
            }
            printf("%s", utf8_buffer);
            fflush(stdout);
        } else {
            hid_keyboard_print_char(s_alt_code);
        }
        s_alt_code = 0;
    }
    s_is_ansi = false;
}
#endif

static bool hid_keyboard_get_char(uint8_t modifier, uint8_t key_code, unsigned char *key_char)
{
    uint8_t mod = hid_keyboard_is_modifier_shift(modifier) ? 1 : 0;

#if KEYBOARD_ENTER_ALT_ESCAPE
    if (hid_keyboard_is_modifier_alt(modifier) && hid_keyboard_alt_code_processing(key_code)) {
        return false;
    }
#endif

    if ((key_code >= HID_KEY_A) && (key_code <= HID_KEY_SLASH)) {
        *key_char = keycode2ascii[key_code][mod];
        return true;
    }

    return false;
}

static void key_event_callback(const key_event_t *key_event)
{
    unsigned char key_char = 0;

    hid_print_new_device_report_header(HID_PROTOCOL_KEYBOARD);

    if (key_event->state != KEY_STATE_PRESSED) {
        return;
    }

    if (hid_keyboard_get_char(key_event->modifier, key_event->key_code, &key_char)) {
        hid_keyboard_print_char(key_char);
    } else {
        printf("[keycode=0x%02X modifier=0x%02X]\r\n", key_event->key_code, key_event->modifier);
        fflush(stdout);
    }
}

static bool key_found(const uint8_t *src, uint8_t key, unsigned int length)
{
    for (unsigned int i = 0; i < length; i++) {
        if (src[i] == key) {
            return true;
        }
    }
    return false;
}

static void hid_host_keyboard_report_callback(const uint8_t *data, int length)
{
    hid_keyboard_input_report_boot_t *kb_report = (hid_keyboard_input_report_boot_t *)data;
    static uint8_t prev_keys[HID_KEYBOARD_KEY_MAX] = {0};
    key_event_t key_event;

    if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) {
        return;
    }

#if KEYBOARD_ENTER_ALT_ESCAPE
    if (!hid_keyboard_is_modifier_alt(kb_report->modifier.val)) {
        hid_keyboard_alt_code_process_complete();
    }
#endif

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if ((prev_keys[i] > HID_KEY_ERROR_UNDEFINED) &&
            !key_found(kb_report->key, prev_keys[i], HID_KEYBOARD_KEY_MAX)) {
            key_event.key_code = prev_keys[i];
            key_event.modifier = 0;
            key_event.state = KEY_STATE_RELEASED;
            key_event_callback(&key_event);
        }

        if ((kb_report->key[i] > HID_KEY_ERROR_UNDEFINED) &&
            !key_found(prev_keys, kb_report->key[i], HID_KEYBOARD_KEY_MAX)) {
            key_event.key_code = kb_report->key[i];
            key_event.modifier = kb_report->modifier.val;
            key_event.state = KEY_STATE_PRESSED;
            key_event_callback(&key_event);
        }
    }

    memcpy(prev_keys, kb_report->key, HID_KEYBOARD_KEY_MAX);
}

static void hid_host_mouse_report_callback(const uint8_t *data, int length)
{
    hid_mouse_input_report_boot_t *mouse_report = (hid_mouse_input_report_boot_t *)data;
    static int x_pos;
    static int y_pos;

    if (length < (int)sizeof(hid_mouse_input_report_boot_t)) {
        return;
    }

    x_pos += mouse_report->x_displacement;
    y_pos += mouse_report->y_displacement;

    hid_print_new_device_report_header(HID_PROTOCOL_MOUSE);
    printf("X:%6d  Y:%6d  Buttons[L:%c M:%c R:%c]\r\n",
           x_pos,
           y_pos,
           mouse_report->buttons.button1 ? 'o' : ' ',
           mouse_report->buttons.button3 ? 'o' : ' ',
           mouse_report->buttons.button2 ? 'o' : ' ');
    fflush(stdout);
}

static void hid_host_generic_report_callback(const uint8_t *data, int length)
{
    hid_print_new_device_report_header(HID_PROTOCOL_NONE);
    printf("Unsupported HID report:");
    for (int i = 0; i < length; i++) {
        printf(" %02X", data[i]);
    }
    printf("\r\n");
    fflush(stdout);
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        hid_host_interface_event_t event,
                                        void *arg)
{
    (void)arg;

    uint8_t data[64] = {0};
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(
                            hid_device_handle,
                            data,
                            sizeof(data),
                            &data_length));

        if ((HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) &&
            (HID_PROTOCOL_KEYBOARD == dev_params.proto)) {
            hid_host_keyboard_report_callback(data, (int)data_length);
        } else if ((HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) &&
                   (HID_PROTOCOL_MOUSE == dev_params.proto)) {
            hid_host_mouse_report_callback(data, (int)data_length);
        } else {
            hid_host_generic_report_callback(data, (int)data_length);
        }
        break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID device disconnected, protocol '%s'", hid_proto_name(dev_params.proto));
        ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
        break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "HID transfer error, protocol '%s'", hid_proto_name(dev_params.proto));
        break;

    default:
        ESP_LOGW(TAG, "Unhandled HID interface event %d", event);
        break;
    }
}

static void hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                                  hid_host_driver_event_t event,
                                  void *arg)
{
    (void)arg;

    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED: {
        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL,
        };

        ESP_LOGI(TAG,
                 "HID device connected, protocol '%s', subclass=%u",
                 hid_proto_name(dev_params.proto),
                 dev_params.sub_class);

        if ((HID_SUBCLASS_BOOT_INTERFACE != dev_params.sub_class) ||
            ((HID_PROTOCOL_KEYBOARD != dev_params.proto) && (HID_PROTOCOL_MOUSE != dev_params.proto))) {
            ESP_LOGW(TAG, "This example parses boot keyboard/mouse reports. Other HID devices will be shown as raw reports.");
        }

        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
        if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
            ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
            if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
            }
        }
        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        break;
    }

    default:
        break;
    }
}

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive((TaskHandle_t)arg);

    while (true) {
        uint32_t event_flags = 0;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
    }
}

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                     hid_host_driver_event_t event,
                                     void *arg)
{
    const app_hid_event_t evt = {
        .handle = hid_device_handle,
        .event = event,
        .arg = arg,
    };

    if (s_app_event_queue != NULL) {
        xQueueSend(s_app_event_queue, &evt, 0);
    }
}

void app_main(void)
{
    BaseType_t task_created;
    app_hid_event_t evt;

    ESP_LOGI(TAG, "USB HID host example for LilyGo T5-P4 E-Paper");
    ESP_LOGI(TAG, "Use the board OTG port as USB host on GPIO49/50.");

    s_app_event_queue = xQueueCreate(10, sizeof(app_hid_event_t));
    assert(s_app_event_queue != NULL);

    task_created = xTaskCreatePinnedToCore(
        usb_lib_task,
        "usb_events",
        4096,
        xTaskGetCurrentTaskHandle(),
        2,
        NULL,
        0);
    assert(task_created == pdTRUE);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL,
    };

    ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));
    ESP_LOGI(TAG, "Waiting for USB HID keyboard or mouse to be connected...");

    while (true) {
        if (xQueueReceive(s_app_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            hid_host_device_event(evt.handle, evt.event, evt.arg);
        }
    }
}
