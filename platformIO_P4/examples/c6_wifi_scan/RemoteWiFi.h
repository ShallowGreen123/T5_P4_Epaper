#ifndef REMOTE_WIFI_H
#define REMOTE_WIFI_H

#include <Arduino.h>
#include <vector>
#include <string>

// Pin Definitions (as requested)
#define BOARD_C6_D0     (14)
#define BOARD_C6_D1     (15)
#define BOARD_C6_D2     (16)
#define BOARD_C6_D3     (17)
#define BOARD_C6_CLK    (18)
#define BOARD_C6_CMD    (19)
#define BOARD_C6_RST    (54)
#define BOARD_C6_WAKEUP (6)

// Encryption types matching standard WiFi
typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WAPI_PSK,
    WIFI_AUTH_MAX
} wifi_auth_mode_t;

struct WiFiNetwork {
    String ssid;
    int32_t rssi;
    wifi_auth_mode_t encryptionType;
    String bssid;
    int32_t channel;
};

class RemoteWiFiClass {
public:
    RemoteWiFiClass();
    
    // Initialize SDIO connection and C6
    bool begin();
    
    // Scan for networks
    // Returns number of networks found, or -1 on error/timeout
    int16_t scanNetworks(bool async = false, bool show_hidden = false, bool passive = false, uint32_t max_ms_per_chan = 300);
    
    // Get scan result details
    String SSID(uint8_t networkItem);
    wifi_auth_mode_t encryptionType(uint8_t networkItem);
    int32_t RSSI(uint8_t networkItem);
    String BSSIDstr(uint8_t networkItem);
    int32_t channel(uint8_t networkItem);
    
    // Helper to print encryption type name
    String encryptionTypeStr(wifi_auth_mode_t type);

private:
    std::vector<WiFiNetwork> _scanResults;
    
    // Low-level SDIO control
    bool initSDIO();
    void deinitSDIO();
    bool resetC6();
    bool sendScanCommand();
    bool receiveScanResults();
    
    // Handle C6 reset and handshake
    bool handshake();
};

extern RemoteWiFiClass RemoteWiFi;

#endif
