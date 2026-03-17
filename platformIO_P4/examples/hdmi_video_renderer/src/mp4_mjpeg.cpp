#include "mp4_mjpeg.h"

#include <stdio.h>
#include <string.h>

static uint32_t readBe32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t readBe64(const uint8_t *p)
{
    return ((uint64_t)readBe32(p) << 32) | (uint64_t)readBe32(p + 4);
}

static uint32_t tag4(const char s[4])
{
    return ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | (uint32_t)s[3];
}

static bool freadExact(FILE *f, void *buf, size_t n)
{
    return fread(buf, 1, n, f) == n;
}

static uint64_t ftellU64(FILE *f)
{
    long pos = ftell(f);
    return pos < 0 ? 0 : (uint64_t)pos;
}

static bool fseekAbs(FILE *f, uint64_t pos)
{
    return fseek(f, (long)pos, SEEK_SET) == 0;
}

static bool fskip(FILE *f, uint64_t n)
{
    return fseek(f, (long)n, SEEK_CUR) == 0;
}

bool Mp4MjpegReader::open(const char *path)
{
    close();
    diag_ = {};
    FILE *f = fopen(path, "rb");
    if (!f) {
        diag_.status = Mp4ProbeStatus::OpenFileFailed;
        return false;
    }
    file_ = f;
    if (!parse()) {
        close();
        return false;
    }
    diag_.status = Mp4ProbeStatus::Ready;
    return rewindToFirstFrame();
}

void Mp4MjpegReader::close()
{
    if (file_) {
        fclose(static_cast<FILE *>(file_));
        file_ = nullptr;
    }
    info_ = {};
    selected_ = false;
    inVideoTrak_ = false;
    videoCodecOk_ = false;
    stsz_.clear();
    stszFixed_ = 0;
    stco_.clear();
    stsc_.clear();
    stts_.clear();
    samples_.clear();
    nextSample_ = 0;
}

bool Mp4MjpegReader::info(Mp4MjpegInfo &out) const
{
    if (!file_ || !selected_) {
        return false;
    }
    out = info_;
    return true;
}

void Mp4MjpegReader::diagnostics(Mp4MjpegDiagnostics &out) const
{
    out = diag_;
}

bool Mp4MjpegReader::rewindToFirstFrame()
{
    if (!file_ || !selected_) {
        return false;
    }
    nextSample_ = 0;
    return true;
}

bool Mp4MjpegReader::readNextSample(uint8_t *dst, size_t dstCap, size_t &outSize, uint32_t &outDurationTs)
{
    outSize = 0;
    outDurationTs = 0;
    if (!file_ || !selected_ || nextSample_ >= samples_.size()) {
        return false;
    }
    const Sample &s = samples_[nextSample_++];
    if (s.size > dstCap) {
        return false;
    }
    auto f = static_cast<FILE *>(file_);
    if (!fseekAbs(f, s.offset)) {
        return false;
    }
    if (!freadExact(f, dst, s.size)) {
        return false;
    }
    outSize = s.size;
    outDurationTs = s.durationTs;
    return true;
}

bool Mp4MjpegReader::parse()
{
    auto f = static_cast<FILE *>(file_);
    if (!fseekAbs(f, 0)) {
        diag_.status = Mp4ProbeStatus::ParseFailed;
        return false;
    }
    if (!parseBox(UINT64_MAX)) {
        diag_.status = Mp4ProbeStatus::ParseFailed;
        return false;
    }
    if (!selected_) {
        diag_.status = Mp4ProbeStatus::NoVideoTrack;
        return false;
    }
    if (!buildSampleTable()) {
        return false;
    }
    if (samples_.empty()) {
        diag_.status = Mp4ProbeStatus::EmptySampleTable;
        return false;
    }
    diag_.status = Mp4ProbeStatus::Ready;
    return true;
}

bool Mp4MjpegReader::parseBox(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 <= end) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        uint64_t size = readBe32(hdr);
        const uint32_t type = readBe32(hdr + 4);
        uint64_t headerSize = 8;
        if (size == 1) {
            uint8_t ext[8];
            if (!freadExact(f, ext, sizeof(ext))) {
                return false;
            }
            size = readBe64(ext);
            headerSize = 16;
        }
        if (size < headerSize) {
            return false;
        }
        const uint64_t boxStart = ftellU64(f) - headerSize;
        uint64_t boxEnd = boxStart + size;
        if (end != UINT64_MAX && boxEnd > end) {
            return false;
        }
        const uint64_t payloadEnd = boxEnd;

        if (type == tag4("moov")) {
            if (!parseMoov(payloadEnd)) {
                return false;
            }
        } else {
            if (!fseekAbs(f, payloadEnd)) {
                return false;
            }
        }
        if (selected_) {
            return true;
        }
    }
    return true;
}

