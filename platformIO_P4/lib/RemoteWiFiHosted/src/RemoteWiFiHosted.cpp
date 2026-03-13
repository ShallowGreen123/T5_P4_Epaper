#include "RemoteWiFiHosted.h"

#include <esp32-hal-hosted.h>
#include <esp_wifi.h>

namespace {
constexpr uint32_t kResetLowMs = 20;
constexpr uint32_t kResetBootMs = 350;
constexpr uint32_t kRetryDelayMs = 500;
constexpr uint8_t kInitRetries = 3;
}

RemoteWiFiClass RemoteWiFi;

bool RemoteWiFiClass::begin() {
    if (_ready) {
        return true;
    }

    pinMode(BOARD_C6_WAKEUP, INPUT);
    WiFi.onEvent(&RemoteWiFiClass::onWiFiEvent);

    if (!configurePins()) {
        return false;
    }

    for (uint8_t attempt = 1; attempt <= kInitRetries; ++attempt) {
        resetSlave();
        if (startHostedWiFi()) {
            _ready = true;
            return true;
        }

        WiFi.mode(WIFI_MODE_NULL);
        delay(kRetryDelayMs);
    }

    return false;
}

bool RemoteWiFiClass::isReady() const {
    return _ready;
}

wl_status_t RemoteWiFiClass::connect(const char *ssid, const char *pass, uint32_t timeout_ms, bool disconnect_first) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return WL_NO_SSID_AVAIL;
    }

    if (!_ready && !begin()) {
        return WL_CONNECT_FAILED;
    }

    if (disconnect_first) {
        disconnect(false, true);
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass == nullptr ? "" : pass);
    _sta_begun = true;

    const uint32_t start_ms = millis();
    wl_status_t st = static_cast<wl_status_t>(WiFi.status());
    while (st != WL_CONNECTED && (millis() - start_ms) < timeout_ms) {
        delay(250);
        st = static_cast<wl_status_t>(WiFi.status());
    }

    return st;
}

bool RemoteWiFiClass::disconnect(bool wifioff, bool eraseap) {
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        _sta_begun = false;
        return true;
    }

    if (!_sta_begun) {
        return true;
    }

    const bool ok = WiFi.disconnect(wifioff, eraseap);
    if (wifioff) {
        _sta_begun = false;
    }
    return ok;
}

bool RemoteWiFiClass::isConnected() const {
    return WiFi.isConnected();
}

wl_status_t RemoteWiFiClass::status() const {
    return static_cast<wl_status_t>(WiFi.status());
}

IPAddress RemoteWiFiClass::localIP() const {
    return WiFi.localIP();
}

bool RemoteWiFiClass::configurePins() {
#if CONFIG_ESP_WIFI_REMOTE_ENABLED
    return WiFi.setPins(
        BOARD_C6_CLK,
        BOARD_C6_CMD,
        BOARD_C6_D0,
        BOARD_C6_D1,
        BOARD_C6_D2,
        BOARD_C6_D3,
        BOARD_C6_RST
    );
#else
    return false;
#endif
}

bool RemoteWiFiClass::resetSlave() const {
    pinMode(BOARD_C6_RST, OUTPUT);
    digitalWrite(BOARD_C6_RST, LOW);
    delay(kResetLowMs);
    digitalWrite(BOARD_C6_RST, HIGH);
    delay(kResetBootMs);
    return true;
}

bool RemoteWiFiClass::startHostedWiFi() {
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setScanTimeout(15000);
    WiFi.setScanActiveMinTime(100);

    if (!WiFi.mode(WIFI_STA)) {
        return false;
    }

    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);
    return true;
}

int16_t RemoteWiFiClass::scanNetworks(bool async, bool show_hidden, bool passive, uint32_t max_ms_per_chan) {
    if (!_ready && !begin()) {
        return WIFI_SCAN_FAILED;
    }

    return WiFi.scanNetworks(async, show_hidden, passive, max_ms_per_chan);
}

int16_t RemoteWiFiClass::scanComplete() {
    return WiFi.scanComplete();
}

void RemoteWiFiClass::scanDelete() {
    WiFi.scanDelete();
}

String RemoteWiFiClass::SSID(uint8_t networkItem) const {
    return WiFi.SSID(networkItem);
}

wifi_auth_mode_t RemoteWiFiClass::encryptionType(uint8_t networkItem) const {
    return WiFi.encryptionType(networkItem);
}

int32_t RemoteWiFiClass::RSSI(uint8_t networkItem) const {
    return WiFi.RSSI(networkItem);
}

String RemoteWiFiClass::BSSIDstr(uint8_t networkItem) const {
    return WiFi.BSSIDstr(networkItem);
}

int32_t RemoteWiFiClass::channel(uint8_t networkItem) const {
    return WiFi.channel(networkItem);
}

String RemoteWiFiClass::encryptionTypeStr(wifi_auth_mode_t type) const {
    switch (type) {
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
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI";
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
        case WIFI_AUTH_OWE:
            return "OWE";
        case WIFI_AUTH_WPA3_ENT_192:
            return "WPA3-192";
        case WIFI_AUTH_WPA3_EXT_PSK:
            return "WPA3-EXT";
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
            return "WPA3-EXT-M";
#endif
        default:
            return "UNKNOWN";
    }
}

bool RemoteWiFiClass::getHostedVersions(HostedVersion &host, HostedVersion &slave) const {
#if CONFIG_ESP_WIFI_REMOTE_ENABLED
    hostedGetHostVersion(&host.major, &host.minor, &host.patch);
    hostedGetSlaveVersion(&slave.major, &slave.minor, &slave.patch);
    return true;
#else
    (void)host;
    (void)slave;
    return false;
#endif
}

void RemoteWiFiClass::printHostedInfo(Stream &out) const {
    HostedVersion host {};
    HostedVersion slave {};
    if (!getHostedVersions(host, slave)) {
        out.println("esp-hosted unavailable in this build.");
        return;
    }

    out.printf(
        "Hosted host fw : %lu.%lu.%lu\r\n",
        static_cast<unsigned long>(host.major),
        static_cast<unsigned long>(host.minor),
        static_cast<unsigned long>(host.patch)
    );
    out.printf(
        "Hosted slave fw: %lu.%lu.%lu\r\n",
        static_cast<unsigned long>(slave.major),
        static_cast<unsigned long>(slave.minor),
        static_cast<unsigned long>(slave.patch)
    );
    out.printf("C6 WAKEUP level: %d\r\n", digitalRead(BOARD_C6_WAKEUP));
}

void RemoteWiFiClass::printScanTable(Stream &out, int16_t networksFound) const {
    if (networksFound <= 0) {
        return;
    }

    out.println("Idx RSSI Ch Auth         BSSID              SSID");
    for (int16_t i = 0; i < networksFound; ++i) {
        out.printf(
            "%3d %4ld %2ld %-12s %-18s %s\r\n",
            static_cast<int>(i + 1),
            static_cast<long>(RSSI(i)),
            static_cast<long>(channel(i)),
            encryptionTypeStr(encryptionType(i)).c_str(),
            BSSIDstr(i).c_str(),
            SSID(i).c_str()
        );
    }
}

void RemoteWiFiClass::onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_READY:
            Serial.println("[WiFi] remote stack ready");
            break;
        case ARDUINO_EVENT_WIFI_SCAN_DONE:
            Serial.printf("[WiFi] scan done, status=%u, found=%u\r\n", info.wifi_scan_done.status, info.wifi_scan_done.number);
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("[WiFi] STA started");
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
            Serial.println("[WiFi] STA stopped");
            break;
        default:
            break;
    }
}
