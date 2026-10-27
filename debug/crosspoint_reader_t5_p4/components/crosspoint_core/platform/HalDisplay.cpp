#include "HalDisplay.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>

namespace {
constexpr char kTag[] = "cp_display";
}

HalDisplay::~HalDisplay()
{
    if (frame_buffer_) {
        heap_caps_free(frame_buffer_);
        frame_buffer_ = nullptr;
    }
}

void HalDisplay::attach(FASTEPD *display)
{
    display_ = display;
    if (display_) {
        width_ = display_->width();
        height_ = display_->height();
        width_bytes_ = (width_ + 7) / 8;
        buffer_size_ = width_bytes_ * height_;
    }
}

void HalDisplay::begin()
{
    if (!frame_buffer_) {
        frame_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!frame_buffer_) {
            frame_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(buffer_size_, MALLOC_CAP_8BIT));
        }
    }
    if (!frame_buffer_) {
        ESP_LOGE(kTag, "failed to allocate %lu-byte 1bpp framebuffer", static_cast<unsigned long>(buffer_size_));
        return;
    }
    memset(frame_buffer_, 0xFF, buffer_size_);
}

void HalDisplay::clearScreen(uint8_t color) const
{
    if (!frame_buffer_) {
        return;
    }
    memset(frame_buffer_, color, buffer_size_);
}

void HalDisplay::drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem) const
{
    (void)fromProgmem;
    if (!frame_buffer_ || !imageData) {
        return;
    }
    for (uint16_t row = 0; row < h; ++row) {
        if (y + row >= height_) {
            break;
        }
        for (uint16_t col = 0; col < w; ++col) {
            if (x + col >= width_) {
                break;
            }
            const size_t src_byte = row * ((w + 7) / 8) + (col / 8);
            const bool black = ((imageData[src_byte] >> (7 - (col & 7))) & 1) != 0;
            const size_t dst_byte = (y + row) * width_bytes_ + ((x + col) / 8);
            const uint8_t mask = 1 << (7 - ((x + col) & 7));
            if (black) {
                frame_buffer_[dst_byte] &= ~mask;
            } else {
                frame_buffer_[dst_byte] |= mask;
            }
        }
    }
}

void HalDisplay::drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem) const
{
    drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) const
{
    (void)turnOffScreen;
    if (!display_ || !frame_buffer_) {
        return;
    }
    blit_to_fastepd();
    display_->fullUpdate(mode == FULL_REFRESH ? CLEAR_SLOW : CLEAR_NONE, true);
}

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) const
{
    displayBuffer(mode, turnOffScreen);
}

void HalDisplay::blit_to_fastepd() const
{
    display_->setMode(BB_MODE_4BPP);
    for (uint16_t y = 0; y < height_; ++y) {
        const uint8_t *row = frame_buffer_ + y * width_bytes_;
        for (uint16_t x = 0; x < width_; ++x) {
            const bool white = ((row[x >> 3] >> (7 - (x & 7))) & 1) != 0;
            display_->drawPixelFast(x, y, white ? 15 : 0);
        }
    }
}
