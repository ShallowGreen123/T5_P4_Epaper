#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "epd_video.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "epd_refresh_demo";
constexpr int kAnimationTop = 120;
constexpr int kAnimationBottom = 650;
constexpr int kAnimationFrames = 240;
constexpr int kQualityHoldMs = 1800;

struct Canvas {
    uint8_t *pixels;

    void clear(bool white = true) const
    {
        // The demo intentionally uses the opposite polarity: black canvas,
        // white graphics. The raw driver still receives the normal 1-bit
        // framebuffer convention (bit 1 = white, bit 0 = black).
        memset(pixels, white ? 0x00 : 0xFF, EPD_VIDEO_FRAME_BYTES);
    }

    void set_pixel(int x, int y, bool black) const
    {
        if (x < 0 || y < 0 || x >= EPD_VIDEO_WIDTH || y >= EPD_VIDEO_HEIGHT) {
            return;
        }
        uint8_t &value = pixels[static_cast<size_t>(y) * EPD_VIDEO_ROW_BYTES +
                                static_cast<size_t>(x >> 3)];
        const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
        if (black) {
            value |= mask;
        } else {
            value &= static_cast<uint8_t>(~mask);
        }
    }

    void hline(int x, int y, int width, bool black = true) const
    {
        if (width <= 0 || y < 0 || y >= EPD_VIDEO_HEIGHT) {
            return;
        }
        const int start = std::max(x, 0);
        const int end = std::min(x + width, static_cast<int>(EPD_VIDEO_WIDTH));
        for (int px = start; px < end; ++px) {
            set_pixel(px, y, black);
        }
    }

    void vline(int x, int y, int height, bool black = true) const
    {
        if (height <= 0 || x < 0 || x >= EPD_VIDEO_WIDTH) {
            return;
        }
        const int start = std::max(y, 0);
        const int end = std::min(y + height, static_cast<int>(EPD_VIDEO_HEIGHT));
        for (int py = start; py < end; ++py) {
            set_pixel(x, py, black);
        }
    }

    void fill_rect(int x, int y, int width, int height, bool black = true) const
    {
        if (width <= 0 || height <= 0) {
            return;
        }
        const int top = std::max(y, 0);
        const int bottom = std::min(y + height, static_cast<int>(EPD_VIDEO_HEIGHT));
        for (int py = top; py < bottom; ++py) {
            hline(x, py, width, black);
        }
    }

    void rect(int x, int y, int width, int height, bool black = true) const
    {
        hline(x, y, width, black);
        hline(x, y + height - 1, width, black);
        vline(x, y, height, black);
        vline(x + width - 1, y, height, black);
    }

    void line(int x0, int y0, int x1, int y1, bool black = true) const
    {
        int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true) {
            set_pixel(x0, y0, black);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int twice_error = error * 2;
            if (twice_error >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice_error <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

    void circle(int cx, int cy, int radius, bool filled, bool black = true) const
    {
        int x = radius;
        int y = 0;
        int error = 1 - radius;
        while (x >= y) {
            if (filled) {
                hline(cx - x, cy + y, x * 2 + 1, black);
                hline(cx - x, cy - y, x * 2 + 1, black);
                hline(cx - y, cy + x, y * 2 + 1, black);
                hline(cx - y, cy - x, y * 2 + 1, black);
            } else {
                set_pixel(cx + x, cy + y, black);
                set_pixel(cx + y, cy + x, black);
                set_pixel(cx - y, cy + x, black);
                set_pixel(cx - x, cy + y, black);
                set_pixel(cx - x, cy - y, black);
                set_pixel(cx - y, cy - x, black);
                set_pixel(cx + y, cy - x, black);
                set_pixel(cx + x, cy - y, black);
            }
            ++y;
            if (error < 0) {
                error += 2 * y + 1;
            } else {
                --x;
                error += 2 * (y - x + 1);
            }
        }
    }
};

const uint8_t *glyph(char character)
{
    static constexpr uint8_t kBlank[5] = {};
    static constexpr uint8_t kDigits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
    };
    static constexpr uint8_t kLetters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    };
    static constexpr uint8_t kDash[5] = {0x08,0x08,0x08,0x08,0x08};
    static constexpr uint8_t kSlash[5] = {0x20,0x10,0x08,0x04,0x02};
    if (character >= '0' && character <= '9') {
        return kDigits[character - '0'];
    }
    if (character >= 'A' && character <= 'Z') {
        return kLetters[character - 'A'];
    }
    if (character == '-') {
        return kDash;
    }
    if (character == '/') {
        return kSlash;
    }
    return kBlank;
}

