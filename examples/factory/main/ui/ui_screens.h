#pragma once

#include "factory_types.h"
#include "scr_mrg.h"

#ifdef __cplusplus
extern "C" {
#endif

scr_lifecycle_t *factory_screen_home_lifecycle(void);
scr_lifecycle_t *factory_screen_setting_lifecycle(void);
scr_lifecycle_t *factory_screen_test_lifecycle(void);
scr_lifecycle_t *factory_screen_display_lifecycle(void);
scr_lifecycle_t *factory_screen_touch_lifecycle(void);
scr_lifecycle_t *factory_screen_adjust_lifecycle(void);
scr_lifecycle_t *factory_screen_wifi_lifecycle(void);
scr_lifecycle_t *factory_screen_sd_lifecycle(void);
scr_lifecycle_t *factory_screen_battery_lifecycle(void);
scr_lifecycle_t *factory_screen_audio_lifecycle(void);
scr_lifecycle_t *factory_placeholder_lifecycle(factory_page_id_t page_id);

#ifdef __cplusplus
}  // extern "C"
#endif
