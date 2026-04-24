#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct factory_camera_frame_info {
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t bytes_per_frame;
    uint32_t frame_rate;
} factory_camera_frame_info_t;

bool factory_camera_power_on(void);
void factory_camera_power_off(void);
bool factory_camera_init(void);
void factory_camera_deinit(void);
bool factory_camera_capture(void *out_buf, size_t buf_size, uint32_t *out_len);
bool factory_camera_get_frame_info(factory_camera_frame_info_t *out_info);
bool factory_camera_is_detected(void);
uint32_t factory_camera_frame_size(uint32_t width, uint32_t height, uint32_t pixel_format);
const char *factory_camera_pixel_format_name(uint32_t pixel_format);

#ifdef __cplusplus
}  // extern "C"
#endif