void draw_text(const Canvas &canvas, int x, int y, const char *text, int scale,
               bool black = true)
{
    while (*text != '\0') {
        const uint8_t *bitmap = glyph(*text++);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((bitmap[column] & (1U << row)) != 0) {
                    canvas.fill_rect(x + column * scale, y + row * scale,
                                     scale, scale, black);
                }
            }
        }
        x += 6 * scale;
    }
}

void draw_number(const Canvas &canvas, int x, int y, uint32_t number,
                 int digits, int scale, bool black = true)
{
    char text[12] = {};
    for (int i = digits - 1; i >= 0; --i) {
        text[i] = static_cast<char>('0' + number % 10U);
        number /= 10U;
    }
    draw_text(canvas, x, y, text, scale, black);
}

int ping_pong_cycle(uint32_t frame, uint32_t period, int limit,
                    uint32_t phase = 0)
{
    const uint32_t half_period = period / 2U;
    const uint32_t position = (frame + phase) % period;
    const uint32_t triangle = position <= half_period
                                  ? position
                                  : period - position;
    return static_cast<int>(triangle * static_cast<uint32_t>(limit) /
                            half_period);
}

void draw_dither_swatch(const Canvas &canvas, int x, int y, int width, int height,
                        int level)
{
    static constexpr uint8_t kBayer4x4[4][4] = {
        {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5},
    };
    canvas.fill_rect(x, y, width, height, false);
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            if (kBayer4x4[py & 3][px & 3] < level) {
                canvas.set_pixel(x + px, y + py, true);
            }
        }
    }
    canvas.rect(x, y, width, height);
}

void draw_quality_page(const Canvas &canvas)
{
    canvas.clear();
    draw_text(canvas, 52, 28, "T5-P4 RAW I80 EPD", 6);
    draw_text(canvas, 56, 88, "ASYNC DMA  1440 X 720  1-BIT", 3);
    for (int level = 0; level < 16; ++level) {
        const int x = 48 + level * 84;
        draw_dither_swatch(canvas, x, 142, 76, 126, level);
        draw_number(canvas, x + 20, 278, level, 2, 2);
    }

    draw_text(canvas, 52, 326, "1-PIXEL LINES", 3);
    for (int x = 52; x < 520; x += 4) {
        canvas.vline(x, 370, 170, (x & 8) == 0);
    }
    for (int y = 370; y < 540; y += 4) {
        canvas.hline(550, y, 410, (y & 8) == 0);
    }
    draw_text(canvas, 1010, 326, "CHECKER", 3);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 12; ++x) {
            canvas.fill_rect(1010 + x * 28, 370 + y * 22, 28, 22,
                             ((x + y) & 1) == 0);
        }
    }
    canvas.rect(1010, 370, 12 * 28, 8 * 22);
    canvas.rect(48, 570, 1344, 100);
    for (int x = 60; x < 1380; x += 40) {
        canvas.line(720, 620, x, (x & 80) ? 578 : 662);
    }
    draw_text(canvas, 400, 686, "SHARPNESS / DITHER / GEOMETRY", 2);
}

