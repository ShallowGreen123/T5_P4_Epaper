#include "avi_mjpeg.h"

#include <stdio.h>
#include <string.h>

static uint32_t readLe32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool freadExact(FILE *f, void *buf, size_t n)
{
    return fread(buf, 1, n, f) == n;
}

static bool fskip(FILE *f, uint64_t n)
{
    return fseek(f, (long)n, SEEK_CUR) == 0;
}

static uint64_t ftellU64(FILE *f)
{
    long pos = ftell(f);
    return pos < 0 ? 0 : (uint64_t)pos;
}

bool AviMjpegReader::open(const char *path)
{
    close();
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    file_ = f;
    if (!parseHeaders()) {
        close();
        return false;
    }
    return rewindToFirstFrame();
}

void AviMjpegReader::close()
{
    if (file_) {
        fclose(static_cast<FILE *>(file_));
        file_ = nullptr;
    }
    moviStart_ = 0;
    moviEnd_ = 0;
    info_ = {};
}

bool AviMjpegReader::info(AviMjpegInfo &out) const
{
    if (!file_) {
        return false;
    }
    out = info_;
    return true;
}

bool AviMjpegReader::rewindToFirstFrame()
{
    if (!file_) {
        return false;
    }
    return seekToMovi();
}

bool AviMjpegReader::seekToMovi()
{
    auto f = static_cast<FILE *>(file_);
    return fseek(f, (long)moviStart_, SEEK_SET) == 0;
}

bool AviMjpegReader::parseHeaders()
{
    auto f = static_cast<FILE *>(file_);

    uint8_t riff[12];
    if (!freadExact(f, riff, sizeof(riff))) {
        return false;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "AVI ", 4) != 0) {
        return false;
    }

    while (true) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        const uint32_t chunkSize = readLe32(hdr + 4);
        if (memcmp(hdr, "LIST", 4) == 0) {
            uint8_t listType[4];
            if (!freadExact(f, listType, sizeof(listType))) {
                return false;
            }
            const uint64_t listPayloadStart = ftellU64(f);
            if (memcmp(listType, "movi", 4) == 0) {
                moviStart_ = listPayloadStart;
                moviEnd_ = listPayloadStart + (uint64_t)chunkSize - 4;
                return moviStart_ < moviEnd_;
            }
            const uint64_t toSkip = (uint64_t)chunkSize - 4;
            if (!fskip(f, toSkip + (toSkip & 1))) {
                return false;
            }
        }
        else if (memcmp(hdr, "avih", 4) == 0) {
            uint8_t avih[56];
            const size_t need = sizeof(avih) <= chunkSize ? sizeof(avih) : chunkSize;
            if (!freadExact(f, avih, need)) {
                return false;
            }
            if (need >= 40) {
                info_.frameIntervalUs = readLe32(avih + 0);
                info_.width = readLe32(avih + 32);
                info_.height = readLe32(avih + 36);
            }
            const uint64_t rem = chunkSize - need;
            if (!fskip(f, rem + (chunkSize & 1))) {
                return false;
            }
        }
        else {
            if (!fskip(f, (uint64_t)chunkSize + (chunkSize & 1))) {
                return false;
            }
        }
    }
}

bool AviMjpegReader::readNextJpeg(uint8_t *dst, size_t dstCap, size_t &outSize)
{
    outSize = 0;
    if (!file_) {
        return false;
    }
    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 < moviEnd_) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        const uint32_t chunkSize = readLe32(hdr + 4);
        const bool isVideo = (hdr[2] == 'd') && (hdr[3] == 'c' || hdr[3] == 'b');
        if (isVideo) {
            if (chunkSize > dstCap) {
                return false;
            }
            if (!freadExact(f, dst, chunkSize)) {
                return false;
            }
            if (chunkSize & 1) {
                fskip(f, 1);
            }
            outSize = chunkSize;
            return true;
        }
        if (!fskip(f, (uint64_t)chunkSize + (chunkSize & 1))) {
            return false;
        }
    }
    return false;
}

