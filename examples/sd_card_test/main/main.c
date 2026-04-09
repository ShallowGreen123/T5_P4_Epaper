/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

#define SD_TEST_MAX_PATH_LEN 128
#define SD_TEST_MAX_DATA_LEN 256
#define SD_TEST_FALLBACK_FILE_NAME "sdtest.txt"
#define SD_TEST_SPEED_FALLBACK_FILE_NAME "sdbench.bin"

#ifndef CONFIG_SD_CARD_TEST_SPEED_FILE_NAME
#define CONFIG_SD_CARD_TEST_SPEED_FILE_NAME SD_TEST_SPEED_FALLBACK_FILE_NAME
#endif

#ifndef CONFIG_SD_CARD_TEST_SPEED_SIZE_KB
#define CONFIG_SD_CARD_TEST_SPEED_SIZE_KB 1024
#endif

#ifndef CONFIG_SD_CARD_TEST_SPEED_BLOCK_SIZE
#define CONFIG_SD_CARD_TEST_SPEED_BLOCK_SIZE 16384
#endif

static const char *TAG = "sd_card_test";

static const char *card_type_to_string(const sdmmc_card_t *card)
{
    if (card->is_sdio && card->is_mem) {
        return "SDIO/SD combo";
    }
    if (card->is_sdio) {
        return "SDIO";
    }
    if (card->is_mmc) {
        return "MMC/eMMC";
    }
    if ((card->ocr & SD_OCR_SDHC_CAP) != 0) {
        return "SDHC/SDXC";
    }
    return "SDSC";
}

static double bytes_to_mib(uint64_t bytes)
{
    return (double)bytes / (1024.0 * 1024.0);
}

static double bytes_to_gib(uint64_t bytes)
{
    return (double)bytes / (1024.0 * 1024.0 * 1024.0);
}

static bool is_fat_8_3_char(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_' || c == '-' || c == '$' || c == '~' || c == '!';
}

static bool is_fat_8_3_name(const char *name)
{
    size_t base_len = 0;
    size_t ext_len = 0;
    bool in_ext = false;

    if (name == NULL || name[0] == '\0') {
        return false;
    }

    for (const char *p = name; *p != '\0'; ++p) {
        const char c = *p;
        if (c == '/' || c == '\\' || c == ':') {
            return false;
        }
        if (c == '.') {
            if (in_ext || base_len == 0) {
                return false;
            }
            in_ext = true;
            continue;
        }
        if (!is_fat_8_3_char(c)) {
            return false;
        }
        if (in_ext) {
            if (++ext_len > 3) {
                return false;
            }
        } else if (++base_len > 8) {
            return false;
        }
    }

    return base_len > 0 && (!in_ext || ext_len > 0);
}

static const char *select_fatfs_file_name(const char *configured_name, const char *fallback_name)
{
#if CONFIG_FATFS_LFN_NONE
    if (!is_fat_8_3_name(configured_name)) {
        ESP_LOGW(TAG,
                 "FATFS long filename support is disabled; using 8.3 file name %s instead of %s",
                 fallback_name, configured_name);
        return fallback_name;
    }
#endif
    return configured_name;
}

static void log_card_info(const sdmmc_card_t *card)
{
    const uint64_t capacity_bytes = (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    const char *bus_mode = (card->host.flags & SDMMC_HOST_FLAG_SPI) ? "SDSPI" : "SDMMC";

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "Card type: %s", card_type_to_string(card));
    ESP_LOGI(TAG, "Card capacity: %" PRIu64 " bytes (%.2f MiB, %.2f GiB)",
             capacity_bytes, bytes_to_mib(capacity_bytes), bytes_to_gib(capacity_bytes));
    ESP_LOGI(TAG, "Sector count: %d, sector size: %d bytes",
             card->csd.capacity, card->csd.sector_size);
    ESP_LOGI(TAG, "Bus: %s, real clock: %d kHz, max clock: %" PRIu32 " kHz",
             bus_mode, card->real_freq_khz, card->max_freq_khz);
}