void draw_animation_frame(const Canvas &canvas, uint32_t frame, uint32_t fps)
{
    const uint32_t cycle_frame = frame % kAnimationFrames;
    canvas.clear();
    draw_text(canvas, 44, 24, "RAW EPD LOOP", 5);
    draw_text(canvas, 740, 30, "FPS", 4);
    draw_number(canvas, 866, 30, fps, 2, 4);
    draw_text(canvas, 1040, 30, "FRAME", 4);
    draw_number(canvas, 1260, 30, frame % 1000U, 3, 4);

    canvas.rect(38, kAnimationTop, 1364, kAnimationBottom - kAnimationTop);
    for (int x = 70; x < 1380; x += 65) {
        canvas.vline(x, 170, 360);
    }
    for (int y = 170; y <= 530; y += 45) {
        canvas.hline(55, y, 1330);
    }

    const int block_x = 70 + ping_pong_cycle(cycle_frame, 240, 1210);
    const int block_y = 215 + ping_pong_cycle(cycle_frame, 120, 235, 30);
    const int ball_x = 100 + ping_pong_cycle(cycle_frame, 120, 1170, 15);
    const int ball_y = 235 + ping_pong_cycle(cycle_frame, 80, 205, 20);
    const int scan_x = 50 + ping_pong_cycle(cycle_frame, 60, 1335);
    canvas.vline(scan_x, 145, 410);
    canvas.vline(scan_x + 3, 145, 410);
    canvas.circle(ball_x, ball_y, 34, false);
    canvas.circle(ball_x, ball_y, 24, true);
    canvas.fill_rect(block_x, block_y, 104, 76);
    draw_number(canvas, block_x + 24, block_y + 22, frame % 100U, 2, 4, false);

    const int progress = ping_pong_cycle(cycle_frame, kAnimationFrames, 1280);
    canvas.rect(70, 575, 1300, 38);
    canvas.fill_rect(80, 585, progress, 18);
    draw_text(canvas, 425, 630, "DOUBLE BUFFER / DIRTY ROW / DMA", 3);
}

void wait_until_can_submit()
{
    while (!epd_video_can_submit()) {
        vTaskDelay(1);
    }
}

void wait_until_idle()
{
    while (epd_video_submit_pending()) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void submit_quality_page()
{
    wait_until_can_submit();
    Canvas canvas{epd_video_get_backbuffer()};
    const int64_t draw_start = esp_timer_get_time();
    draw_quality_page(canvas);
    const int64_t draw_us = esp_timer_get_time() - draw_start;
    while (!epd_video_submit(0, EPD_VIDEO_HEIGHT)) {
        vTaskDelay(1);
    }
    wait_until_idle();
    ESP_LOGI(kTag, "quality page complete: draw=%lld us vsync=%lu",
             static_cast<long long>(draw_us),
             static_cast<unsigned long>(epd_video_get_vsync_count()));
}

void play_animation_loop()
{
    uint32_t displayed_frames = 0;
    uint32_t measured_fps = 0;
    uint32_t frames_in_window = 0;
    int64_t window_start = esp_timer_get_time();

    while (true) {
        wait_until_can_submit();
        Canvas canvas{epd_video_get_backbuffer()};
        draw_animation_frame(canvas, displayed_frames, measured_fps);
        while (!epd_video_submit(0, kAnimationBottom)) {
            vTaskDelay(1);
        }
        ++displayed_frames;
        ++frames_in_window;
        const int64_t now = esp_timer_get_time();
        if (now - window_start >= 1000000LL) {
            measured_fps = static_cast<uint32_t>(
                frames_in_window * 1000000ULL /
                static_cast<uint64_t>(now - window_start));
            ESP_LOGI(kTag, "producer=%lu fps submitted=%lu vsync=%lu",
                     static_cast<unsigned long>(measured_fps),
                     static_cast<unsigned long>(epd_video_get_submit_count()),
                     static_cast<unsigned long>(epd_video_get_vsync_count()));
            frames_in_window = 0;
            window_start = now;
        }
    }
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "LILYGO T5-P4 E-Paper V0.3 raw refresh demo");
    ESP_LOGI(kTag, "ESP-IDF 6.0.1, ESP32-P4 v3.2, no M5GFX");
    ESP_LOGI(kTag, "heap before init: internal=%u PSRAM=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    if (!epd_video_init() || !epd_video_power_on() || !epd_video_start()) {
        ESP_LOGE(kTag, "raw EPD driver initialization failed");
        epd_video_shutdown();
        return;
    }

    submit_quality_page();
    vTaskDelay(pdMS_TO_TICKS(kQualityHoldMs));
    play_animation_loop();
}
