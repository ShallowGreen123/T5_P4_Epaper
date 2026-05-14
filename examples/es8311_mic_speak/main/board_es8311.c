#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"

#include "board_es8311.h"
#include "config.h"

static const char *TAG = "board_es8311";

static esp_codec_dev_handle_t s_play_dev;
static esp_codec_dev_handle_t s_record_dev;
static bool s_audio_ready;

static esp_err_t board_codecs_open(void)
{
    if (s_audio_ready) {
        return ESP_OK;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = EXAMPLE_SAMPLE_RATE,
        .bits_per_sample = 16,
        .channel = 2,
    };

    s_play_dev = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_play_dev != NULL, ESP_FAIL, TAG, "Create speaker codec handle failed");

    s_record_dev = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(s_record_dev != NULL, ESP_FAIL, TAG, "Create microphone codec handle failed");

    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_play_dev, &sample_info), TAG, "Open speaker codec failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_record_dev, &sample_info), TAG, "Open microphone codec failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_play_dev, EXAMPLE_VOICE_VOLUME), TAG, "Set output volume failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_record_dev, EXAMPLE_MIC_GAIN), TAG, "Set microphone gain failed");

    s_audio_ready = true;
    ESP_LOGI(TAG, "ES8311 ready at %d Hz, volume=%d, mic_gain=%d",
             EXAMPLE_SAMPLE_RATE, EXAMPLE_VOICE_VOLUME, EXAMPLE_MIC_GAIN);
    return ESP_OK;
}

esp_err_t board_audio_amp_set(bool enable)
{
    ESP_RETURN_ON_ERROR(t5_board_audio_select_speaker(false), TAG, "Select speaker path failed");
    ESP_RETURN_ON_ERROR(t5_board_audio_amp_enable(enable), TAG, "Set audio amplifier state failed");
    ESP_LOGI(TAG, "Audio amplifier %s through XL9555 P06", enable ? "enabled" : "disabled");
    return ESP_OK;
}

static void mic_loopback_task(void *args)
{
    uint8_t *audio_buffer = (uint8_t *)calloc(1, EXAMPLE_RECV_BUF_SIZE);

    (void)args;

    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Allocate audio buffer failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Microphone loopback started");

    while (1) {
        esp_err_t ret = esp_codec_dev_read(s_record_dev, audio_buffer, EXAMPLE_RECV_BUF_SIZE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Codec read failed: %s", esp_err_to_name(ret));
            continue;
        }

        ret = esp_codec_dev_write(s_play_dev, audio_buffer, EXAMPLE_RECV_BUF_SIZE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Codec write failed: %s", esp_err_to_name(ret));
            continue;
        }
    }
}

void es8311_start(void)
{
    ESP_LOGI(TAG, "Starting ES8311 mic-to-speaker loopback");

    ESP_ERROR_CHECK(board_audio_amp_set(true));
    ESP_ERROR_CHECK(board_codecs_open());

    xTaskCreate(mic_loopback_task, "mic_loopback", 4096, NULL, 5, NULL);
}
