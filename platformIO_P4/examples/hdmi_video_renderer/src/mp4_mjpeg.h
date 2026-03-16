#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct Mp4MjpegInfo {
    uint32_t width;
    uint32_t height;
    uint32_t timeScale;
};

class Mp4MjpegReader {
public:
    bool open(const char *path);
    void close();
    bool info(Mp4MjpegInfo &out) const;
    bool rewindToFirstFrame();
    bool readNextSample(uint8_t *dst, size_t dstCap, size_t &outSize, uint32_t &outDurationTs);

private:
    bool parse();
    bool parseBox(uint64_t end);
    bool parseMoov(uint64_t end);
    bool parseTrak(uint64_t end);
    bool parseMdia(uint64_t end);
    bool parseMinf(uint64_t end);
    bool parseStbl(uint64_t end);
    bool parseStsd(uint64_t end);
    bool parseStsz(uint64_t end);
    bool parseStco(uint64_t end, bool is64);
    bool parseStsc(uint64_t end);
    bool parseStts(uint64_t end);
    bool parseTkhd(uint64_t end);
    bool parseMdhd(uint64_t end);
    bool buildSampleTable();

private:
    struct StscEntry {
        uint32_t firstChunk;
        uint32_t samplesPerChunk;
        uint32_t sampleDescIdx;
    };
    struct SttsEntry {
        uint32_t count;
        uint32_t delta;
    };
    struct Sample {
        uint64_t offset;
        uint32_t size;
        uint32_t durationTs;
    };

private:
    void *file_{};
    Mp4MjpegInfo info_{};
    bool selected_{};
    bool inVideoTrak_{};
    bool videoCodecOk_{};

    std::vector<uint32_t> stsz_;
    uint32_t stszFixed_{};
    std::vector<uint64_t> stco_;
    std::vector<StscEntry> stsc_;
    std::vector<SttsEntry> stts_;

    std::vector<Sample> samples_;
    size_t nextSample_{};
};

