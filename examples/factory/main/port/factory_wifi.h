#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void factory_wifi_init(void);
bool factory_wifi_get_status(void);
bool factory_wifi_scan_start(void);
void factory_wifi_scan_poll(void);
bool factory_wifi_scan_busy(void);
bool factory_wifi_is_connecting(void);
bool factory_wifi_has_scan_started(void);
bool factory_wifi_selected_requires_password(void);
bool factory_wifi_connect_selected(void);
bool factory_wifi_connect_selected_with_password(const char *password);
bool factory_wifi_can_disconnect(void);
bool factory_wifi_disconnect(void);
const char *factory_wifi_get_state_text(void);
const char *factory_wifi_get_summary(void);
int factory_wifi_get_scan_count(void);
const char *factory_wifi_get_scan_item(int index);
int factory_wifi_get_selected_index(void);
void factory_wifi_select_item(int index);
const char *factory_wifi_get_ip(void);
const char *factory_wifi_get_ssid(void);
const char *factory_wifi_get_password(void);

#ifdef __cplusplus
}  // extern "C"
#endif
