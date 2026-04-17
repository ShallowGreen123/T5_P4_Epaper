#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum factory_page_id {
    FACTORY_PAGE_HOME = 0,
    FACTORY_PAGE_DISPLAY,
    FACTORY_PAGE_TOUCH,
    FACTORY_PAGE_BATTERY,
    FACTORY_PAGE_WIFI,
    FACTORY_PAGE_SD,
    FACTORY_PAGE_GPS,
    FACTORY_PAGE_LORA,
    FACTORY_PAGE_DEVICE,
} factory_page_id_t;

typedef struct factory_display_mode_info {
    uint16_t width;
    uint16_t height;
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

typedef struct factory_placeholder_info {
    factory_page_id_t page_id;
    const char *title;
    const char *summary;
    const char *detail;
    const char *status_text;
} factory_placeholder_info_t;

#ifdef __cplusplus
}  // extern "C"
#endif
