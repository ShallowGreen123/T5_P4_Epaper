#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void factory_ui_init(void);
void factory_ui_task_handler(void);
void factory_ui_notify_touch_activity(void);
void factory_ui_set_inactivity_shutdown_minutes(uint8_t minutes);
uint8_t factory_ui_get_inactivity_shutdown_minutes(void);
bool factory_ui_request_power_off(void);

#ifdef __cplusplus
}  // extern "C"
#endif
