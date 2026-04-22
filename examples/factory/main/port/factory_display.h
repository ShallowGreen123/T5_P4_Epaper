#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool factory_display_init(void);
void factory_display_task_handler(void);
void factory_display_request_full_refresh(void);
const factory_display_mode_info_t *factory_display_get_mode_info(void);
i2c_master_bus_handle_t factory_display_get_i2c_bus(void);

void factory_display_set_rotation(uint16_t rotation_deg);
uint16_t factory_display_get_rotation(void);
void factory_display_set_mirror(uint8_t mirror_mode);
uint8_t factory_display_get_mirror(void);
void factory_display_set_passes(uint8_t partial_passes, uint8_t full_passes);
void factory_display_set_dither(bool enable);
bool factory_display_get_dither(void);
void factory_display_set_max_partial_refreshes_before_full(uint8_t count);
uint8_t factory_display_get_max_partial_refreshes_before_full(void);

#ifdef __cplusplus
}  // extern "C"
#endif
