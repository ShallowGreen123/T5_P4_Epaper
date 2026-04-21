#include "factory_wifi.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace {

enum FactoryWifiState {
    FACTORY_WIFI_STATE_IDLE = 0,
    FACTORY_WIFI_STATE_READY,
    FACTORY_WIFI_STATE_SCANNING,
    FACTORY_WIFI_STATE_DONE,
    FACTORY_WIFI_STATE_ERROR,
};

constexpr int kFactoryWifiMaxItems = 12;
constexpr size_t kFactoryWifiLineLen = 96;
constexpr size_t kFactoryWifiSsidLen = 33;
constexpr uint32_t kFactoryWifiScanTaskStack = 6144;

static const char *TAG = "factory_wifi";

bool s_wifi_available = false;
bool s_wifi_scan_started = false;
bool s_wifi_scan_busy = false;
FactoryWifiState s_wifi_state = FACTORY_WIFI_STATE_IDLE;
int s_wifi_scan_count = 0;
int s_wifi_scan_total_count = 0;
int s_wifi_selected_index = -1;
char s_wifi_state_text[64] = "WiFi idle";
char s_wifi_summary[160] = "Tap Scan WiFi to scan nearby networks.";
char s_wifi_ssid[64] = "Not selected";
char s_wifi_password[64] = "-";
char s_wifi_ip[64] = "Not available";
char s_wifi_lines[kFactoryWifiMaxItems][kFactoryWifiLineLen] = {};
char s_wifi_ssids[kFactoryWifiMaxItems][kFactoryWifiSsidLen] = {};

void refresh_summary();

void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src != nullptr ? src : "");
}

void clear_items()
{
    for (int i = 0; i < kFactoryWifiMaxItems; ++i) {
        s_wifi_lines[i][0] = '\0';
        s_wifi_ssids[i][0] = '\0';
    }
    s_wifi_scan_count = 0;
    s_wifi_scan_total_count = 0;
    s_wifi_selected_index = -1;
    copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), "Not selected");
}

void set_state(FactoryWifiState state, const char *text)
{
    s_wifi_state = state;
    copy_text(s_wifi_state_text, sizeof(s_wifi_state_text), text);
}

const char *auth_mode_to_str(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";
        case WIFI_AUTH_OWE:
            return "OWE";
        default:
            return "UNKNOWN";
    }
}

void refresh_summary()
{
    if (!s_wifi_available) {
        copy_text(
            s_wifi_summary,
            sizeof(s_wifi_summary),
            "WiFi backend initialization failed. Check esp_hosted firmware on the onboard ESP32-C6.");
        return;
    }

    if (s_wifi_scan_busy) {
        copy_text(s_wifi_summary, sizeof(s_wifi_summary), "Scanning nearby WiFi networks...");
        return;
    }

    if (!s_wifi_scan_started) {
        copy_text(
            s_wifi_summary,
            sizeof(s_wifi_summary),
            "Tap Scan WiFi to run one WiFi scan via the onboard ESP32-C6.");
        return;
    }

    if (s_wifi_scan_count <= 0) {
        copy_text(s_wifi_summary, sizeof(s_wifi_summary), "Scan complete. No WiFi networks found.");
        return;
    }

    if (s_wifi_selected_index >= 0 && s_wifi_selected_index < s_wifi_scan_count) {
        if (s_wifi_scan_total_count > s_wifi_scan_count) {
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Showing %d of %d WiFi network(s). Selected: %s",
                s_wifi_scan_count,
                s_wifi_scan_total_count,
                s_wifi_ssids[s_wifi_selected_index]);
        } else {
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Found %d WiFi network(s). Selected: %s",
                s_wifi_scan_count,
                s_wifi_ssids[s_wifi_selected_index]);
        }
        return;
    }

    if (s_wifi_scan_total_count > s_wifi_scan_count) {
        snprintf(
            s_wifi_summary,
            sizeof(s_wifi_summary),
            "Showing %d of %d WiFi network(s). Tap one item to select it.",
            s_wifi_scan_count,
            s_wifi_scan_total_count);
    } else {
        snprintf(
            s_wifi_summary,
            sizeof(s_wifi_summary),
            "Found %d WiFi network(s). Tap one item to select it.",
            s_wifi_scan_count);
    }
}

