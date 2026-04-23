#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACTORY_AUDIO_WAVEFORM_BINS 64

typedef enum factory_audio_mode {
    FACTORY_AUDIO_MODE_IDLE = 0,
    FACTORY_AUDIO_MODE_MONITOR,
    FACTORY_AUDIO_MODE_RECORD,
    FACTORY_AUDIO_MODE_PLAYBACK,
    FACTORY_AUDIO_MODE_LOOPBACK,
    FACTORY_AUDIO_MODE_ERROR,
} factory_audio_mode_t;

typedef struct factory_audio_state {
    bool init_attempted;
    bool codec_ready;
    bool i2s_ready;
    bool amp_ready;
    bool mic_ready;
    bool speaker_ready;
    bool recording_ready;
    bool clipping;
    factory_audio_mode_t mode;
    uint32_t sample_rate_hz;
    uint32_t record_seconds;
    uint32_t bytes_recorded;
    uint32_t clip_count;
    uint8_t volume_percent;
    uint8_t mic_gain_db;
    uint8_t rms_percent;
    uint8_t peak_percent;
    uint8_t noise_floor_percent;
    int8_t waveform[FACTORY_AUDIO_WAVEFORM_BINS];
    uint8_t waveform_count;
    char status_text[128];
    char result_text[32];
} factory_audio_state_t;

bool factory_audio_init(void);
bool factory_audio_is_ready(void);
void factory_audio_start_monitor(void);
void factory_audio_record_3s(void);
void factory_audio_playback(void);
void factory_audio_start_loopback(void);
void factory_audio_stop(void);
void factory_audio_get_state(factory_audio_state_t *state);

#ifdef __cplusplus
}  // extern "C"
#endif
