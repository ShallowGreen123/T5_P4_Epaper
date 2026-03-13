#ifndef REMOTE_WIFI_HOSTED_H
#define REMOTE_WIFI_HOSTED_H

#include <Arduino.h>
#include <WiFi.h>

#define BOARD_C6_D0 (14)
#define BOARD_C6_D1 (15)
#define BOARD_C6_D2 (16)
#define BOARD_C6_D3 (17)
#define BOARD_C6_CLK (18)
#define BOARD_C6_CMD (19)
#define BOARD_C6_RST (54)
#define BOARD_C6_WAKEUP (6)

struct HostedVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
};

class RemoteWiFiClass {
public:
    bool begin();
    bool isReady() const;

    wl_status_t connect(const char *ssid,
                        const char *pass,
                        uint32_t timeout_ms = 20000,
                        bool disconnect_first = true);
    bool disconnect(bool wifioff = false, bool eraseap = false);
    bool isConnected() const;
    wl_status_t status() const;
    IPAddress localIP() const;

    int16_t scanNetworks(bool async = false,
                         bool show_hidden = false,
                         bool passive = false,
                         uint32_t max_ms_per_chan = 300);
    int16_t scanComplete();
    void scanDelete();

    String SSID(uint8_t networkItem) const;
    wifi_auth_mode_t encryptionType(uint8_t networkItem) const;
    int32_t RSSI(uint8_t networkItem) const;
    String BSSIDstr(uint8_t networkItem) const;
    int32_t channel(uint8_t networkItem) const;
    String encryptionTypeStr(wifi_auth_mode_t type) const;

    bool getHostedVersions(HostedVersion &host, HostedVersion &slave) const;
    void printHostedInfo(Stream &out) const;
    void printScanTable(Stream &out, int16_t networksFound) const;

private:
    bool _ready = false;
    bool _sta_begun = false;
    bool configurePins();
    bool resetSlave() const;
    bool startHostedWiFi();
    static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info);
};

extern RemoteWiFiClass RemoteWiFi;

#endif
