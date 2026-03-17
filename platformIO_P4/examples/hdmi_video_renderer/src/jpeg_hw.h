#pragma once

#include <stdint.h>
#include <stddef.h>

class JpegHwDecoder {
public:
    bool begin();
    void end();
    bool decodeRgb888(const uint8_t *jpeg, size_t jpegSize, void *outPixels, size_t outSize, uint32_t *written = nullptr);

private:
    void *decoder_{};
};

