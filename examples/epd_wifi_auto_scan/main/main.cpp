#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "M5GFX.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"
#include "nvs_flash.h"
#include "t5_p4_board.h"

namespace {

constexpr char kTag[] = "epd_wifi_auto";

constexpr int kPanelWidth = 1440;
constexpr int kPanelHeight = 720;
constexpr int kEpdBusSpeedHz = 40000000;
constexpr int kVcomMillivolts = -1600;
constexpr uint8_t kPanelMirrorXRotation = 6;

constexpr gpio_num_t kEpdD0 = GPIO_NUM_27;
constexpr gpio_num_t kEpdD1 = GPIO_NUM_28;
constexpr gpio_num_t kEpdD2 = GPIO_NUM_29;
constexpr gpio_num_t kEpdD3 = GPIO_NUM_30;
constexpr gpio_num_t kEpdD4 = GPIO_NUM_31;
constexpr gpio_num_t kEpdD5 = GPIO_NUM_32;
constexpr gpio_num_t kEpdD6 = GPIO_NUM_33;
constexpr gpio_num_t kEpdD7 = GPIO_NUM_34;
constexpr gpio_num_t kDummyDc = GPIO_NUM_22;

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsEnableAllRails = 0x3F;
constexpr uint8_t kTpsPowerGoodMask = 0xFA;
constexpr uint8_t kTpsPowerGoodExpected = 0xFA;
constexpr int kPowerGoodTimeoutMs = 400;

constexpr TickType_t kRescanIntervalTicks = pdMS_TO_TICKS(10000);
constexpr TickType_t kConnectTimeoutTicks = pdMS_TO_TICKS(15000);
constexpr TickType_t kDisconnectSettleTicks = pdMS_TO_TICKS(300);
constexpr int kMaxVisibleAps = 12;

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiFailedBit = BIT1;
constexpr EventBits_t kWifiDisconnectedBit = BIT2;

struct KnownWifi {
    const char *ssid;
    const char *password;
};

constexpr KnownWifi kKnownNetworks[] = {
    {"xinyuandianzi", "AA15994823428"},
    {"LilyGo-AABB", "xinyuandianzi"},
};

constexpr size_t kKnownNetworkCount = sizeof(kKnownNetworks) / sizeof(kKnownNetworks[0]);

struct VisibleAp {
    char ssid[33] = "";
    int rssi = 0;
    uint8_t channel = 0;
    wifi_auth_mode_t authmode = WIFI_AUTH_OPEN;
    bool hidden = false;
    bool preferred = false;
};

struct TargetCandidate {
    size_t preferred_index = 0;
    int rssi = std::numeric_limits<int>::min();
};

struct ScanSnapshot {
    uint16_t total_count = 0;
    size_t visible_count = 0;
    std::array<VisibleAp, kMaxVisibleAps> visible = {};
    std::array<bool, kKnownNetworkCount> preferred_found = {};
    std::array<int, kKnownNetworkCount> preferred_rssi = {};
    std::array<uint8_t, kKnownNetworkCount> preferred_channel = {};
    std::array<TargetCandidate, kKnownNetworkCount> candidates = {};
    size_t candidate_count = 0;
};

struct ConnectionInfo {
    char ssid[33] = "";
    int rssi = 0;
    char ip[16] = "0.0.0.0";
};

i2c_master_dev_handle_t s_tps = nullptr;
EventGroupHandle_t s_wifi_events = nullptr;
esp_netif_ip_info_t s_ip_info = {};
volatile int s_last_disconnect_reason = 0;
volatile bool s_wifi_connected = false;
ScanSnapshot s_blank_snapshot = {};
ScanSnapshot s_scan_snapshot = {};
ConnectionInfo s_connection_info = {};
char s_render_line[192] = {};
char s_detail_text[192] = {};
char s_error_text[128] = {};

esp_err_t ensure_tps_device()
{
    if (s_tps != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != nullptr, ESP_ERR_INVALID_STATE, kTag, "shared I2C bus is not initialized");

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = T5_BOARD_I2C_ADDR_TPS651851;
    dev_cfg.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
    return i2c_master_bus_add_device(bus, &dev_cfg, &s_tps);
}

esp_err_t tps_write(uint8_t reg, const uint8_t *data, size_t size)
{
    ESP_RETURN_ON_ERROR(ensure_tps_device(), kTag, "create TPS651851 I2C device failed");
    uint8_t buffer[4] = {reg, 0, 0, 0};
    ESP_RETURN_ON_FALSE(size <= sizeof(buffer) - 1, ESP_ERR_INVALID_ARG, kTag, "TPS write too long");
    if (size > 0) {
        memcpy(&buffer[1], data, size);
    }
    return i2c_master_transmit(s_tps, buffer, size + 1, 100);
}

esp_err_t tps_write_u8(uint8_t reg, uint8_t value)
{
    return tps_write(reg, &value, 1);
}

esp_err_t tps_read_u8(uint8_t reg, uint8_t *value)
{
    ESP_RETURN_ON_ERROR(ensure_tps_device(), kTag, "create TPS651851 I2C device failed");
    return i2c_master_transmit_receive(s_tps, &reg, 1, value, 1, 100);
}

void log_heap(const char *stage)
{
    ESP_LOGI(kTag, "%s heap: internal=%u psram=%u",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

class T5P4EpdBus : public lgfx::Bus_EPD {
public:
    bool init() override
    {
        if (bsp_i2c_init() != ESP_OK) {
            ESP_LOGE(kTag, "I2C init failed");
            return false;
        }
        if (t5_board_pca9535_init() != ESP_OK) {
            ESP_LOGE(kTag, "PCA9535 init failed");
            return false;
        }
        if (ensure_tps_device() != ESP_OK) {
            ESP_LOGE(kTag, "TPS651851 init failed");
            return false;
        }

        const bool ok = lgfx::Bus_EPD::init();
        if (!ok) {
            ESP_LOGE(kTag, "M5GFX EPD bus init failed");
        }
        return ok;
    }

    bool powerControl(bool power_on) override
    {
        if (_pwr_on == power_on) {
            return true;
        }

        wait();
        const esp_err_t err = power_on ? power_on_sequence() : power_off_sequence();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "EPD power %s failed: %s", power_on ? "on" : "off", esp_err_to_name(err));
            return false;
        }

        _pwr_on = power_on;
        return true;
    }

private:
    esp_err_t set_expander(uint8_t io, bool level)
    {
        return t5_board_pca9535_set_level(io, level);
    }

    esp_err_t wait_pca_power_good()
    {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            bool level = false;
            ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_TPS_PWR_GOOD, &level),
                                kTag, "read TPS PWR_GOOD failed");
            if (level) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t wait_tps_power_good()
    {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            uint8_t pg = 0;
            ESP_RETURN_ON_ERROR(tps_read_u8(kTpsRegPowerGood, &pg), kTag, "read TPS PG failed");
            if ((pg & kTpsPowerGoodMask) == kTpsPowerGoodExpected) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t power_on_sequence()
    {
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_OE, true), kTag, "set EPD OE failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_MODE, true), kTag, "set EPD MODE failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_WAKEUP, true), kTag, "set TPS WAKEUP failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, true), kTag, "set TPS PWRUP failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, true), kTag, "set VCOM CTRL failed");
        vTaskDelay(pdMS_TO_TICKS(1));

        ESP_RETURN_ON_ERROR(wait_pca_power_good(), kTag, "PCA TPS PWR_GOOD timeout");
        ESP_RETURN_ON_ERROR(tps_write_u8(kTpsRegEnable, kTpsEnableAllRails), kTag, "enable TPS rails failed");

        const int vcom = std::clamp(kVcomMillivolts / -10, 0, 0xFFFF);
        const uint8_t vcom_data[2] = {
            static_cast<uint8_t>(vcom & 0xFF),
            static_cast<uint8_t>((vcom >> 8) & 0xFF),
        };
        ESP_RETURN_ON_ERROR(tps_write(kTpsRegVcom, vcom_data, sizeof(vcom_data)), kTag, "set TPS VCOM failed");
        ESP_RETURN_ON_ERROR(wait_tps_power_good(), kTag, "TPS PG timeout");
        return ESP_OK;
    }

    esp_err_t power_off_sequence()
    {
        esp_err_t err = ESP_OK;
        err |= set_expander(T5_BOARD_PCA_IO_EPD_OE, false);
        err |= set_expander(T5_BOARD_PCA_IO_EPD_MODE, false);
        err |= set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, false);
        err |= set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, false);
        vTaskDelay(pdMS_TO_TICKS(1));
        err |= set_expander(T5_BOARD_PCA_IO_TPS_WAKEUP, false);
        return err;
    }
};

