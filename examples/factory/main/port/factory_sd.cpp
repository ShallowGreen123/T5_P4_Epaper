#include "factory_sd.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {

static const char *TAG = "factory_sd";

static bool s_initialized = false;
static bool s_spi_bus_ready = false;
static sdmmc_card_t *s_card = nullptr;
static factory_sd_state_t s_state = {};
static factory_sd_entry_info_t s_entries[FACTORY_SD_MAX_ENTRIES] = {};

static void copy_text(char *destination, size_t destination_size, const char *source)
{
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    snprintf(destination, destination_size, "%s", source);
}

static void set_status(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(s_state.status_text, sizeof(s_state.status_text), format, args);
    va_end(args);
}

static void clear_entries(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_state.entry_count = 0;
}

static void set_root_path(void)
{
    copy_text(s_state.current_path, sizeof(s_state.current_path), FACTORY_SD_MOUNT_POINT);
    s_state.at_root = true;
}

static void update_root_flag(void)
{
    s_state.at_root = strcmp(s_state.current_path, FACTORY_SD_MOUNT_POINT) == 0;
}

static int compare_text_case_insensitive(const char *lhs, const char *rhs)
{
    if (lhs == nullptr && rhs == nullptr) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }

    while (*lhs != '\0' && *rhs != '\0') {
        const int left = std::tolower(static_cast<unsigned char>(*lhs));
        const int right = std::tolower(static_cast<unsigned char>(*rhs));
        if (left != right) {
            return left - right;
        }
        ++lhs;
        ++rhs;
    }

    return std::tolower(static_cast<unsigned char>(*lhs)) -
           std::tolower(static_cast<unsigned char>(*rhs));
}

static bool compare_entries(const factory_sd_entry_info_t &lhs, const factory_sd_entry_info_t &rhs)
{
    if (lhs.is_directory != rhs.is_directory) {
        return lhs.is_directory && !rhs.is_directory;
    }

    return compare_text_case_insensitive(lhs.name, rhs.name) < 0;
}

static void reset_state(const char *status_text)
{
    s_state.mounted = false;
    clear_entries();
    set_root_path();
    set_status("%s", status_text);
}

static void unmount_sd_card(void)
{
    if (s_card != nullptr) {
        esp_vfs_fat_sdcard_unmount(FACTORY_SD_MOUNT_POINT, s_card);
        s_card = nullptr;
    }

    if (s_spi_bus_ready) {
        spi_bus_free(SDSPI_DEFAULT_HOST);
        s_spi_bus_ready = false;
    }

    s_state.mounted = false;
}

static void handle_access_failure(const char *operation, int errnum)
{
    const char *err_text = (errnum != 0) ? strerror(errnum) : "Unknown error";

    ESP_LOGW(TAG, "%s failed: %s", operation, err_text);
    unmount_sd_card();
    clear_entries();
    set_root_path();
    set_status("%s failed: %s. Insert SD card and tap Retry.", operation, err_text);
}

