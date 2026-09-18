#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint16_t EPD_VIDEO_WIDTH = 1440;
constexpr uint16_t EPD_VIDEO_HEIGHT = 720;
constexpr size_t EPD_VIDEO_ROW_BYTES = EPD_VIDEO_WIDTH / 8U;
constexpr size_t EPD_VIDEO_FRAME_BYTES = EPD_VIDEO_ROW_BYTES * EPD_VIDEO_HEIGHT;

bool epd_video_init();
bool epd_video_power_on();
bool epd_video_start();
uint8_t *epd_video_get_backbuffer();
size_t epd_video_get_backbuffer_size();
bool epd_video_submit(uint16_t dirty_y, uint16_t dirty_height);
bool epd_video_can_submit();
bool epd_video_submit_pending();
uint32_t epd_video_get_vsync_count();
uint32_t epd_video_get_submit_count();
void epd_video_shutdown();
