#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool factory_camera_process_frame(
    const void *input,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    void *output_1bpp,
    uint32_t out_width,
    uint32_t out_height);

bool factory_camera_convert_to_grayscale(
    const void *input,
    uint8_t *gray,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format);

bool factory_camera_scale_nearest(
    const uint8_t *src,
    uint32_t src_w,
    uint32_t src_h,
    uint8_t *dst,
    uint32_t dst_w,
    uint32_t dst_h);

bool factory_camera_dither_bayer4(
    const uint8_t *gray,
    uint8_t *output_1bpp,
    uint32_t width,
    uint32_t height);

#ifdef __cplusplus
}  // extern "C"
#endif