static void log_filesystem_info(void)
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info(CONFIG_SD_CARD_TEST_MOUNT_POINT, &total_bytes, &free_bytes);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to query FAT filesystem info: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "FAT filesystem: total=%" PRIu64 " bytes (%.2f MiB), free=%" PRIu64 " bytes (%.2f MiB)",
             total_bytes, bytes_to_mib(total_bytes), free_bytes, bytes_to_mib(free_bytes));
}

static esp_err_t make_file_path(char *path, size_t path_size, const char *file_name)
{
    int len = snprintf(path, path_size, "%s/%s", CONFIG_SD_CARD_TEST_MOUNT_POINT, file_name);
    if (len < 0 || (size_t)len >= path_size) {
        ESP_LOGE(TAG, "Test file path is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t make_test_payload(const sdmmc_card_t *card, char *payload, size_t payload_size)
{
    const uint64_t capacity_bytes = (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    int len = snprintf(payload, payload_size,
                       "T5-P4 E-Paper SD card test\r\n"
                       "type=%s\r\n"
                       "capacity_bytes=%" PRIu64 "\r\n"
                       "sector_size=%d\r\n",
                       card_type_to_string(card), capacity_bytes, card->csd.sector_size);

    if (len < 0 || (size_t)len >= payload_size) {
        ESP_LOGE(TAG, "Test payload is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t write_file(const char *path, const char *data)
{
    ESP_LOGI(TAG, "Writing test file: %s", path);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for writing: errno=%d (%s)", path, errno, strerror(errno));
        return (errno == EINVAL) ? ESP_ERR_INVALID_ARG : ESP_FAIL;
    }

    const size_t data_len = strlen(data);
    const size_t written = fwrite(data, 1, data_len, file);
    if (written != data_len) {
        ESP_LOGE(TAG, "Short write: wrote %u of %u bytes", (unsigned)written, (unsigned)data_len);
        fclose(file);
        return ESP_FAIL;
    }

    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Failed to close %s after writing: errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wrote %u bytes", (unsigned)data_len);
    return ESP_OK;
}

static esp_err_t read_file(const char *path, char *data, size_t data_size, size_t *out_len)
{
    ESP_LOGI(TAG, "Reading test file: %s", path);

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for reading: errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }

    const size_t read_len = fread(data, 1, data_size - 1, file);
    if (ferror(file)) {
        ESP_LOGE(TAG, "Failed to read %s: errno=%d (%s)", path, errno, strerror(errno));
        fclose(file);
        return ESP_FAIL;
    }

    data[read_len] = '\0';
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Failed to close %s after reading: errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }

    if (out_len != NULL) {
        *out_len = read_len;
    }
    ESP_LOGI(TAG, "Read %u bytes", (unsigned)read_len);
    return ESP_OK;
}

static esp_err_t verify_file_data(const char *expected, const char *actual, size_t actual_len)
{
    const size_t expected_len = strlen(expected);
    if (actual_len != expected_len || memcmp(expected, actual, expected_len) != 0) {
        ESP_LOGE(TAG, "File data mismatch");
        ESP_LOGE(TAG, "Expected %u bytes: %s", (unsigned)expected_len, expected);
        ESP_LOGE(TAG, "Actual %u bytes: %s", (unsigned)actual_len, actual);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File read/write verification passed");
    return ESP_OK;
}

static void log_file_stat(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "Failed to stat %s: errno=%d (%s)", path, errno, strerror(errno));
        return;
    }

    ESP_LOGI(TAG, "Verified file size from stat: %ld bytes", (long)st.st_size);
}

static void fill_speed_buffer(uint8_t *buffer, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = (uint8_t)((i * 31U) + 17U);
    }
}

static double throughput_mib_s(uint64_t bytes, int64_t elapsed_us)
{
    if (elapsed_us <= 0) {
        return 0.0;
    }

    return ((double)bytes * 1000000.0) / ((double)elapsed_us * 1024.0 * 1024.0);
}

static esp_err_t run_speed_test(const char *path)
{
#if CONFIG_SD_CARD_TEST_SPEED_TEST
    const size_t block_size = CONFIG_SD_CARD_TEST_SPEED_BLOCK_SIZE;
    const uint64_t total_bytes = (uint64_t)CONFIG_SD_CARD_TEST_SPEED_SIZE_KB * 1024ULL;
    uint8_t *buffer = malloc(block_size);
    uint64_t processed = 0;
    int64_t start_us = 0;
    int64_t elapsed_us = 0;
    uint32_t checksum = 0;

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u-byte speed test buffer", (unsigned)block_size);
        return ESP_ERR_NO_MEM;
    }

    fill_speed_buffer(buffer, block_size);
    ESP_LOGI(TAG, "Running SD speed test: file=%s, size=%" PRIu64 " KiB, block=%u bytes",
             path, total_bytes / 1024ULL, (unsigned)block_size);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for speed write: errno=%d (%s)", path, errno, strerror(errno));
        free(buffer);
        return (errno == EINVAL) ? ESP_ERR_INVALID_ARG : ESP_FAIL;
    }

    start_us = esp_timer_get_time();
    while (processed < total_bytes) {
        const size_t chunk = (total_bytes - processed > block_size) ? block_size : (size_t)(total_bytes - processed);
        if (fwrite(buffer, 1, chunk, file) != chunk) {
            ESP_LOGE(TAG, "Speed write failed at %" PRIu64 " bytes: errno=%d (%s)",
                     processed, errno, strerror(errno));
            fclose(file);
            free(buffer);
            return ESP_FAIL;
        }
        processed += chunk;
    }
    if (fflush(file) != 0) {
        ESP_LOGE(TAG, "Speed write flush failed: errno=%d (%s)", errno, strerror(errno));
        fclose(file);
        free(buffer);
        return ESP_FAIL;
    }
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Speed write close failed: errno=%d (%s)", errno, strerror(errno));
        free(buffer);
        return ESP_FAIL;
    }
    elapsed_us = esp_timer_get_time() - start_us;
    ESP_LOGI(TAG, "SD write speed: %.2f MiB/s (%" PRIu64 " bytes in %" PRId64 " us)",
             throughput_mib_s(total_bytes, elapsed_us), total_bytes, elapsed_us);

    file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for speed read: errno=%d (%s)", path, errno, strerror(errno));
        free(buffer);
        return ESP_FAIL;
    }

    processed = 0;
    start_us = esp_timer_get_time();
    while (processed < total_bytes) {
        const size_t chunk = (total_bytes - processed > block_size) ? block_size : (size_t)(total_bytes - processed);
        const size_t read_len = fread(buffer, 1, chunk, file);
        if (read_len != chunk) {
            ESP_LOGE(TAG, "Speed read failed at %" PRIu64 " bytes: read=%u expected=%u errno=%d (%s)",
                     processed, (unsigned)read_len, (unsigned)chunk, errno, strerror(errno));
            fclose(file);
            free(buffer);
            return ESP_FAIL;
        }
        checksum += buffer[0] + buffer[read_len - 1];
        processed += read_len;
    }
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Speed read close failed: errno=%d (%s)", errno, strerror(errno));
        free(buffer);
        return ESP_FAIL;
    }
    elapsed_us = esp_timer_get_time() - start_us;
    ESP_LOGI(TAG, "SD read speed: %.2f MiB/s (%" PRIu64 " bytes in %" PRId64 " us, checksum=%" PRIu32 ")",
             throughput_mib_s(total_bytes, elapsed_us), total_bytes, elapsed_us, checksum);

    if (remove(path) != 0) {
        ESP_LOGW(TAG, "Failed to remove speed test file %s: errno=%d (%s)", path, errno, strerror(errno));
    }

    free(buffer);