class T5P4M5GFX : public lgfx::LGFX_Device {
public:
    T5P4M5GFX()
    {
        auto bus_cfg = bus_.config();
        bus_cfg.bus_speed = kEpdBusSpeedHz;
        bus_cfg.pin_data[0] = kEpdD0;
        bus_cfg.pin_data[1] = kEpdD1;
        bus_cfg.pin_data[2] = kEpdD2;
        bus_cfg.pin_data[3] = kEpdD3;
        bus_cfg.pin_data[4] = kEpdD4;
        bus_cfg.pin_data[5] = kEpdD5;
        bus_cfg.pin_data[6] = kEpdD6;
        bus_cfg.pin_data[7] = kEpdD7;
        bus_cfg.pin_pwr = kDummyDc;
        bus_cfg.pin_spv = T5_BOARD_EPD_STV;
        bus_cfg.pin_ckv = T5_BOARD_EPD_CKV;
        bus_cfg.pin_sph = T5_BOARD_EPD_STH;
        bus_cfg.pin_oe = kDummyDc;
        bus_cfg.pin_le = T5_BOARD_EPD_LEH;
        bus_cfg.pin_cl = T5_BOARD_EPD_CKH;
        bus_cfg.bus_width = 8;
        bus_.config(bus_cfg);

        panel_.setBus(&bus_);

        auto panel_cfg = panel_.config();
        panel_cfg.memory_width = kPanelWidth;
        panel_cfg.memory_height = kPanelHeight;
        panel_cfg.panel_width = kPanelWidth;
        panel_cfg.panel_height = kPanelHeight;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = kPanelMirrorXRotation;
        panel_cfg.bus_shared = false;
        panel_.config(panel_cfg);

        auto detail = panel_.config_detail();
        detail.line_padding = 0;
        detail.task_priority = 3;
        panel_.config_detail(detail);

        setPanel(&panel_);
    }

private:
    T5P4EpdBus bus_;
    lgfx::Panel_EPD panel_;
};

