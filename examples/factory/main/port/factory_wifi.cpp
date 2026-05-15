#include "factory_wifi.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "t5_p4_board.h"

namespace {

enum FactoryWifiState {
    FACTORY_WIFI_STATE_IDLE = 0,
    FACTORY_WIFI_STATE_READY,
    FACTORY_WIFI_STATE_SCANNING,
    FACTORY_WIFI_STATE_CONNECTING,
    FACTORY_WIFI_STATE_CONNECTED,
    FACTORY_WIFI_STATE_DONE,
    FACTORY_WIFI_STATE_ERROR,
};

constexpr int kFactoryWifiMaxItems = 12;
constexpr size_t kFactoryWifiLineLen = 96;
constexpr size_t kFactoryWifiSsidLen = 33;
constexpr size_t kFactoryWifiPasswordLen = 65;
constexpr uint32_t kFactoryWifiScanTaskStack = 6144;

static const char *TAG = "factory_wifi";

bool s_wifi_available = false;
bool s_wifi_scan_started = false;
bool s_wifi_scan_busy = false;
bool s_wifi_connecting = false;
bool s_wifi_connected = false;
bool s_wifi_ignore_disconnect_once = false;
bool s_wifi_disconnect_requested = false;
FactoryWifiState s_wifi_state = FACTORY_WIFI_STATE_IDLE;
int s_wifi_scan_count = 0;
int s_wifi_scan_total_count = 0;
int s_wifi_selected_index = -1;
char s_wifi_state_text[64] = "WiFi idle";
char s_wifi_summary[160] = "Tap Scan WiFi to scan nearby networks.";
char s_wifi_connection_note[160] = "";
char s_wifi_ssid[64] = "Not selected";
char s_wifi_password[kFactoryWifiPasswordLen] = "-";
char s_wifi_ip[64] = "Not available";
char s_wifi_connected_ssid[kFactoryWifiSsidLen] = "";
char s_wifi_lines[kFactoryWifiMaxItems][kFactoryWifiLineLen] = {};
char s_wifi_ssids[kFactoryWifiMaxItems][kFactoryWifiSsidLen] = {};
wifi_auth_mode_t s_wifi_auth_modes[kFactoryWifiMaxItems] = {};
bool s_wifi_hidden[kFactoryWifiMaxItems] = {};
char s_wifi_pending_password[kFactoryWifiPasswordLen] = "";
char s_wifi_cached_ssid[kFactoryWifiSsidLen] = "";
char s_wifi_cached_password[kFactoryWifiPasswordLen] = "";

void refresh_summary();
const char *get_display_ssid(int index);
int find_scan_index_by_ssid(const char *ssid);
bool selected_network_is_open(void);
bool has_cached_password_for_selected(void);
bool selected_network_requires_password(void);
bool start_connection_for_selected_item(const char *password);
void update_password_label_for_selected();

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
        s_wifi_auth_modes[i] = WIFI_AUTH_OPEN;
        s_wifi_hidden[i] = false;
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

const char *get_display_ssid(int index)
{
    if (index < 0 || index >= s_wifi_scan_count) {
        return "Unknown";
    }
    return s_wifi_hidden[index] ? "<hidden>" : s_wifi_ssids[index];
}

int find_scan_index_by_ssid(const char *ssid)
{
    if (ssid == nullptr || ssid[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < s_wifi_scan_count; ++i) {
        if (!s_wifi_hidden[i] &&
            strncmp(s_wifi_ssids[i], ssid, sizeof(s_wifi_ssids[i])) == 0) {
            return i;
        }
    }

    return -1;
}

bool selected_network_is_open(void)
{
    if (s_wifi_selected_index < 0 || s_wifi_selected_index >= s_wifi_scan_count) {
        return false;
    }

    return s_wifi_auth_modes[s_wifi_selected_index] == WIFI_AUTH_OPEN ||
           s_wifi_auth_modes[s_wifi_selected_index] == WIFI_AUTH_OWE;
}

bool has_cached_password_for_selected(void)
{
    if (s_wifi_selected_index < 0 || s_wifi_selected_index >= s_wifi_scan_count || s_wifi_hidden[s_wifi_selected_index] ||
        selected_network_is_open()) {
        return false;
    }

    return s_wifi_cached_password[0] != '\0' &&
           strncmp(s_wifi_cached_ssid, s_wifi_ssids[s_wifi_selected_index], sizeof(s_wifi_cached_ssid)) == 0;
}

bool selected_network_requires_password(void)
{
    if (s_wifi_selected_index < 0 || s_wifi_selected_index >= s_wifi_scan_count || s_wifi_hidden[s_wifi_selected_index]) {
        return false;
    }

    return !selected_network_is_open() && !has_cached_password_for_selected();
}

void update_password_label_for_selected()
{
    if (s_wifi_selected_index < 0 || s_wifi_selected_index >= s_wifi_scan_count) {
        copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
        return;
    }

    if (s_wifi_hidden[s_wifi_selected_index] || selected_network_is_open()) {
        copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
    } else if (has_cached_password_for_selected()) {
        copy_text(s_wifi_password, sizeof(s_wifi_password), "(cached)");
    } else {
        copy_text(s_wifi_password, sizeof(s_wifi_password), "(required)");
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

    if (s_wifi_connecting || s_wifi_connected || s_wifi_connection_note[0] != '\0') {
        copy_text(s_wifi_summary, sizeof(s_wifi_summary), s_wifi_connection_note);
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
        if (s_wifi_hidden[s_wifi_selected_index]) {
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Selected hidden network. Manual SSID entry is not supported here.");
            return;
        }

        if (selected_network_requires_password()) {
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Selected: %s. Enter a password to connect.",
                get_display_ssid(s_wifi_selected_index));
            return;
        }

        if (s_wifi_scan_total_count > s_wifi_scan_count) {
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Showing %d of %d WiFi network(s). Selected: %s",
                s_wifi_scan_count,
                s_wifi_scan_total_count,
                get_display_ssid(s_wifi_selected_index));
        } else {
            snprintf(
                s_wifi_summary,
                sizeof(s_wifi_summary),
                "Found %d WiFi network(s). Selected: %s",
                s_wifi_scan_count,
                get_display_ssid(s_wifi_selected_index));
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

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;

        if (s_wifi_ignore_disconnect_once) {
            s_wifi_ignore_disconnect_once = false;
            return;
        }

        s_wifi_connecting = false;
        s_wifi_connected = false;
        copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
        s_wifi_connected_ssid[0] = '\0';
        s_wifi_pending_password[0] = '\0';

        if (s_wifi_disconnect_requested) {
            s_wifi_disconnect_requested = false;
            if (s_wifi_selected_index >= 0) {
                copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), get_display_ssid(s_wifi_selected_index));
            } else {
                copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), "Not selected");
            }
            update_password_label_for_selected();
            set_state(FACTORY_WIFI_STATE_READY, "WiFi disconnected");
            if (s_wifi_selected_index >= 0) {
                snprintf(
                    s_wifi_connection_note,
                    sizeof(s_wifi_connection_note),
                    "Disconnected from %s.",
                    get_display_ssid(s_wifi_selected_index));
            } else {
                copy_text(s_wifi_connection_note, sizeof(s_wifi_connection_note), "WiFi disconnected.");
            }
            refresh_summary();
            return;
        }

        set_state(FACTORY_WIFI_STATE_ERROR, "WiFi disconnected");
        update_password_label_for_selected();

        if (s_wifi_selected_index >= 0) {
            snprintf(
                s_wifi_connection_note,
                sizeof(s_wifi_connection_note),
                "Failed to connect to %s (reason %d).",
                get_display_ssid(s_wifi_selected_index),
                disconnected->reason);
        } else {
            snprintf(
                s_wifi_connection_note,
                sizeof(s_wifi_connection_note),
                "WiFi disconnected (reason %d).",
                disconnected->reason);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        s_wifi_connecting = false;
        s_wifi_connected = true;
        snprintf(s_wifi_ip, sizeof(s_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        set_state(FACTORY_WIFI_STATE_CONNECTED, "WiFi connected");

        if (s_wifi_selected_index >= 0) {
            copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), get_display_ssid(s_wifi_selected_index));
            copy_text(s_wifi_connected_ssid, sizeof(s_wifi_connected_ssid), s_wifi_ssids[s_wifi_selected_index]);

            if (!selected_network_is_open() && s_wifi_pending_password[0] != '\0') {
                copy_text(s_wifi_cached_ssid, sizeof(s_wifi_cached_ssid), s_wifi_ssids[s_wifi_selected_index]);
                copy_text(s_wifi_cached_password, sizeof(s_wifi_cached_password), s_wifi_pending_password);
                copy_text(s_wifi_password, sizeof(s_wifi_password), "(cached)");
            } else {
                copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
            }

            snprintf(
                s_wifi_connection_note,
                sizeof(s_wifi_connection_note),
                "Connected to %s | IP %s",
                get_display_ssid(s_wifi_selected_index),
                s_wifi_ip);
        } else {
            snprintf(
                s_wifi_connection_note,
                sizeof(s_wifi_connection_note),
                "WiFi connected | IP %s",
                s_wifi_ip);
        }

        s_wifi_pending_password[0] = '\0';
    }
}

