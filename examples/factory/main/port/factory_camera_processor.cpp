#include "factory_camera_processor.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "linux/videodev2.h"

namespace {

static const char *TAG = "factory_cam_proc";

constexpr uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static void *alloc_image_buffer(size_t size)
{
    void *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
        buffer = malloc(size);
    }
    return buffer;
}

static uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint8_t)(((uint16_t)r * 77U + (uint16_t)g * 150U + (uint16_t)b * 29U) >> 8);
}

}  // namespace

extern "C" bool factory_camera_convert_to_grayscale(
    const void *input,
    uint8_t *gray,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format)
{
    if (input == nullptr || gray == nullptr || width == 0 || height == 0) {
        return false;
    }

    const uint8_t *src = static_cast<const uint8_t *>(input);
    const size_t pixel_count = (size_t)width * height;

    switch (pixel_format) {
        case V4L2_PIX_FMT_GREY:
            memcpy(gray, src, pixel_count);
            return true;

        case V4L2_PIX_FMT_RGB565:
            for (size_t i = 0; i < pixel_count; ++i) {
                const uint16_t pixel = (uint16_t)src[i * 2U] | ((uint16_t)src[i * 2U + 1U] << 8);
                const uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1F);
                const uint8_t g6 = (uint8_t)((pixel >> 5) & 0x3F);
                const uint8_t b5 = (uint8_t)(pixel & 0x1F);
                const uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
                const uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
                const uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
                gray[i] = rgb_to_gray(r8, g8, b8);
            }
            return true;

        case V4L2_PIX_FMT_RGB24:
            for (size_t i = 0; i < pixel_count; ++i) {
                gray[i] = rgb_to_gray(src[i * 3U], src[i * 3U + 1U], src[i * 3U + 2U]);
            }
            return true;

        case V4L2_PIX_FMT_UYVY:
            for (uint32_t y = 0; y < height; ++y) {
                const uint8_t *row = src + (size_t)y * width * 2U;
                uint8_t *dst = gray + (size_t)y * width;
                for (uint32_t x = 0; x < width; ++x) {
                    dst[x] = row[x * 2U + 1U];
                }
            }
            return true;

        case V4L2_PIX_FMT_YUYV:
            for (uint32_t y = 0; y < height; ++y) {
                const uint8_t *row = src + (size_t)y * width * 2U;
                uint8_t *dst = gray + (size_t)y * width;
                for (uint32_t x = 0; x < width; ++x) {
                    dst[x] = row[x * 2U];
                }
            }
            return true;

        default:
            ESP_LOGW(TAG, "unsupported camera pixel format 0x%08" PRIx32, pixel_format);
            return false;
    }
}

extern "C" bool factory_camera_scale_nearest(
    const uint8_t *src,
    uint32_t src_w,
    uint32_t src_h,
    uint8_t *dst,
    uint32_t dst_w,
    uint32_t dst_h)
{
    if (src == nullptr || dst == nullptr || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
        return false;
    }

    for (uint32_t y = 0; y < dst_h; ++y) {
        const uint32_t src_y = (uint32_t)(((uint64_t)y * src_h) / dst_h);
        const uint8_t *src_row = src + (size_t)src_y * src_w;
        uint8_t *dst_row = dst + (size_t)y * dst_w;
        for (uint32_t x = 0; x < dst_w; ++x) {
            const uint32_t src_x = (uint32_t)(((uint64_t)x * src_w) / dst_w);
            dst_row[x] = src_row[src_x];
        }
    }

    return true;
}

extern "C" bool factory_camera_dither_bayer4(
    const uint8_t *gray,
    uint8_t *output_1bpp,
    uint32_t width,
    uint32_t height)
{
    if (gray == nullptr || output_1bpp == nullptr || width == 0 || height == 0) {
        return false;
    }

    const size_t pitch = (width + 7U) / 8U;
    memset(output_1bpp, 0, pitch * height);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *src_row = gray + (size_t)y * width;
        uint8_t *dst_row = output_1bpp + (size_t)y * pitch;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t threshold = (uint8_t)(kBayer4[y & 0x3U][x & 0x3U] * 16U + 8U);
            const bool is_white = src_row[x] >= threshold;
            if (is_white) {
                dst_row[x >> 3] |= (uint8_t)(0x80U >> (x & 0x7U));
            }
        }
    }

    return true;
}

extern "C" bool factory_camera_process_frame(
    const void *input,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    void *output_1bpp,
    uint32_t out_width,
    uint32_t out_height)
{
    if (input == nullptr || output_1bpp == nullptr || width == 0 || height == 0 || out_width == 0 || out_height == 0) {
        return false;
    }

    const size_t src_pixels = (size_t)width * height;
    const size_t dst_pixels = (size_t)out_width * out_height;
    uint8_t *gray = static_cast<uint8_t *>(alloc_image_buffer(src_pixels));
    if (gray == nullptr) {
        ESP_LOGE(TAG, "failed to allocate grayscale buffer (%u x %u)", (unsigned)width, (unsigned)height);
        return false;
    }

    bool ok = factory_camera_convert_to_grayscale(input, gray, width, height, pixel_format);
    if (!ok) {
        free(gray);
        return false;
    }

    if (width == out_width && height == out_height) {
        ok = factory_camera_dither_bayer4(gray, static_cast<uint8_t *>(output_1bpp), out_width, out_height);
        free(gray);
        return ok;
    }

    uint8_t *scaled = static_cast<uint8_t *>(alloc_image_buffer(dst_pixels));
    if (scaled == nullptr) {
        ESP_LOGE(TAG, "failed to allocate scaled buffer (%u x %u)", (unsigned)out_width, (unsigned)out_height);
        free(gray);
        return false;
    }

    ok = factory_camera_scale_nearest(gray, width, height, scaled, out_width, out_height);
    if (ok) {
        ok = factory_camera_dither_bayer4(scaled, static_cast<uint8_t *>(output_1bpp), out_width, out_height);
    }

    free(scaled);
    free(gray);
    return ok;
}