esp_err_t ensure_nvs_ready()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t ensure_netif_ready()
{
    esp_err_t ret = esp_netif_init();
    if (ret == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return ret;
}

esp_err_t ensure_event_loop_ready()
{
    esp_err_t ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return ret;
}

void wifi_scan_task(void *arg)
{
    (void)arg;

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = true;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(ret));
        s_wifi_scan_busy = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "WiFi scan failed");
        snprintf(
            s_wifi_summary,
            sizeof(s_wifi_summary),
            "Failed to start WiFi scan: %s",
            esp_err_to_name(ret));
        vTaskDelete(nullptr);
        return;
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(ret));
        s_wifi_scan_busy = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "WiFi scan failed");
        snprintf(
            s_wifi_summary,
            sizeof(s_wifi_summary),
            "Failed to fetch scan count: %s",
            esp_err_to_name(ret));
        vTaskDelete(nullptr);
        return;
    }

    wifi_ap_record_t *ap_records = nullptr;
    if (ap_count > 0) {
        ap_records = (wifi_ap_record_t *)calloc(ap_count, sizeof(wifi_ap_record_t));
        if (ap_records == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate %" PRIu16 " AP records", ap_count);
            s_wifi_scan_busy = false;
            set_state(FACTORY_WIFI_STATE_ERROR, "WiFi scan failed");
            copy_text(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Out of memory while collecting WiFi scan results.");
            vTaskDelete(nullptr);
            return;
        }

        uint16_t record_count = ap_count;
        ret = esp_wifi_scan_get_ap_records(&record_count, ap_records);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(ret));
            free(ap_records);
            s_wifi_scan_busy = false;
            set_state(FACTORY_WIFI_STATE_ERROR, "WiFi scan failed");
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Failed to fetch scan results: %s",
                esp_err_to_name(ret));
            vTaskDelete(nullptr);
            return;
        }

        clear_items();
        s_wifi_scan_total_count = (int)record_count;
        s_wifi_scan_count = record_count > kFactoryWifiMaxItems ? kFactoryWifiMaxItems : (int)record_count;
        for (int i = 0; i < s_wifi_scan_count; ++i) {
            const wifi_ap_record_t *ap = &ap_records[i];
            snprintf(
                s_wifi_ssids[i],
                sizeof(s_wifi_ssids[i]),
                "%s",
                ap->ssid[0] != '\0' ? (const char *)ap->ssid : "<hidden>");
            snprintf(
                s_wifi_lines[i],
                sizeof(s_wifi_lines[i]),
                "%s | %ddBm | Ch%u | %s",
                s_wifi_ssids[i],
                ap->rssi,
                ap->primary,
                auth_mode_to_str(ap->authmode));
        }
    } else {
        clear_items();
    }

    if (ap_records != nullptr) {
        free(ap_records);
    }

    s_wifi_scan_busy = false;
    set_state(FACTORY_WIFI_STATE_DONE, "WiFi scan complete");
    refresh_summary();
    vTaskDelete(nullptr);
}

void set_init_failed(const char *reason)
{
    s_wifi_available = false;
    s_wifi_scan_busy = false;
    s_wifi_scan_started = false;
    clear_items();
    set_state(FACTORY_WIFI_STATE_ERROR, "WiFi unavailable");
    copy_text(s_wifi_summary, sizeof(s_wifi_summary), reason);
    copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
}

void set_ready_state()
{
    s_wifi_available = true;
    s_wifi_scan_busy = false;
    s_wifi_scan_started = false;
    clear_items();
    set_state(FACTORY_WIFI_STATE_READY, "WiFi ready");
    copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
    refresh_summary();
}

}  // namespace

extern "C" void factory_wifi_init(void)
{
    esp_err_t ret = ensure_nvs_ready();
    if (ret != ESP_OK) {
        set_init_failed("nvs_flash_init failed for WiFi backend.");
        return;
    }

    ret = ensure_netif_ready();
    if (ret != ESP_OK) {
        set_init_failed("esp_netif_init failed for WiFi backend.");
        return;
    }

    ret = ensure_event_loop_ready();
    if (ret != ESP_OK) {
        set_init_failed("esp_event_loop_create_default failed for WiFi backend.");
        return;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == nullptr) {
        set_init_failed("Failed to create default WiFi STA netif.");
        return;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        set_init_failed("esp_wifi_init failed for WiFi backend.");
        return;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        set_init_failed("esp_wifi_set_mode(WIFI_MODE_STA) failed.");
        return;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        set_init_failed("esp_wifi_start failed. Check esp_hosted firmware on C6.");
        return;
    }

    set_ready_state();
}

extern "C" bool factory_wifi_get_status(void)
{
    return s_wifi_available;
}

extern "C" bool factory_wifi_scan_start(void)
{
    if (!s_wifi_available || s_wifi_scan_busy) {
        refresh_summary();
        return false;
    }

    clear_items();
    s_wifi_scan_started = true;
    s_wifi_scan_busy = true;
    set_state(FACTORY_WIFI_STATE_SCANNING, "Scanning WiFi...");
    refresh_summary();

    BaseType_t task_ok = xTaskCreate(
        wifi_scan_task,
        "factory_wifi_scan",
        kFactoryWifiScanTaskStack,
        nullptr,
        tskIDLE_PRIORITY + 1,
        nullptr);
    if (task_ok != pdPASS) {
        s_wifi_scan_busy = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "WiFi scan failed");
        copy_text(s_wifi_summary, sizeof(s_wifi_summary), "Failed to create WiFi scan task.");
        return false;
    }

    return true;
}

extern "C" void factory_wifi_scan_poll(void)
{
    refresh_summary();
}

extern "C" bool factory_wifi_scan_busy(void)
{
    return s_wifi_scan_busy;
}

extern "C" bool factory_wifi_has_scan_started(void)
{
    return s_wifi_scan_started;
}

extern "C" const char *factory_wifi_get_state_text(void)
{
    return s_wifi_state_text;
}

extern "C" const char *factory_wifi_get_summary(void)
{
    return s_wifi_summary;
}

extern "C" int factory_wifi_get_scan_count(void)
{
    return s_wifi_scan_count;
}

extern "C" const char *factory_wifi_get_scan_item(int index)
{
    if (index < 0 || index >= s_wifi_scan_count) {
        return "";
    }
    return s_wifi_lines[index];
}

extern "C" int factory_wifi_get_selected_index(void)
{
    return s_wifi_selected_index;
}

extern "C" void factory_wifi_select_item(int index)
{
    if (index < 0 || index >= s_wifi_scan_count) {
        return;
    }

    s_wifi_selected_index = index;
    copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), s_wifi_ssids[index]);
    refresh_summary();
}

extern "C" const char *factory_wifi_get_ip(void)
{
    return s_wifi_ip;
}

extern "C" const char *factory_wifi_get_ssid(void)
{
    return s_wifi_ssid;
}

extern "C" const char *factory_wifi_get_password(void)
{
    return s_wifi_password;
}