bool start_connection_for_selected_item(const char *password)
{
    if (!s_wifi_available || s_wifi_selected_index < 0 || s_wifi_selected_index >= s_wifi_scan_count) {
        return false;
    }

    if (s_wifi_hidden[s_wifi_selected_index]) {
        s_wifi_connecting = false;
        s_wifi_connected = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "Hidden SSID");
        snprintf(
            s_wifi_connection_note,
            sizeof(s_wifi_connection_note),
            "Cannot connect to hidden SSID entry directly. Manual SSID entry is required.");
        copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
        refresh_summary();
        return false;
    }

    const bool open_network = selected_network_is_open();
    const char *connect_password = "";
    if (!open_network && password != nullptr && password[0] != '\0') {
        connect_password = password;
    } else if (!open_network && has_cached_password_for_selected()) {
        connect_password = s_wifi_cached_password;
    }

    if (!open_network && connect_password[0] == '\0') {
        s_wifi_connecting = false;
        s_wifi_connected = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "Password required");
        snprintf(
            s_wifi_connection_note,
            sizeof(s_wifi_connection_note),
            "%s requires a password before connecting.",
            get_display_ssid(s_wifi_selected_index));
        update_password_label_for_selected();
        refresh_summary();
        return false;
    }

    s_wifi_disconnect_requested = false;
    wifi_config_t wifi_cfg = {};
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", s_wifi_ssids[s_wifi_selected_index]);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", open_network ? "" : connect_password);
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        s_wifi_connecting = false;
        s_wifi_connected = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "WiFi config failed");
        snprintf(
            s_wifi_connection_note,
            sizeof(s_wifi_connection_note),
            "Failed to apply WiFi config for %s: %s",
            get_display_ssid(s_wifi_selected_index),
            esp_err_to_name(ret));
        refresh_summary();
        return false;
    }

    s_wifi_ignore_disconnect_once = true;
    ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT && ret != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_disconnect returned %s", esp_err_to_name(ret));
        s_wifi_ignore_disconnect_once = false;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        s_wifi_connecting = false;
        s_wifi_connected = false;
        set_state(FACTORY_WIFI_STATE_ERROR, "WiFi connect failed");
        s_wifi_pending_password[0] = '\0';
        snprintf(
            s_wifi_connection_note,
            sizeof(s_wifi_connection_note),
            "Failed to start connection to %s: %s",
            get_display_ssid(s_wifi_selected_index),
            esp_err_to_name(ret));
        refresh_summary();
        return false;
    }

    s_wifi_connecting = true;
    s_wifi_connected = false;
    set_state(FACTORY_WIFI_STATE_CONNECTING, "Connecting WiFi...");
    copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), get_display_ssid(s_wifi_selected_index));
    copy_text(s_wifi_pending_password, sizeof(s_wifi_pending_password), open_network ? "" : connect_password);
    copy_text(
        s_wifi_password,
        sizeof(s_wifi_password),
        open_network ? "-" : ((password != nullptr && password[0] != '\0') ? "(entered)" : "(cached)"));
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
    snprintf(
        s_wifi_connection_note,
        sizeof(s_wifi_connection_note),
        "Connecting to %s...",
        get_display_ssid(s_wifi_selected_index));
    refresh_summary();
    return true;
}

