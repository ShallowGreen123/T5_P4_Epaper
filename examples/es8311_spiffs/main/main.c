#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "audio_player.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "board_es8311.h"
#include "config.h"

static const char *TAG = "main";

static SemaphoreHandle_t s_playback_done;
static bool s_playback_started;

static bool has_supported_audio_extension(const char *name)
{
    size_t len = strlen(name);

    if (len >= 4 && strcasecmp(name + len - 4, ".mp3") == 0) {
        return true;
    }
    if (len >= 4 && strcasecmp(name + len - 4, ".wav") == 0) {
        return true;
    }
    return false;
}

static void log_spiffs_files(void)
{
    DIR *dir = opendir(EXAMPLE_SPIFFS_BASE_PATH);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Failed to open %s", EXAMPLE_SPIFFS_BASE_PATH);
        return;
    }

    ESP_LOGI(TAG, "Listing files in %s", EXAMPLE_SPIFFS_BASE_PATH);

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[EXAMPLE_MAX_FILE_PATH];
        struct stat st = {0};
        snprintf(path, sizeof(path), EXAMPLE_SPIFFS_BASE_PATH "/%s", entry->d_name);
        if (stat(path, &st) == 0) {
            ESP_LOGI(TAG, "  %s (%ld bytes)", path, (long)st.st_size);
        } else {
            ESP_LOGI(TAG, "  %s", path);
        }
    }

    closedir(dir);
}

static esp_err_t find_first_audio_file(char *path, size_t path_len)
{
    DIR *dir = opendir(EXAMPLE_SPIFFS_BASE_PATH);
    if (dir == NULL) {
        return ESP_FAIL;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!has_supported_audio_extension(entry->d_name)) {
            continue;
        }

        int written = snprintf(path, path_len, EXAMPLE_SPIFFS_BASE_PATH "/%s", entry->d_name);
        closedir(dir);
        return (written > 0 && (size_t)written < path_len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }

    closedir(dir);
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t mount_spiffs(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = EXAMPLE_SPIFFS_BASE_PATH,
        .partition_label = EXAMPLE_SPIFFS_PARTITION_LABEL,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&conf), TAG, "Mount SPIFFS failed");

    size_t total = 0;
    size_t used = 0;
    ESP_RETURN_ON_ERROR(esp_spiffs_info(EXAMPLE_SPIFFS_PARTITION_LABEL, &total, &used),
                        TAG, "Query SPIFFS info failed");

    ESP_LOGI(TAG, "SPIFFS mounted: total=%u used=%u", (unsigned)total, (unsigned)used);
    return ESP_OK;
}

static void unmount_spiffs(void)
{
    esp_vfs_spiffs_unregister(EXAMPLE_SPIFFS_PARTITION_LABEL);
}

static void audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        s_playback_started = true;
        ESP_LOGI(TAG, "Playback started");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
        ESP_LOGI(TAG, "Switching to next track");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        ESP_LOGI(TAG, "Playback paused");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        ESP_LOGI(TAG, "Playback idle");
        if (s_playback_started && s_playback_done != NULL) {
            xSemaphoreGive(s_playback_done);
        }
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
        ESP_LOGE(TAG, "Unsupported audio file type");
        if (s_playback_done != NULL) {
            xSemaphoreGive(s_playback_done);
        }
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
        ESP_LOGI(TAG, "Audio player shutdown");
        break;
    default:
        ESP_LOGI(TAG, "Audio player event: %d", (int)ctx->audio_event);
        break;
    }
}

void app_main(void)
{
    char audio_path[EXAMPLE_MAX_FILE_PATH] = {0};
    FILE *audio_file = NULL;

    s_playback_done = xSemaphoreCreateBinary();
    if (s_playback_done == NULL) {
        ESP_LOGE(TAG, "Create playback semaphore failed");
        return;
    }

    ESP_ERROR_CHECK(mount_spiffs());
    log_spiffs_files();

    if (find_first_audio_file(audio_path, sizeof(audio_path)) != ESP_OK) {
        ESP_LOGE(TAG, "No supported audio file found in %s", EXAMPLE_SPIFFS_BASE_PATH);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Selected audio file: %s", audio_path);

    ESP_ERROR_CHECK(board_audio_player_init());
    ESP_ERROR_CHECK(audio_player_callback_register(audio_player_callback, NULL));

    audio_file = fopen(audio_path, "rb");
    if (audio_file == NULL) {
        ESP_LOGE(TAG, "Open %s failed", audio_path);
        goto cleanup;
    }

    s_playback_started = false;
    if (audio_player_play(audio_file) != ESP_OK) {
        ESP_LOGE(TAG, "Queue playback request failed");
        fclose(audio_file);
        audio_file = NULL;
        goto cleanup;
    }
    audio_file = NULL;

    if (xSemaphoreTake(s_playback_done, pdMS_TO_TICKS(EXAMPLE_PLAYBACK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Playback timeout");
        audio_player_stop();
        goto cleanup;
    }

    ESP_LOGI(TAG, "Playback completed");

cleanup:
    board_audio_player_deinit();
    unmount_spiffs();

    if (s_playback_done != NULL) {
        vSemaphoreDelete(s_playback_done);
        s_playback_done = NULL;
    }
}