bool Mp4MjpegReader::parseMoov(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 <= end) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        uint64_t size = readBe32(hdr);
        const uint32_t type = readBe32(hdr + 4);
        uint64_t headerSize = 8;
        if (size == 1) {
            uint8_t ext[8];
            if (!freadExact(f, ext, sizeof(ext))) {
                return false;
            }
            size = readBe64(ext);
            headerSize = 16;
        }
        const uint64_t boxStart = ftellU64(f) - headerSize;
        const uint64_t boxEnd = boxStart + size;
        if (type == tag4("trak")) {
            if (!parseTrak(boxEnd)) {
                return false;
            }
            if (selected_) {
                return true;
            }
        } else {
            if (!fseekAbs(f, boxEnd)) {
                return false;
            }
        }
    }
    return true;
}

bool Mp4MjpegReader::parseTrak(uint64_t end)
{
    if (selected_) {
        auto f = static_cast<FILE *>(file_);
        return fseekAbs(f, end);
    }

    Mp4MjpegInfo oldInfo = info_;
    Mp4MjpegDiagnostics oldDiag = diag_;
    auto oldStsz = stsz_;
    uint32_t oldStszFixed = stszFixed_;
    auto oldStco = stco_;
    auto oldStsc = stsc_;
    auto oldStts = stts_;

    info_ = {};
    diag_ = {};
    stsz_.clear();
    stszFixed_ = 0;
    stco_.clear();
    stsc_.clear();
    stts_.clear();
    inVideoTrak_ = false;
    videoCodecOk_ = false;

    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 <= end) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        uint64_t size = readBe32(hdr);
        const uint32_t type = readBe32(hdr + 4);
        uint64_t headerSize = 8;
        if (size == 1) {
            uint8_t ext[8];
            if (!freadExact(f, ext, sizeof(ext))) {
                return false;
            }
            size = readBe64(ext);
            headerSize = 16;
        }
        const uint64_t boxStart = ftellU64(f) - headerSize;
        const uint64_t boxEnd = boxStart + size;

        if (type == tag4("tkhd")) {
            if (!parseTkhd(boxEnd)) {
                return false;
            }
        } else if (type == tag4("mdia")) {
            if (!parseMdia(boxEnd)) {
                return false;
            }
        } else {
            if (!fseekAbs(f, boxEnd)) {
                return false;
            }
        }
    }

    if (inVideoTrak_) {
        selected_ = true;
        return true;
    }

    info_ = oldInfo;
    diag_ = oldDiag;
    stsz_ = std::move(oldStsz);
    stszFixed_ = oldStszFixed;
    stco_ = std::move(oldStco);
    stsc_ = std::move(oldStsc);
    stts_ = std::move(oldStts);
    inVideoTrak_ = false;
    videoCodecOk_ = false;
    return true;
}

bool Mp4MjpegReader::parseMdia(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 <= end) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        uint64_t size = readBe32(hdr);
        const uint32_t type = readBe32(hdr + 4);
        uint64_t headerSize = 8;
        if (size == 1) {
            uint8_t ext[8];
            if (!freadExact(f, ext, sizeof(ext))) {
                return false;
            }
            size = readBe64(ext);
            headerSize = 16;
        }
        const uint64_t boxStart = ftellU64(f) - headerSize;
        const uint64_t boxEnd = boxStart + size;

        if (type == tag4("mdhd")) {
            if (!parseMdhd(boxEnd)) {
                return false;
            }
        } else if (type == tag4("hdlr")) {
            uint8_t full[20];
            const uint64_t need = sizeof(full) <= (boxEnd - ftellU64(f)) ? sizeof(full) : (boxEnd - ftellU64(f));
            if (need < 12) {
                return false;
            }
            if (!freadExact(f, full, (size_t)need)) {
                return false;
            }
            const uint32_t handler = readBe32(full + 8);
            if (handler == tag4("vide")) {
                inVideoTrak_ = true;
            }
            if (!fseekAbs(f, boxEnd)) {
                return false;
            }
        } else if (type == tag4("minf")) {
            if (!parseMinf(boxEnd)) {
                return false;
            }
        } else {
            if (!fseekAbs(f, boxEnd)) {
                return false;
            }
        }
    }
    return true;
}