void wifi_scan_task(void *arg)
{
    (void)arg;

    const bool preserve_connection_state = s_wifi_connected;
    char previous_selected_ssid[kFactoryWifiSsidLen] = "";
    if (s_wifi_selected_index >= 0 &&
        s_wifi_selected_index < s_wifi_scan_count &&
        !s_wifi_hidden[s_wifi_selected_index]) {
        copy_text(previous_selected_ssid, sizeof(previous_selected_ssid), s_wifi_ssids[s_wifi_selected_index]);
    }

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
            s_wifi_hidden[i] = (ap->ssid[0] == '\0');
            s_wifi_auth_modes[i] = ap->authmode;
            snprintf(
                s_wifi_ssids[i],
                sizeof(s_wifi_ssids[i]),
                "%s",
                ap->ssid[0] != '\0' ? (const char *)ap->ssid : "");
            snprintf(
                s_wifi_lines[i],
                sizeof(s_wifi_lines[i]),
                "%s | %ddBm | Ch%u | %s",
                s_wifi_hidden[i] ? "<hidden>" : s_wifi_ssids[i],
                ap->rssi,
                ap->primary,
                auth_mode_to_str(ap->authmode));
        }
    } else {
        clear_items();
    }

    int restored_index = -1;
    if (preserve_connection_state) {
        restored_index = find_scan_index_by_ssid(s_wifi_connected_ssid);
    }
    if (restored_index < 0) {
        restored_index = find_scan_index_by_ssid(previous_selected_ssid);
    }
    s_wifi_selected_index = restored_index;
    if (restored_index >= 0 && !preserve_connection_state) {
        copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), get_display_ssid(restored_index));
    } else if (preserve_connection_state && s_wifi_connected_ssid[0] != '\0') {
        copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), s_wifi_connected_ssid);
    }

    if (ap_records != nullptr) {
        free(ap_records);
    }

    s_wifi_scan_busy = false;
    if (!preserve_connection_state) {
        set_state(FACTORY_WIFI_STATE_DONE, "WiFi scan complete");
    }
    refresh_summary();
    vTaskDelete(nullptr);
}

