/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_msc_host.h"
#include "usb/usb_host.h"
#include "app_wifi.h"
#include "app_http_server.h"

#define BASE_PATH "/usb"

static const char *TAG = "usb_host_msc_example";

static void esp_msc_host_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ESP_MSC_HOST_CONNECT:
        ESP_LOGI(TAG, "MSC device connected on USB OTG");
        break;
    case ESP_MSC_HOST_DISCONNECT:
        ESP_LOGI(TAG, "MSC device disconnected from USB OTG");
        break;
    case ESP_MSC_HOST_INSTALL:
        ESP_LOGI(TAG, "USB Host MSC installed. Waiting for USB storage on USB OTG (GPIO49/GPIO50).");
        break;
    case ESP_MSC_HOST_UNINSTALL:
        ESP_LOGI(TAG, "USB Host MSC uninstalled");
        break;
    case ESP_MSC_HOST_VFS_REGISTER:
        ESP_LOGI(TAG, "USB storage mounted at %s", BASE_PATH);
        break;
    case ESP_MSC_HOST_VFS_UNREGISTER:
        ESP_LOGI(TAG, "USB storage unmounted from %s", BASE_PATH);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    app_wifi_main();

    ESP_ERROR_CHECK(esp_event_handler_register(ESP_MSC_HOST_EVENT, ESP_EVENT_ANY_ID, &esp_msc_host_handler, NULL));

    esp_msc_host_config_t msc_host_config = {
        .base_path = BASE_PATH,
        .host_driver_config = DEFAULT_MSC_HOST_DRIVER_CONFIG(),
        .vfs_fat_mount_config = DEFAULT_ESP_VFS_FAT_MOUNT_CONFIG(),
        .host_config = DEFAULT_USB_HOST_CONFIG()
    };
    esp_msc_host_handle_t host_handle = NULL;
    ESP_ERROR_CHECK(esp_msc_host_install(&msc_host_config, &host_handle));

    ESP_ERROR_CHECK(start_file_server(BASE_PATH));
}