static bool mount_sd_card(void)
{
    if (s_card != nullptr) {
        s_state.mounted = true;
        return true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (gpio_num_t)FACTORY_SD_SPI_MOSI,
        .miso_io_num = (gpio_num_t)FACTORY_SD_SPI_MISO,
        .sclk_io_num = (gpio_num_t)FACTORY_SD_SPI_SCK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    const spi_host_device_t spi_host = static_cast<spi_host_device_t>(host.slot);
    esp_err_t ret = spi_bus_initialize(spi_host, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI bus init failed: %s", esp_err_to_name(ret));
        clear_entries();
        set_root_path();
        set_status("SDSPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }
    s_spi_bus_ready = true;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = spi_host;
    slot_config.gpio_cs = (gpio_num_t)FACTORY_SD_SPI_CS;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    ret = esp_vfs_fat_sdspi_mount(FACTORY_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        s_card = nullptr;
        clear_entries();
        set_root_path();
        set_status("SD mount failed: %s", esp_err_to_name(ret));
        if (s_spi_bus_ready) {
            spi_bus_free(spi_host);
            s_spi_bus_ready = false;
        }
        s_state.mounted = false;
        return false;
    }

    s_state.mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", FACTORY_SD_MOUNT_POINT);
    return true;
}

static bool refresh_directory(void)
{
    if (!mount_sd_card()) {
        return false;
    }

    DIR *dir = opendir(s_state.current_path);
    if (dir == nullptr) {
        handle_access_failure("Open directory", errno);
        return false;
    }

    clear_entries();

    size_t total_count = 0;
    size_t stored_count = 0;
    bool truncated = false;

    while (true) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) {
            if (errno != 0) {
                const int errnum = errno;
                closedir(dir);
                handle_access_failure("Read directory", errnum);
                return false;
            }
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char entry_path[FACTORY_SD_MAX_PATH_LEN] = {};
        const int written = snprintf(entry_path, sizeof(entry_path), "%s/%s", s_state.current_path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(entry_path)) {
            ESP_LOGW(TAG, "Skipping entry with path longer than %u bytes: %s",
                     (unsigned)sizeof(entry_path), entry->d_name);
            continue;
        }

        struct stat st = {};
        if (stat(entry_path, &st) != 0) {
            const int errnum = errno;
            closedir(dir);
            handle_access_failure("Stat entry", errnum);
            return false;
        }

        ++total_count;
        if (stored_count >= FACTORY_SD_MAX_ENTRIES) {
            truncated = true;
            continue;
        }

        factory_sd_entry_info_t &item = s_entries[stored_count];
        memset(&item, 0, sizeof(item));
        copy_text(item.name, sizeof(item.name), entry->d_name);
        copy_text(item.path, sizeof(item.path), entry_path);
        item.is_directory = S_ISDIR(st.st_mode);
        item.size_bytes = item.is_directory ? 0 : (uint64_t)st.st_size;
        copy_text(item.type, sizeof(item.type), item.is_directory ? "Directory" : "File");
        ++stored_count;
    }

    closedir(dir);

    std::sort(s_entries, s_entries + stored_count, compare_entries);
    s_state.entry_count = (uint16_t)stored_count;
    s_state.mounted = true;
    update_root_flag();

    if (truncated) {
        set_status("Showing %u of %u items.", (unsigned)stored_count, (unsigned)total_count);
    } else if (stored_count == 0) {
        set_status("Directory is empty.");
    } else {
        set_status("%u item%s in current directory.", (unsigned)stored_count, stored_count == 1 ? "" : "s");
    }

    return true;
}

}  // namespace

extern "C" void factory_sd_init(void)
{
    if (s_initialized) {
        return;
    }

    reset_state("SD page ready.");
    s_initialized = true;
}

extern "C" bool factory_sd_enter_root(void)
{
    factory_sd_init();
    set_root_path();
    return refresh_directory();
}

extern "C" void factory_sd_leave(void)
{
    factory_sd_init();
    unmount_sd_card();
    clear_entries();
    set_root_path();
    set_status("SD page ready.");
}

extern "C" bool factory_sd_refresh(void)
{
    factory_sd_init();
    return refresh_directory();
}

extern "C" bool factory_sd_go_parent(void)
{
    factory_sd_init();

    if (!s_state.mounted) {
        return refresh_directory();
    }

    if (s_state.at_root) {
        return refresh_directory();
    }

    const size_t root_len = strlen(FACTORY_SD_MOUNT_POINT);
    char *last_slash = strrchr(s_state.current_path, '/');
    if (last_slash == nullptr || (size_t)(last_slash - s_state.current_path) <= root_len) {
        set_root_path();
    } else {
        *last_slash = '\0';
        update_root_flag();
    }

    return refresh_directory();
}

extern "C" const factory_sd_state_t *factory_sd_get_state(void)
{
    factory_sd_init();
    return &s_state;
}

extern "C" size_t factory_sd_get_entry_count(void)
{
    factory_sd_init();
    return s_state.entry_count;
}

extern "C" const factory_sd_entry_info_t *factory_sd_get_entry(size_t index)
{
    factory_sd_init();
    if (index >= s_state.entry_count) {
        return nullptr;
    }
    return &s_entries[index];
}

extern "C" bool factory_sd_open_entry(size_t index)
{
    factory_sd_init();

    if (index >= s_state.entry_count) {
        set_status("Invalid selection.");
        return false;
    }

    const factory_sd_entry_info_t *entry = &s_entries[index];
    if (!entry->is_directory) {
        return true;
    }

    copy_text(s_state.current_path, sizeof(s_state.current_path), entry->path);
    update_root_flag();
    return refresh_directory();
}
