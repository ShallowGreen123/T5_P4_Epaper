#pragma once

#include "factory_types.h"

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

bool factory_display_init(void);
void factory_display_task_handler(void);
void factory_display_request_full_refresh(void);
const factory_display_mode_info_t *factory_display_get_mode_info(void);
i2c_master_bus_handle_t factory_display_get_i2c_bus(void);

#ifdef __cplusplus
}  // extern "C"
#endif
