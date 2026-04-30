#pragma once

#include <stdint.h>
#include <stddef.h>

#include <FastEPD.h>

class HalDisplay {
public:
    enum RefreshMode {
        FULL_REFRESH,
        HALF_REFRESH,
        FAST_REFRESH,
    };

    static constexpr uint16_t DISPLAY_WIDTH = 1440;
    static constexpr uint16_t DISPLAY_HEIGHT = 720;
    static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
    static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

    HalDisplay() = default;
    explicit HalDisplay(FASTEPD *display) : display_(display) {}
    ~HalDisplay();

    void attach(FASTEPD *display);
    void begin();
    void clearScreen(uint8_t color = 0xFF) const;
    void drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false) const;
    void drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false) const;
    void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false) const;
    void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false) const;
    void deepSleep() {}
    uint8_t *getFrameBuffer() const { return frame_buffer_; }
    void copyGrayscaleBuffers(const uint8_t *, const uint8_t *) {}
    void copyGrayscaleLsbBuffers(const uint8_t *) {}
    void copyGrayscaleMsbBuffers(const uint8_t *) {}
    void cleanupGrayscaleBuffers(const uint8_t *) {}
    void displayGrayBuffer(bool turnOffScreen = false) { (void)turnOffScreen; }
    uint16_t getDisplayWidth() const { return width_; }
    uint16_t getDisplayHeight() const { return height_; }
    uint16_t getDisplayWidthBytes() const { return width_bytes_; }
    uint32_t getBufferSize() const { return buffer_size_; }

private:
    FASTEPD *display_ = nullptr;
    uint8_t *frame_buffer_ = nullptr;
    uint16_t width_ = DISPLAY_WIDTH;
    uint16_t height_ = DISPLAY_HEIGHT;
    uint16_t width_bytes_ = DISPLAY_WIDTH_BYTES;
    uint32_t buffer_size_ = BUFFER_SIZE;

    void blit_to_fastepd() const;
};
