#include "factory_wifi.h"

#include <stdio.h>
#include <string.h>

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

bool s_wifi_status = false;
FactoryWifiState s_wifi_state = FACTORY_WIFI_STATE_ERROR;
int s_wifi_scan_count = 0;
char s_wifi_state_text[64] = "WiFi disabled";
char s_wifi_summary[128] = "Factory build: WiFi scan backend is not enabled.";
char s_wifi_ssid[64] = "Not connected";
char s_wifi_password[64] = "-";
char s_wifi_ip[64] = "Not available";
char s_wifi_lines[kFactoryWifiMaxItems][kFactoryWifiLineLen] = {};

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
    }
    s_wifi_scan_count = 0;
}

void set_state(FactoryWifiState state, const char *text)
{
    s_wifi_state = state;
    copy_text(s_wifi_state_text, sizeof(s_wifi_state_text), text);
}

void set_placeholder_state()
{
    clear_items();
    s_wifi_status = false;
    set_state(FACTORY_WIFI_STATE_ERROR, "WiFi scan disabled");
    copy_text(
        s_wifi_summary,
        sizeof(s_wifi_summary),
        "FastEPD WiFi page migrated. Actual ESP-IDF scan backend is still disabled in this factory build.");
    copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), "Not connected");
    copy_text(s_wifi_password, sizeof(s_wifi_password), "-");
    copy_text(s_wifi_ip, sizeof(s_wifi_ip), "Not available");
}

}  // namespace

extern "C" void factory_wifi_init(void)
{
    set_placeholder_state();
}

extern "C" bool factory_wifi_get_status(void)
{
    return s_wifi_status;
}

extern "C" bool factory_wifi_scan_start(void)
{
    set_placeholder_state();
    return false;
}

extern "C" void factory_wifi_scan_poll(void)
{
    set_placeholder_state();
}

extern "C" bool factory_wifi_scan_busy(void)
{
    return s_wifi_state == FACTORY_WIFI_STATE_SCANNING;
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
