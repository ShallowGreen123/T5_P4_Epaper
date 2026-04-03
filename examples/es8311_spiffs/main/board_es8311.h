#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio_player.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_audio_player_init(void);
esp_err_t board_audio_player_deinit(void);
esp_err_t board_audio_player_set_volume(int volume);

esp_err_t board_audio_player_mute(AUDIO_PLAYER_MUTE_SETTING setting);
esp_err_t board_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t slot_mode);
esp_err_t board_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