void set_init_failed(const char *reason)
{
    s_wifi_available = false;
    s_wifi_scan_busy = false;
    s_wifi_scan_started = false;
    s_wifi_connecting = false;
    s_wifi_connected = false;
    s_wifi_disconnect_requested = false;
    clear_items();
    set_state(FACTORY_WIFI_STATE_ERROR, "WiFi unavailable");
    copy_text(s_wifi_connection_note, sizeof(s_wifi_connection_note), reason);
    copy_text(s_wifi_summary, sizeof(s_wifi_summary), reason);
    copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
    s_wifi_connected_ssid[0] = '\0';
    s_wifi_pending_password[0] = '\0';
    s_wifi_cached_ssid[0] = '\0';
    s_wifi_cached_password[0] = '\0';
}

void set_ready_state()
{
    s_wifi_available = true;
    s_wifi_scan_busy = false;
    s_wifi_scan_started = false;
    s_wifi_connecting = false;
    s_wifi_connected = false;
    s_wifi_disconnect_requested = false;
    clear_items();
    set_state(FACTORY_WIFI_STATE_READY, "WiFi ready");
    s_wifi_connection_note[0] = '\0';
    copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
    s_wifi_connected_ssid[0] = '\0';
    s_wifi_pending_password[0] = '\0';
    s_wifi_cached_ssid[0] = '\0';
    s_wifi_cached_password[0] = '\0';
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

    ret = static_cast<esp_err_t>(esp_hosted_init());
    if (ret != ESP_OK) {
        set_init_failed("esp_hosted_init failed for WiFi backend.");
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

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr);
    if (ret != ESP_OK) {
        set_init_failed("Failed to register WiFi event handler.");
        return;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr);
    if (ret != ESP_OK) {
        set_init_failed("Failed to register IP event handler.");
        return;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        set_init_failed("esp_wifi_set_storage(WIFI_STORAGE_RAM) failed.");
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
    return s_wifi_connected;
}

extern "C" bool factory_wifi_scan_start(void)
{
    if (!s_wifi_available || s_wifi_scan_busy || s_wifi_connecting) {
        refresh_summary();
        return false;
    }

    s_wifi_scan_started = true;
    s_wifi_scan_busy = true;
    if (!s_wifi_connected) {
        clear_items();
        s_wifi_connection_note[0] = '\0';
        set_state(FACTORY_WIFI_STATE_SCANNING, "Scanning WiFi...");
    }
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

extern "C" bool factory_wifi_is_connecting(void)
{
    return s_wifi_connecting;
}

extern "C" bool factory_wifi_has_scan_started(void)
{
    return s_wifi_scan_started;
}

extern "C" bool factory_wifi_selected_requires_password(void)
{
    return selected_network_requires_password();
}

extern "C" bool factory_wifi_connect_selected(void)
{
    return start_connection_for_selected_item(nullptr);
}

extern "C" bool factory_wifi_connect_selected_with_password(const char *password)
{
    return start_connection_for_selected_item(password);
}

extern "C" bool factory_wifi_can_disconnect(void)
{
    return s_wifi_available && (s_wifi_connected || s_wifi_connecting);
}

extern "C" bool factory_wifi_disconnect(void)
{
    if (!factory_wifi_can_disconnect()) {
        refresh_summary();
        return false;
    }

    s_wifi_ignore_disconnect_once = false;
    s_wifi_disconnect_requested = true;
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
    set_state(FACTORY_WIFI_STATE_READY, "Disconnecting WiFi...");

    if (s_wifi_selected_index >= 0) {
        snprintf(
            s_wifi_connection_note,
            sizeof(s_wifi_connection_note),
            "Disconnecting from %s...",
            get_display_ssid(s_wifi_selected_index));
    } else {
        copy_text(s_wifi_connection_note, sizeof(s_wifi_connection_note), "Disconnecting WiFi...");
    }
    refresh_summary();

    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        return true;
    }

    if (ret == ESP_ERR_WIFI_NOT_CONNECT || ret == ESP_ERR_WIFI_CONN) {
        s_wifi_disconnect_requested = false;
        s_wifi_connecting = false;
        s_wifi_connected = false;
        set_state(FACTORY_WIFI_STATE_READY, "WiFi disconnected");
        update_password_label_for_selected();
        if (s_wifi_selected_index >= 0) {
            snprintf(
                s_wifi_connection_note,
                sizeof(s_wifi_connection_note),
                "Disconnected from %s.",
                get_display_ssid(s_wifi_selected_index));
        } else {
            copy_text(s_wifi_connection_note, sizeof(s_wifi_connection_note), "WiFi disconnected.");
        }
        refresh_summary();
        return true;
    }

    s_wifi_disconnect_requested = false;
    set_state(FACTORY_WIFI_STATE_ERROR, "WiFi disconnect failed");
    snprintf(
        s_wifi_connection_note,
        sizeof(s_wifi_connection_note),
        "Failed to disconnect WiFi: %s",
        esp_err_to_name(ret));
    refresh_summary();
    return false;
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
    s_wifi_connection_note[0] = '\0';
    copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), get_display_ssid(index));
    update_password_label_for_selected();
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
