#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_audio_amp_set(bool enable);
void es8311_start(void);




#ifdef __cplusplus
}
#endif
