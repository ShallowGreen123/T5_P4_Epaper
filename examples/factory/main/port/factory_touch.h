#pragma once

#include <stdbool.h>

#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool factory_touch_init(void);
bool factory_touch_is_ready(void);
void factory_touch_get_diag_state(factory_touch_diag_state_t *state);

#ifdef __cplusplus
}  // extern "C"
#endif
