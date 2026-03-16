#pragma once

#include <stdint.h>
#include <stddef.h>

class JpegHwDecoder {
public:
    bool begin();
    void end();
    bool decodeRgb888(const uint8_t *jpeg, size_t jpegSize, uint8_t *outRgb, size_t outSize);

private:
    void *decoder_{};
};

