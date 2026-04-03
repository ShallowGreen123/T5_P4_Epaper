/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "c6_wifi_scan";
static const TickType_t SCAN_INTERVAL_TICKS = pdMS_TO_TICKS(5000);

static const char *auth_mode_to_str(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_OWE:
        return "OWE";
    default:
        return "UNKNOWN";
    }
}

static void print_scan_table(const wifi_ap_record_t *ap_records, uint16_t ap_count)
{
    ESP_LOGI(TAG, "Idx RSSI Ch Auth      BSSID              SSID");
    for (uint16_t i = 0; i < ap_count; ++i) {
        const wifi_ap_record_t *ap = &ap_records[i];
        ESP_LOGI(TAG,
                 "%3u %4d %2u %-9s " MACSTR " %s",
                 i + 1,
                 ap->rssi,
                 ap->primary,
                 auth_mode_to_str(ap->authmode),
                 MAC2STR(ap->bssid),
                 (const char *)ap->ssid);
    }
}

static void wifi_init_sta(void)
{
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void wifi_scan_once(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 100,
            .max = 300,
        },
    };

    ESP_LOGI(TAG, "Starting one-shot WiFi scan via esp_wifi_remote/esp_hosted");
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true));

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Scan complete, AP count: %u", ap_count);

    if (ap_count == 0) {
        ESP_LOGI(TAG, "No AP found.");
        return;
    }

    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(*ap_records));
    if (ap_records == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for %" PRIu16 " AP records", ap_count);
        return;
    }

    uint16_t record_count = ap_count;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&record_count, ap_records));
    ESP_LOGI(TAG, "Retrieved %" PRIu16 " AP record(s)", record_count);
    print_scan_table(ap_records, record_count);

    free(ap_records);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "ESP32-P4 hosted WiFi scan example");
    ESP_LOGI(TAG, "Expect hosted SDIO logs for CLK[18] CMD[19] D0[14] D1[15] D2[16] D3[17] Slave_Reset[54]");

    wifi_init_sta();

    while (true) {
        wifi_scan_once();
        vTaskDelay(SCAN_INTERVAL_TICKS);
    }
}