bool Mp4MjpegReader::parseMinf(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 <= end) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        uint64_t size = readBe32(hdr);
        const uint32_t type = readBe32(hdr + 4);
        uint64_t headerSize = 8;
        if (size == 1) {
            uint8_t ext[8];
            if (!freadExact(f, ext, sizeof(ext))) {
                return false;
            }
            size = readBe64(ext);
            headerSize = 16;
        }
        const uint64_t boxStart = ftellU64(f) - headerSize;
        const uint64_t boxEnd = boxStart + size;

        if (type == tag4("stbl")) {
            if (!parseStbl(boxEnd)) {
                return false;
            }
        } else {
            if (!fseekAbs(f, boxEnd)) {
                return false;
            }
        }
    }
    return true;
}

bool Mp4MjpegReader::parseStbl(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    while (ftellU64(f) + 8 <= end) {
        uint8_t hdr[8];
        if (!freadExact(f, hdr, sizeof(hdr))) {
            return false;
        }
        uint64_t size = readBe32(hdr);
        const uint32_t type = readBe32(hdr + 4);
        uint64_t headerSize = 8;
        if (size == 1) {
            uint8_t ext[8];
            if (!freadExact(f, ext, sizeof(ext))) {
                return false;
            }
            size = readBe64(ext);
            headerSize = 16;
        }
        const uint64_t boxStart = ftellU64(f) - headerSize;
        const uint64_t boxEnd = boxStart + size;

        if (type == tag4("stsd")) {
            if (!parseStsd(boxEnd)) {
                return false;
            }
        } else if (type == tag4("stsz")) {
            if (!parseStsz(boxEnd)) {
                return false;
            }
        } else if (type == tag4("stco")) {
            if (!parseStco(boxEnd, false)) {
                return false;
            }
        } else if (type == tag4("co64")) {
            if (!parseStco(boxEnd, true)) {
                return false;
            }
        } else if (type == tag4("stsc")) {
            if (!parseStsc(boxEnd)) {
                return false;
            }
        } else if (type == tag4("stts")) {
            if (!parseStts(boxEnd)) {
                return false;
            }
        } else {
            if (!fseekAbs(f, boxEnd)) {
                return false;
            }
        }
    }
    return true;
}

