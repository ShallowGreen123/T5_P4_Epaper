#pragma once

#include <stddef.h>
#include <stdint.h>

struct AviMjpegInfo {
    uint32_t width;
    uint32_t height;
    uint32_t frameIntervalUs;
};

class AviMjpegReader {
public:
    bool open(const char *path);
    void close();
    bool info(AviMjpegInfo &out) const;
    bool rewindToFirstFrame();
    bool readNextJpeg(uint8_t *dst, size_t dstCap, size_t &outSize);

private:
    bool parseHeaders();
    bool seekToMovi();

private:
    void *file_{};
    uint64_t moviStart_{};
    uint64_t moviEnd_{};
    AviMjpegInfo info_{};
};

