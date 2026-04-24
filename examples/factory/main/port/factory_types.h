#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum factory_page_id {
    FACTORY_PAGE_HOME = 0,
    FACTORY_PAGE_CLOCK,
    FACTORY_PAGE_LORA,
    FACTORY_PAGE_SD,
    FACTORY_PAGE_SETTING,
    FACTORY_PAGE_TEST,
    FACTORY_PAGE_WIFI,
    FACTORY_PAGE_BATTERY,
    FACTORY_PAGE_GPS,
    FACTORY_PAGE_ADJUST,
    FACTORY_PAGE_SHUTDOWN,
    FACTORY_PAGE_SLEEP,
    FACTORY_PAGE_DISPLAY,
    FACTORY_PAGE_TOUCH,
    FACTORY_PAGE_AUDIO,
    FACTORY_PAGE_CAMERA,
    FACTORY_PAGE_HDMI,
} factory_page_id_t;

#define FACTORY_SD_MAX_ENTRIES 64
#define FACTORY_SD_MAX_NAME_LEN 128
#define FACTORY_SD_MAX_PATH_LEN 256
#define FACTORY_SD_MAX_TYPE_LEN 16
#define FACTORY_SD_MAX_STATUS_LEN 160

typedef struct factory_display_mode_info {
    uint16_t width;
    uint16_t height;
    uint16_t native_width;
    uint16_t native_height;
    uint16_t rotation_deg;
    uint8_t mirror_mode;
    uint8_t partial_passes;
    uint8_t full_passes;
    const char *mode_name;
    const char *mode_summary;
} factory_display_mode_info_t;

typedef struct factory_touch_diag_state {
    bool ready;
    bool pressed;
    int16_t x;
    int16_t y;
    int16_t raw_x;
    int16_t raw_y;
    uint32_t sample_count;
    const char *status_text;
} factory_touch_diag_state_t;

typedef struct factory_runtime_info {
    const char *app_name;
    const char *app_version;
    const char *target;
    uint16_t width;
    uint16_t height;
    bool touch_ready;
    const char *display_mode;
    const char *touch_status;
    const char *boot_mode;
} factory_runtime_info_t;

typedef struct factory_page_info {
    factory_page_id_t page_id;
    const char *title;
    const char *summary;
    const char *detail;
    const char *status_text;
} factory_page_info_t;

typedef enum factory_hdmi_mode {
    FACTORY_HDMI_MODE_PATTERN = 0,
    FACTORY_HDMI_MODE_MOTION,
    FACTORY_HDMI_MODE_CAMERA,
    FACTORY_HDMI_MODE_AUDIO,
    FACTORY_HDMI_MODE_SD_VIDEO,
} factory_hdmi_mode_t;

typedef struct factory_hdmi_state {
    bool initialized;
    bool powered;
    bool ready;
    bool running;
    factory_hdmi_mode_t mode;
    uint16_t width;
    uint16_t height;
    uint32_t frame_count;
    uint32_t fps;
    uint32_t free_psram;
    const char *status_text;
    const char *last_error;
} factory_hdmi_state_t;

typedef struct factory_sd_state {
    bool mounted;
    bool at_root;
    uint16_t entry_count;
    char current_path[FACTORY_SD_MAX_PATH_LEN];
    char status_text[FACTORY_SD_MAX_STATUS_LEN];
} factory_sd_state_t;

typedef struct factory_sd_entry_info {
    bool is_directory;
    uint64_t size_bytes;
    char name[FACTORY_SD_MAX_NAME_LEN];
    char path[FACTORY_SD_MAX_PATH_LEN];
    char type[FACTORY_SD_MAX_TYPE_LEN];
} factory_sd_entry_info_t;

#ifdef __cplusplus
}  // extern "C"
#endif