T5P4M5GFX display;

void reset_scan_snapshot(ScanSnapshot *snapshot)
{
    *snapshot = {};
    snapshot->preferred_rssi.fill(std::numeric_limits<int>::min());
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

int find_known_network(const char *ssid)
{
    if (ssid == nullptr || ssid[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < kKnownNetworkCount; ++i) {
        if (strcmp(ssid, kKnownNetworks[i].ssid) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        s_last_disconnect_reason = disconnected != nullptr ? disconnected->reason : 0;
        s_wifi_connected = false;
        if (s_wifi_events != nullptr) {
            xEventGroupClearBits(s_wifi_events, kWifiConnectedBit);
            xEventGroupSetBits(s_wifi_events, kWifiFailedBit | kWifiDisconnectedBit);
        }
        ESP_LOGW(kTag, "WiFi disconnected, reason=%d", static_cast<int>(s_last_disconnect_reason));
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto *got_ip = static_cast<ip_event_got_ip_t *>(event_data);
        if (got_ip != nullptr) {
            s_ip_info = got_ip->ip_info;
        }
        s_wifi_connected = true;
        if (s_wifi_events != nullptr) {
            xEventGroupClearBits(s_wifi_events, kWifiFailedBit | kWifiDisconnectedBit);
            xEventGroupSetBits(s_wifi_events, kWifiConnectedBit);
        }
        ESP_LOGI(kTag, "WiFi connected, IP=" IPSTR, IP2STR(&s_ip_info.ip));
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
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

esp_err_t ensure_event_loop_ready()
{
    esp_err_t ret = esp_event_loop_create_default();
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

esp_err_t init_wifi_backend()
{
    ESP_RETURN_ON_ERROR(ensure_nvs_ready(), kTag, "nvs init failed");
    ESP_RETURN_ON_ERROR(ensure_netif_ready(), kTag, "netif init failed");
    ESP_RETURN_ON_ERROR(ensure_event_loop_ready(), kTag, "event loop init failed");

    if (s_wifi_events == nullptr) {
        s_wifi_events = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_wifi_events != nullptr, ESP_ERR_NO_MEM, kTag, "create WiFi event group failed");
    }

    ESP_RETURN_ON_ERROR(static_cast<esp_err_t>(esp_hosted_init()), kTag, "esp_hosted_init failed");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta_netif != nullptr, ESP_ERR_NO_MEM, kTag, "create default WiFi STA netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr),
                        kTag, "register WiFi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr),
                        kTag, "register IP event handler failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), kTag, "set WiFi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set WiFi mode failed");

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        return ret;
    }

    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Disable WiFi power save failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(kTag, "Hosted WiFi backend ready");
    return ESP_OK;
}

void draw_section_box(int x, int y, int w, int h, const char *title)
{
    const uint32_t gray = display.color888(224, 224, 224);
    display.drawRect(x, y, w, h, TFT_BLACK);
    display.fillRect(x + 1, y + 1, w - 2, 32, gray);
    display.setTextColor(TFT_BLACK, gray);
    display.setTextSize(2);
    display.setCursor(x + 14, y + 7);
    display.print(title);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void begin_screen(const char *status, const char *detail)
{
    display.waitDisplay();
    display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    display.fillScreen(TFT_WHITE);
    display.setTextWrap(false);
    display.setTextColor(TFT_BLACK, TFT_WHITE);

    display.setTextSize(3);
    display.setCursor(48, 36);
    display.print("T5-P4 Hosted WiFi Scan");

    display.setTextSize(2);
    display.setCursor(52, 82);
    display.print("M5GFX + esp_hosted + esp_wifi_remote");

    draw_section_box(48, 118, 1344, 116, "Status");
    display.setCursor(68, 162);
    display.print(status);
    display.setCursor(68, 192);
    display.print(detail);
}

void draw_target_summary(const ScanSnapshot &snapshot)
{
    draw_section_box(48, 252, 1344, 110, "Target WiFi");

    int y = 296;
    for (size_t i = 0; i < kKnownNetworkCount; ++i) {
        if (snapshot.preferred_found[i]) {
            std::snprintf(s_render_line,
                          sizeof(s_render_line),
                          "%zu. %-24s found  RSSI %d dBm  Ch %u",
                          i + 1,
                          kKnownNetworks[i].ssid,
                          snapshot.preferred_rssi[i],
                          snapshot.preferred_channel[i]);
        } else {
            std::snprintf(s_render_line,
                          sizeof(s_render_line),
                          "%zu. %-24s not found",
                          i + 1,
                          kKnownNetworks[i].ssid);
        }
        display.setCursor(68, y);
        display.print(s_render_line);
        y += 28;
    }
}

void draw_ap_list(const ScanSnapshot &snapshot)
{
    draw_section_box(48, 380, 1344, 292, "Nearby WiFi");

    std::snprintf(s_render_line,
                  sizeof(s_render_line),
                  "Showing %u of %u AP(s) from the latest scan",
                  static_cast<unsigned>(snapshot.visible_count),
                  static_cast<unsigned>(snapshot.total_count));
    display.setCursor(68, 424);
    display.print(s_render_line);

    if (snapshot.total_count == 0) {
        display.setCursor(68, 456);
        display.print("No WiFi networks detected.");
        return;
    }

    int y = 456;
    for (size_t i = 0; i < snapshot.visible_count; ++i) {
        const VisibleAp &ap = snapshot.visible[i];
        const char *name = ap.hidden ? "<hidden>" : ap.ssid;
        std::snprintf(s_render_line,
                      sizeof(s_render_line),
                      "%2u. %-32.32s %4d dBm  Ch %-2u  %-8s%s",
                      static_cast<unsigned>(i + 1),
                      name,
                      ap.rssi,
                      ap.channel,
                      auth_mode_to_str(ap.authmode),
                      ap.preferred ? "  <target>" : "");
        display.setCursor(68, y);
        display.print(s_render_line);
        y += 18;
    }
}

void render_scan_screen(const ScanSnapshot &snapshot, const char *status, const char *detail)
{
    begin_screen(status, detail);
    draw_target_summary(snapshot);
    draw_ap_list(snapshot);
    display.display();
}

void render_connected_screen(const ScanSnapshot &snapshot, const ConnectionInfo &info)
{
    begin_screen("Connected to known WiFi", "The link will stay up until it disconnects.");

    draw_section_box(48, 252, 1344, 110, "Connection");
    std::snprintf(s_render_line, sizeof(s_render_line), "SSID: %s", info.ssid);
    display.setCursor(68, 296);
    display.print(s_render_line);
    std::snprintf(s_render_line, sizeof(s_render_line), "RSSI: %d dBm", info.rssi);
    display.setCursor(460, 296);
    display.print(s_render_line);
    std::snprintf(s_render_line, sizeof(s_render_line), "IP: %s", info.ip);
    display.setCursor(68, 324);
    display.print(s_render_line);

    draw_ap_list(snapshot);
    display.display();
}

void render_message_screen(const char *status, const char *detail)
{
    render_scan_screen(s_blank_snapshot, status, detail);
}

void log_scan_table(const std::vector<wifi_ap_record_t> &records)
{
    ESP_LOGI(kTag, "Idx RSSI Ch Auth      SSID");
    for (size_t i = 0; i < records.size(); ++i) {
        const wifi_ap_record_t &ap = records[i];
        ESP_LOGI(kTag,
                 "%3u %4d %2u %-9s %s",
                 static_cast<unsigned>(i + 1),
                 ap.rssi,
                 ap.primary,
                 auth_mode_to_str(ap.authmode),
                 ap.ssid[0] != '\0' ? reinterpret_cast<const char *>(ap.ssid) : "<hidden>");
    }
}

esp_err_t perform_wifi_scan(ScanSnapshot *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot != nullptr, ESP_ERR_INVALID_ARG, kTag, "invalid scan snapshot");
    reset_scan_snapshot(snapshot);

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = true;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;

    ESP_LOGI(kTag, "Starting WiFi scan");
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), kTag, "start scan failed");

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), kTag, "get scan count failed");
    snapshot->total_count = ap_count;

    if (ap_count == 0) {
        ESP_LOGI(kTag, "No AP found");
        return ESP_OK;
    }

    std::vector<wifi_ap_record_t> records(ap_count);
    uint16_t record_count = ap_count;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&record_count, records.data()), kTag, "get scan records failed");
    records.resize(record_count);
    std::sort(records.begin(), records.end(), [](const wifi_ap_record_t &lhs, const wifi_ap_record_t &rhs) {
        return lhs.rssi > rhs.rssi;
    });

    log_scan_table(records);

    for (const wifi_ap_record_t &ap : records) {
        const int known_index = find_known_network(reinterpret_cast<const char *>(ap.ssid));
        if (known_index >= 0 && ap.rssi > snapshot->preferred_rssi[known_index]) {
            snapshot->preferred_found[known_index] = true;
            snapshot->preferred_rssi[known_index] = ap.rssi;
            snapshot->preferred_channel[known_index] = ap.primary;
        }
    }

    snapshot->visible_count = std::min(records.size(), static_cast<size_t>(kMaxVisibleAps));
    for (size_t i = 0; i < snapshot->visible_count; ++i) {
        const wifi_ap_record_t &ap = records[i];
        VisibleAp &out = snapshot->visible[i];
        out.hidden = (ap.ssid[0] == '\0');
        out.preferred = find_known_network(reinterpret_cast<const char *>(ap.ssid)) >= 0;
        out.rssi = ap.rssi;
        out.channel = ap.primary;
        out.authmode = ap.authmode;
        std::snprintf(out.ssid,
                      sizeof(out.ssid),
                      "%s",
                      out.hidden ? "" : reinterpret_cast<const char *>(ap.ssid));
    }

    for (size_t i = 0; i < kKnownNetworkCount; ++i) {
        if (snapshot->preferred_found[i]) {
            snapshot->candidates[snapshot->candidate_count].preferred_index = i;
            snapshot->candidates[snapshot->candidate_count].rssi = snapshot->preferred_rssi[i];
            ++snapshot->candidate_count;
        }
    }

    std::sort(snapshot->candidates.begin(),
              snapshot->candidates.begin() + snapshot->candidate_count,
              [](const TargetCandidate &lhs, const TargetCandidate &rhs) {
                  return lhs.rssi > rhs.rssi;
              });

    return ESP_OK;
}

