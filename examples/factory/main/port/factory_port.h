#pragma once

#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void factory_port_init(void);
const factory_runtime_info_t *factory_port_get_runtime_info(void);
const factory_placeholder_info_t *factory_port_get_placeholder_page(factory_page_id_t page_id);

#ifdef __cplusplus
}  // extern "C"
#endif
