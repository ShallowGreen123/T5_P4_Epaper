#pragma once

#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void factory_port_init(void);
const factory_runtime_info_t *factory_port_get_runtime_info(void);
const factory_page_info_t *factory_port_get_page_info(factory_page_id_t page_id);
bool factory_port_get_backlight_enabled(uint8_t index);
void factory_port_set_backlight_enabled(uint8_t index, bool enabled);

#ifdef __cplusplus
}  // extern "C"
#endif