bool Mp4MjpegReader::parseStsd(uint64_t end)
{
    if (!inVideoTrak_) {
        auto f = static_cast<FILE *>(file_);
        return fseekAbs(f, end);
    }
    auto f = static_cast<FILE *>(file_);
    uint8_t hdr[16];
    if (!freadExact(f, hdr, sizeof(hdr))) {
        return false;
    }
    const uint32_t entryCount = readBe32(hdr + 4);
    if (entryCount == 0) {
        diag_.status = Mp4ProbeStatus::ParseFailed;
        return false;
    }
    const uint32_t sampleEntrySize = readBe32(hdr + 8);
    const uint32_t format = readBe32(hdr + 12);
    diag_.codecTag = format;
    videoCodecOk_ = (format == tag4("mjpg")) || (format == tag4("jpeg"));
    (void)sampleEntrySize;
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::parseStsz(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    uint8_t hdr[12];
    if (!freadExact(f, hdr, sizeof(hdr))) {
        return false;
    }
    stszFixed_ = readBe32(hdr + 4);
    const uint32_t count = readBe32(hdr + 8);
    if (stszFixed_ == 0) {
        stsz_.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            uint8_t b[4];
            if (!freadExact(f, b, sizeof(b))) {
                return false;
            }
            stsz_[i] = readBe32(b);
        }
    } else {
        stsz_.clear();
        stsz_.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            stsz_.push_back(stszFixed_);
        }
    }
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::parseStco(uint64_t end, bool is64)
{
    auto f = static_cast<FILE *>(file_);
    uint8_t hdr[8];
    if (!freadExact(f, hdr, sizeof(hdr))) {
        return false;
    }
    const uint32_t count = readBe32(hdr + 4);
    stco_.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        if (is64) {
            uint8_t b[8];
            if (!freadExact(f, b, sizeof(b))) {
                return false;
            }
            stco_[i] = readBe64(b);
        } else {
            uint8_t b[4];
            if (!freadExact(f, b, sizeof(b))) {
                return false;
            }
            stco_[i] = readBe32(b);
        }
    }
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::parseStsc(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    uint8_t hdr[8];
    if (!freadExact(f, hdr, sizeof(hdr))) {
        return false;
    }
    const uint32_t count = readBe32(hdr + 4);
    stsc_.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t b[12];
        if (!freadExact(f, b, sizeof(b))) {
            return false;
        }
        stsc_[i] = StscEntry{readBe32(b), readBe32(b + 4), readBe32(b + 8)};
    }
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::parseStts(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    uint8_t hdr[8];
    if (!freadExact(f, hdr, sizeof(hdr))) {
        return false;
    }
    const uint32_t count = readBe32(hdr + 4);
    stts_.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t b[8];
        if (!freadExact(f, b, sizeof(b))) {
            return false;
        }
        stts_[i] = SttsEntry{readBe32(b), readBe32(b + 4)};
    }
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::parseTkhd(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    if (end < 8) {
        diag_.status = Mp4ProbeStatus::ParseFailed;
        return false;
    }
    if (!fseekAbs(f, end - 8)) {
        return false;
    }
    uint8_t wh[8];
    if (!freadExact(f, wh, sizeof(wh))) {
        return false;
    }
    info_.width = readBe32(wh) >> 16;
    info_.height = readBe32(wh + 4) >> 16;
    diag_.width = info_.width;
    diag_.height = info_.height;
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::parseMdhd(uint64_t end)
{
    auto f = static_cast<FILE *>(file_);
    uint8_t vflags[4];
    if (!freadExact(f, vflags, sizeof(vflags))) {
        return false;
    }
    const uint8_t version = vflags[0];
    if (version == 1) {
        if (!fskip(f, 16)) {
            return false;
        }
        uint8_t ts[4];
        if (!freadExact(f, ts, sizeof(ts))) {
            return false;
        }
        info_.timeScale = readBe32(ts);
    } else {
        if (!fskip(f, 8)) {
            return false;
        }
        uint8_t ts[4];
        if (!freadExact(f, ts, sizeof(ts))) {
            return false;
        }
        info_.timeScale = readBe32(ts);
    }
    diag_.timeScale = info_.timeScale;
    return fseekAbs(f, end);
}

bool Mp4MjpegReader::buildSampleTable()
{
    if (!selected_) {
        diag_.status = Mp4ProbeStatus::NoVideoTrack;
        return false;
    }
    if (info_.timeScale == 0 || info_.width == 0 || info_.height == 0) {
        diag_.width = info_.width;
        diag_.height = info_.height;
        diag_.timeScale = info_.timeScale;
        diag_.status = Mp4ProbeStatus::MissingMetadata;
        return false;
    }
    if (stco_.empty() || stsc_.empty() || stsz_.empty() || stts_.empty()) {
        diag_.status = Mp4ProbeStatus::IncompleteSampleTable;
        return false;
    }

    samples_.clear();
    samples_.reserve(stsz_.size());

    size_t stscIdx = 0;
    size_t sampleIdx = 0;
    size_t sttsIdx = 0;
    uint32_t sttsLeft = stts_[0].count;

    for (uint32_t chunkIndex = 1; chunkIndex <= stco_.size(); chunkIndex++) {
        while (stscIdx + 1 < stsc_.size() && stsc_[stscIdx + 1].firstChunk <= chunkIndex) {
            stscIdx++;
        }
        const uint32_t samplesPerChunk = stsc_[stscIdx].samplesPerChunk;
        uint64_t offset = stco_[chunkIndex - 1];
        for (uint32_t i = 0; i < samplesPerChunk; i++) {
            if (sampleIdx >= stsz_.size()) {
                break;
            }
            if (sttsIdx >= stts_.size()) {
                diag_.status = Mp4ProbeStatus::IncompleteSampleTable;
                return false;
            }
            const uint32_t size = stsz_[sampleIdx];
            const uint32_t duration = stts_[sttsIdx].delta;
            samples_.push_back(Sample{offset, size, duration});
            offset += size;
            sampleIdx++;

            if (--sttsLeft == 0) {
                sttsIdx++;
                if (sttsIdx < stts_.size()) {
                    sttsLeft = stts_[sttsIdx].count;
                }
            }
        }
        if (sampleIdx >= stsz_.size()) {
            break;
        }
    }
    diag_.sampleCount = samples_.size();
    if (samples_.size() != stsz_.size()) {
        diag_.status = Mp4ProbeStatus::IncompleteSampleTable;
        return false;
    }
    if (samples_.empty()) {
        diag_.status = Mp4ProbeStatus::EmptySampleTable;
        return false;
    }
    return true;
}
