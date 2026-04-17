#pragma once

#include "scr_mrg.h"
#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

scr_lifecycle_t *factory_screen_home_lifecycle(void);
scr_lifecycle_t *factory_screen_display_lifecycle(void);
scr_lifecycle_t *factory_screen_touch_lifecycle(void);
scr_lifecycle_t *factory_screen_device_lifecycle(void);
scr_lifecycle_t *factory_placeholder_lifecycle(factory_page_id_t page_id);

#ifdef __cplusplus
}  // extern "C"
#endif
