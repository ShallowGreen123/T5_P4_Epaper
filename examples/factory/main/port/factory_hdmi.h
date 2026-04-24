#pragma once

#include <stdbool.h>

#include "factory_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool factory_hdmi_init(void);
void factory_hdmi_deinit(void);
bool factory_hdmi_start(factory_hdmi_mode_t mode);
void factory_hdmi_stop(void);
void factory_hdmi_set_mode(factory_hdmi_mode_t mode);
const factory_hdmi_state_t *factory_hdmi_get_state(void);

#ifdef __cplusplus
}  // extern "C"
#endif