esp_err_t refresh_connection_info(const char *fallback_ssid, int fallback_rssi, ConnectionInfo *info)
{
    ESP_RETURN_ON_FALSE(info != nullptr, ESP_ERR_INVALID_ARG, kTag, "invalid connection info");

    wifi_ap_record_t ap_info = {};
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK && ap_info.ssid[0] != '\0') {
        std::snprintf(info->ssid, sizeof(info->ssid), "%s", reinterpret_cast<const char *>(ap_info.ssid));
        info->rssi = ap_info.rssi;
    } else {
        std::snprintf(info->ssid, sizeof(info->ssid), "%s", fallback_ssid != nullptr ? fallback_ssid : "");
        info->rssi = fallback_rssi;
    }

    std::snprintf(info->ip, sizeof(info->ip), IPSTR, IP2STR(&s_ip_info.ip));
    return ret == ESP_OK ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool connect_to_known_wifi(size_t known_index, int known_rssi, ConnectionInfo *info, char *error_text, size_t error_text_size)
{
    if (error_text_size > 0) {
        error_text[0] = '\0';
    }

    ESP_LOGI(kTag, "Connecting to \"%s\"", kKnownNetworks[known_index].ssid);

    if (s_wifi_connected) {
        esp_wifi_disconnect();
        vTaskDelay(kDisconnectSettleTicks);
    }

    xEventGroupClearBits(s_wifi_events, kWifiConnectedBit | kWifiFailedBit | kWifiDisconnectedBit);

    wifi_config_t wifi_cfg = {};
    std::snprintf(reinterpret_cast<char *>(wifi_cfg.sta.ssid),
                  sizeof(wifi_cfg.sta.ssid),
                  "%s",
                  kKnownNetworks[known_index].ssid);
    std::snprintf(reinterpret_cast<char *>(wifi_cfg.sta.password),
                  sizeof(wifi_cfg.sta.password),
                  "%s",
                  kKnownNetworks[known_index].password);
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        std::snprintf(error_text, error_text_size, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        std::snprintf(error_text, error_text_size, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           kWifiConnectedBit | kWifiFailedBit,
                                           pdTRUE,
                                           pdFALSE,
                                           kConnectTimeoutTicks);
    if ((bits & kWifiConnectedBit) != 0) {
        refresh_connection_info(kKnownNetworks[known_index].ssid, known_rssi, info);
        return true;
    }

    esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi_events, kWifiConnectedBit | kWifiFailedBit | kWifiDisconnectedBit);

    if ((bits & kWifiFailedBit) != 0) {
        std::snprintf(error_text,
                      error_text_size,
                      "disconnect reason %d",
                      static_cast<int>(s_last_disconnect_reason));
    } else {
        std::snprintf(error_text, error_text_size, "connection timeout");
    }
    return false;
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "starting M5GFX hosted WiFi auto scan");
    log_heap("before init");
    reset_scan_snapshot(&s_blank_snapshot);
    reset_scan_snapshot(&s_scan_snapshot);

    if (!display.init_without_reset(false)) {
        ESP_LOGE(kTag, "display init failed");
        return;
    }

    display.setAutoDisplay(false);
    log_heap("after display init");
    render_message_screen("Initializing...", "Bringing up the display and hosted WiFi backend.");

    esp_err_t ret = init_wifi_backend();
    if (ret != ESP_OK) {
        std::snprintf(s_detail_text, sizeof(s_detail_text), "WiFi backend init failed: %s", esp_err_to_name(ret));
        render_message_screen("Initialization failed", s_detail_text);
        ESP_LOGE(kTag, "%s", s_detail_text);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    while (true) {
        render_scan_screen(s_scan_snapshot, "Scanning nearby WiFi...", "A scan starts immediately after boot.");

        ret = perform_wifi_scan(&s_scan_snapshot);
        if (ret != ESP_OK) {
            std::snprintf(s_detail_text,
                          sizeof(s_detail_text),
                          "Scan failed: %s. Rescan in 10 seconds.",
                          esp_err_to_name(ret));
            render_scan_screen(s_scan_snapshot, "WiFi scan failed", s_detail_text);
            ESP_LOGE(kTag, "%s", s_detail_text);
            vTaskDelay(kRescanIntervalTicks);
            continue;
        }

        if (s_scan_snapshot.candidate_count == 0) {
            render_scan_screen(s_scan_snapshot,
                               "No target WiFi found",
                               "Will rescan in 10 seconds. Nearby WiFi is shown below.");
            vTaskDelay(kRescanIntervalTicks);
            continue;
        }

        bool connected = false;

        for (size_t i = 0; i < s_scan_snapshot.candidate_count; ++i) {
            const TargetCandidate &candidate = s_scan_snapshot.candidates[i];
            const KnownWifi &known = kKnownNetworks[candidate.preferred_index];
            std::snprintf(s_detail_text,
                          sizeof(s_detail_text),
                          "Auto select %s (%d dBm) and start connection.",
                          known.ssid,
                          candidate.rssi);
            render_scan_screen(s_scan_snapshot, "Known WiFi detected", s_detail_text);

            if (connect_to_known_wifi(candidate.preferred_index,
                                      candidate.rssi,
                                      &s_connection_info,
                                      s_error_text,
                                      sizeof(s_error_text))) {
                render_connected_screen(s_scan_snapshot, s_connection_info);
                connected = true;
                xEventGroupClearBits(s_wifi_events, kWifiDisconnectedBit | kWifiFailedBit);
                xEventGroupWaitBits(s_wifi_events, kWifiDisconnectedBit, pdTRUE, pdFALSE, portMAX_DELAY);
                std::snprintf(s_detail_text,
                              sizeof(s_detail_text),
                              "Disconnected from %s (reason %d). Rescan in 10 seconds.",
                              s_connection_info.ssid,
                              static_cast<int>(s_last_disconnect_reason));
                render_scan_screen(s_scan_snapshot, "Connection lost", s_detail_text);
                vTaskDelay(kRescanIntervalTicks);
                break;
            }

            std::snprintf(s_detail_text,
                          sizeof(s_detail_text),
                          "Connect %s failed: %s",
                          known.ssid,
                          s_error_text);
            render_scan_screen(s_scan_snapshot, "Known WiFi connect failed", s_detail_text);
            ESP_LOGW(kTag, "%s", s_detail_text);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }

        if (!connected) {
            render_scan_screen(s_scan_snapshot,
                               "All known WiFi failed",
                               "Will rescan in 10 seconds. Nearby WiFi is shown below.");
            vTaskDelay(kRescanIntervalTicks);
        }
    }
}