#else
    (void)path;
#endif
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t ret = ESP_OK;
    sdmmc_card_t *card = NULL;
    bool spi_bus_ready = false;
    char file_path[SD_TEST_MAX_PATH_LEN] = {0};
    char speed_file_path[SD_TEST_MAX_PATH_LEN] = {0};
    char expected_data[SD_TEST_MAX_DATA_LEN] = {0};
    char read_data[SD_TEST_MAX_DATA_LEN] = {0};
    size_t read_len = 0;
    const char *test_file_name = select_fatfs_file_name(CONFIG_SD_CARD_TEST_FILE_NAME, SD_TEST_FALLBACK_FILE_NAME);
    const char *speed_file_name = select_fatfs_file_name(CONFIG_SD_CARD_TEST_SPEED_FILE_NAME,
                                                         SD_TEST_SPEED_FALLBACK_FILE_NAME);

    ESP_LOGI(TAG, "Starting standalone SD card test");
    ESP_LOGI(TAG, "Pins: MISO=%d MOSI=%d CLK=%d CS=%d",
             CONFIG_SD_CARD_TEST_PIN_MISO, CONFIG_SD_CARD_TEST_PIN_MOSI,
             CONFIG_SD_CARD_TEST_PIN_CLK, CONFIG_SD_CARD_TEST_PIN_CS);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = CONFIG_SD_CARD_TEST_MAX_FREQ_KHZ;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (gpio_num_t)CONFIG_SD_CARD_TEST_PIN_MOSI,
        .miso_io_num = (gpio_num_t)CONFIG_SD_CARD_TEST_PIN_MISO,
        .sclk_io_num = (gpio_num_t)CONFIG_SD_CARD_TEST_PIN_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    ESP_LOGI(TAG, "Initializing SDSPI bus");
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SDSPI bus: %s", esp_err_to_name(ret));
        return;
    }
    spi_bus_ready = true;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = (gpio_num_t)CONFIG_SD_CARD_TEST_PIN_CS;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_SD_CARD_TEST_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Mounting SD card at %s over SDSPI", CONFIG_SD_CARD_TEST_MOUNT_POINT);
    ret = esp_vfs_fat_sdspi_mount(CONFIG_SD_CARD_TEST_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount FAT filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
            ESP_LOGE(TAG, "Check card insertion, power, pin mapping, and SD pull-ups");
        }
        goto cleanup;
    }

    log_card_info(card);
    log_filesystem_info();

    ret = make_file_path(file_path, sizeof(file_path), test_file_name);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = make_test_payload(card, expected_data, sizeof(expected_data));
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = write_file(file_path, expected_data);
    if (ret == ESP_ERR_INVALID_ARG && strcmp(test_file_name, SD_TEST_FALLBACK_FILE_NAME) != 0) {
        ESP_LOGW(TAG, "The configured file name may need FATFS long filename support; retrying %s",
                 SD_TEST_FALLBACK_FILE_NAME);
        ret = make_file_path(file_path, sizeof(file_path), SD_TEST_FALLBACK_FILE_NAME);
        if (ret != ESP_OK) {
            goto cleanup;
        }
        ret = write_file(file_path, expected_data);
    }
    if (ret != ESP_OK) {
        goto cleanup;
    }

    log_file_stat(file_path);

    ret = read_file(file_path, read_data, sizeof(read_data), &read_len);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = verify_file_data(expected_data, read_data, read_len);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(TAG, "SD card test finished successfully. Test file remains at %s", file_path);

    ret = make_file_path(speed_file_path, sizeof(speed_file_path), speed_file_name);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = run_speed_test(speed_file_path);
    if (ret == ESP_ERR_INVALID_ARG && strcmp(speed_file_name, SD_TEST_SPEED_FALLBACK_FILE_NAME) != 0) {
        ESP_LOGW(TAG, "The configured speed file name may need FATFS long filename support; retrying %s",
                 SD_TEST_SPEED_FALLBACK_FILE_NAME);
        ret = make_file_path(speed_file_path, sizeof(speed_file_path), SD_TEST_SPEED_FALLBACK_FILE_NAME);
        if (ret != ESP_OK) {
            goto cleanup;
        }
        ret = run_speed_test(speed_file_path);
    }
    if (ret != ESP_OK) {
        goto cleanup;
    }

cleanup:
    if (card != NULL) {
        esp_err_t unmount_ret = esp_vfs_fat_sdcard_unmount(CONFIG_SD_CARD_TEST_MOUNT_POINT, card);
        if (unmount_ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card unmounted");
        } else {
            ESP_LOGW(TAG, "SD card unmount failed: %s", esp_err_to_name(unmount_ret));
        }
    }

    if (spi_bus_ready) {
        esp_err_t free_ret = spi_bus_free(host.slot);
        if (free_ret == ESP_OK) {
            ESP_LOGI(TAG, "SDSPI bus released");
        } else {
            ESP_LOGW(TAG, "SDSPI bus release failed: %s", esp_err_to_name(free_ret));
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card test failed: %s", esp_err_to_name(ret));
    }
}
