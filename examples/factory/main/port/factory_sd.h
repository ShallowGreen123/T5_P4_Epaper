#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void factory_sd_init(void);
bool factory_sd_enter_root(void);
void factory_sd_leave(void);
bool factory_sd_refresh(void);
bool factory_sd_go_parent(void);
const factory_sd_state_t *factory_sd_get_state(void);
size_t factory_sd_get_entry_count(void);
const factory_sd_entry_info_t *factory_sd_get_entry(size_t index);
bool factory_sd_open_entry(size_t index);

#ifdef __cplusplus
}  // extern "C"
#endif
