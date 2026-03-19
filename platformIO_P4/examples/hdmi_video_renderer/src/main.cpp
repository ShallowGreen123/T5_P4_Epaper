#include <Arduino.h>

#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <string>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "avi_mjpeg.h"
#include "hdmi_output.h"
#include "jpeg_hw.h"
#include "mp4_mjpeg.h"
#include "sdcard.h"
#include "video_file_finder.h"

static constexpr uint32_t kWidth = HDMI_FRAME_WIDTH;
static constexpr uint32_t kHeight = HDMI_FRAME_HEIGHT;
static constexpr size_t kFrameBufferSize = (size_t)kWidth * (size_t)kHeight * HDMI_BYTES_PER_PIXEL;
static constexpr uint32_t kDefaultFrameIntervalUs = 1000000UL / 15UL;
static constexpr const char *kMountPoint = "/sdcard";
static constexpr const char *kPixelFormatName = "RGB888";

static HdmiOutput g_hdmi;
static SdCard g_sd;

enum class PlayResult {
    Completed,
    SdRemoved,
    BadVideo,
    InternalError,
};

static uint8_t *allocInputBuffer(size_t cap)
{
    return static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void freeInputBuffer(uint8_t *p)
{
    if (p) {
        heap_caps_free(p);
    }
}

static const char *mp4ProbeStatusName(Mp4ProbeStatus status)
{
    switch (status) {
        case Mp4ProbeStatus::Idle:
            return "idle";
        case Mp4ProbeStatus::OpenFileFailed:
            return "open_file_failed";
        case Mp4ProbeStatus::ParseFailed:
            return "parse_failed";
        case Mp4ProbeStatus::NoVideoTrack:
            return "no_video_track";
        case Mp4ProbeStatus::UnsupportedCodec:
            return "unsupported_codec";
        case Mp4ProbeStatus::MissingMetadata:
            return "missing_metadata";
        case Mp4ProbeStatus::IncompleteSampleTable:
            return "incomplete_sample_table";
        case Mp4ProbeStatus::EmptySampleTable:
            return "empty_sample_table";
        case Mp4ProbeStatus::Ready:
            return "ready";
    }
    return "unknown";
}

static void fourCcToString(uint32_t tag, char out[5])
{
    for (int i = 0; i < 4; ++i) {
        const uint8_t c = (uint8_t)(tag >> (24 - i * 8));
        out[i] = isprint(c) ? (char)c : '.';
    }
    out[4] = '\0';
}

static void logMp4Diagnostics(const Mp4MjpegReader &r)
{
    Mp4MjpegDiagnostics diag{};
    r.diagnostics(diag);
    char codec[5];
    fourCcToString(diag.codecTag, codec);
    Serial.printf("[player] MP4 probe: status=%s codec=%s(0x%08lX) size=%lux%lu timeScale=%lu samples=%u\n",
                  mp4ProbeStatusName(diag.status), codec, (unsigned long)diag.codecTag, (unsigned long)diag.width,
                  (unsigned long)diag.height, (unsigned long)diag.timeScale, (unsigned)diag.sampleCount);
    if (diag.status == Mp4ProbeStatus::UnsupportedCodec) {
        Serial.println("[player] MP4 codec unsupported: only Motion JPEG in MP4 (mjpg/jpeg) is supported");
    }
    if (diag.status == Mp4ProbeStatus::Ready) {
        Serial.println("[player] MP4 metadata source: atoms tkhd/stsd/mdhd parsed from /sdcard file");
    }
}

static bool looksLikeJpeg(const uint8_t *data, size_t size)
{
    return data && size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

static void logSamplePrefix(const uint8_t *data, size_t size)
{
    const uint8_t b0 = size > 0 ? data[0] : 0;
    const uint8_t b1 = size > 1 ? data[1] : 0;
    const uint8_t b2 = size > 2 ? data[2] : 0;
    const uint8_t b3 = size > 3 ? data[3] : 0;
    Serial.printf("[player] MP4 sample prefix: %02X %02X %02X %02X (size=%u)\n", b0, b1, b2, b3, (unsigned)size);
}

static void logStreamConfig(const char *container, uint32_t streamWidth, uint32_t streamHeight)
{
    Serial.printf("[player] %s stream=%lux%lu target=%lux%lu format=%s fb=%u bytes\n", container, (unsigned long)streamWidth,
                  (unsigned long)streamHeight, (unsigned long)kWidth, (unsigned long)kHeight, kPixelFormatName,
                  (unsigned)kFrameBufferSize);
    if (streamWidth != kWidth || streamHeight != kHeight) {
        Serial.printf("[player] WARNING: %s stream size does not match target; this demo does not scale frames\n", container);
    }
}

static void logFramePerf(const char *container, size_t frameIdx, size_t inputSize, uint32_t decodedSize, uint64_t readUs,
                         uint64_t decodeUs, uint64_t presentUs, uint64_t totalUs, uint32_t targetIntervalUs, int64_t sleepUs)
{
    const double slackMs = sleepUs > 0 ? (double)sleepUs / 1000.0 : 0.0;
    const double lateMs = sleepUs < 0 ? (double)(-sleepUs) / 1000.0 : 0.0;
    Serial.printf("[player] %s frame %u in=%u out=%u read=%.1f decode=%.1f present=%.1f total=%.1f target=%.1f %s=%.1f ms\n",
                  container, (unsigned)frameIdx, (unsigned)inputSize, (unsigned)decodedSize, readUs / 1000.0, decodeUs / 1000.0,
                  presentUs / 1000.0, totalUs / 1000.0, targetIntervalUs / 1000.0, sleepUs >= 0 ? "sleep" : "late",
                  sleepUs >= 0 ? slackMs : lateMs);
}

static void logMp4Progress(size_t frameIdx, size_t totalFrames, uint64_t playedUs)
{
    const double progressPct = totalFrames ? (100.0 * (double)(frameIdx + 1) / (double)totalFrames) : 0.0;
    const uint32_t playedSec = (uint32_t)(playedUs / 1000000ULL);
    const uint32_t playedMs = (uint32_t)((playedUs / 1000ULL) % 1000ULL);
    if (totalFrames) {
        Serial.printf("[player] MP4 progress: frame=%u/%u %.1f%% time=%u.%03us\n", (unsigned)(frameIdx + 1),
                      (unsigned)totalFrames, progressPct, (unsigned)playedSec, (unsigned)playedMs);
    } else {
        Serial.printf("[player] MP4 progress: frame=%u time=%u.%03us\n", (unsigned)(frameIdx + 1), (unsigned)playedSec,
                      (unsigned)playedMs);
    }
}

static void logFileStat(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        Serial.printf("[player] file stat: size=%llu bytes mtime=%lld\n", (unsigned long long)st.st_size, (long long)st.st_mtime);
    } else {
        Serial.printf("[player] file stat failed: errno=%d\n", errno);
    }
}

static PlayResult playAvi(const std::string &path, const HdmiFramebuffers &fbs)
{
    AviMjpegReader r;
    if (!r.open(path.c_str())) {
        return PlayResult::SdRemoved;
    }
    AviMjpegInfo info{};
    r.info(info);
    logStreamConfig("AVI", info.width, info.height);

    JpegHwDecoder dec;
    if (!dec.begin()) {
        r.close();
        return PlayResult::InternalError;
    }

    size_t inputCap = 512 * 1024;
    uint8_t *input = allocInputBuffer(inputCap);
    if (!input) {
        dec.end();
        r.close();
        return PlayResult::InternalError;
    }

    uint64_t nextUs = esp_timer_get_time();
    size_t frameIdx = 0;

    while (true) {
        const uint64_t frameStartUs = esp_timer_get_time();
        size_t jpegSize = 0;
        const uint64_t readStartUs = esp_timer_get_time();
        if (!r.readNextJpeg(input, inputCap, jpegSize)) {
            if (!r.rewindToFirstFrame()) {
                freeInputBuffer(input);
                dec.end();
                r.close();
                return PlayResult::SdRemoved;
            }
            continue;
        }
        const uint64_t readEndUs = esp_timer_get_time();

        void *fb = fbs.fb[(frameIdx + 1) % (size_t)fbs.count];
        if (!fb) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }
        uint32_t decodedSize = 0;
        const uint64_t decodeStartUs = esp_timer_get_time();
        if (!dec.decodeRgb888(input, jpegSize, fb, kFrameBufferSize, &decodedSize)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::BadVideo;
        }
        const uint64_t decodeEndUs = esp_timer_get_time();
        const uint64_t presentStartUs = esp_timer_get_time();
        if (!g_hdmi.present(fb)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }
        const uint64_t presentEndUs = esp_timer_get_time();

        const uint32_t intervalUs = info.frameIntervalUs ? info.frameIntervalUs : kDefaultFrameIntervalUs;
        nextUs += intervalUs;
        const int64_t nowUs = esp_timer_get_time();
        const int64_t sleepUs = (int64_t)nextUs - nowUs;
        logFramePerf("AVI", frameIdx, jpegSize, decodedSize, readEndUs - readStartUs, decodeEndUs - decodeStartUs,
                     presentEndUs - presentStartUs, presentEndUs - frameStartUs, intervalUs, sleepUs);
        if (sleepUs > 0) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(sleepUs / 1000)));
        } else {
            nextUs = nowUs;
        }
        frameIdx++;
    }

    freeInputBuffer(input);
    dec.end();
    r.close();
    return PlayResult::Completed;
}

static PlayResult playMp4(const std::string &path, const HdmiFramebuffers &fbs)
{
    Mp4MjpegReader r;
    if (!r.open(path.c_str())) {
        logMp4Diagnostics(r);
        return PlayResult::BadVideo;
    }
    Mp4MjpegInfo info{};
    if (!r.info(info) || info.timeScale == 0) {
        logMp4Diagnostics(r);
        r.close();
        return PlayResult::BadVideo;
    }
    logMp4Diagnostics(r);
    logStreamConfig("MP4", info.width, info.height);
    Mp4MjpegDiagnostics diag{};
    r.diagnostics(diag);
    const size_t totalFrames = diag.sampleCount;

    JpegHwDecoder dec;
    if (!dec.begin()) {
        r.close();
        return PlayResult::InternalError;
    }

    size_t inputCap = 512 * 1024;
    uint8_t *input = allocInputBuffer(inputCap);
    if (!input) {
        dec.end();
        r.close();
        return PlayResult::InternalError;
    }

    uint64_t nextUs = esp_timer_get_time();
    size_t frameIdx = 0;
    uint64_t playedUs = 0;

    while (true) {
        const uint64_t frameStartUs = esp_timer_get_time();
        size_t sampleSize = 0;
        uint32_t durTs = 0;
        const uint64_t readStartUs = esp_timer_get_time();
        if (!r.readNextSample(input, inputCap, sampleSize, durTs)) {
            if (!r.rewindToFirstFrame()) {
                freeInputBuffer(input);
                dec.end();
                r.close();
                return PlayResult::SdRemoved;
            }
            continue;
        }
        const uint64_t readEndUs = esp_timer_get_time();
        if (!looksLikeJpeg(input, sampleSize)) {
            Serial.println("[player] MP4 sample is not a JPEG bitstream");
            logSamplePrefix(input, sampleSize);
            logMp4Diagnostics(r);
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::BadVideo;
        }

        void *fb = fbs.fb[(frameIdx + 1) % (size_t)fbs.count];
        if (!fb) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }
        uint32_t decodedSize = 0;
        const uint64_t decodeStartUs = esp_timer_get_time();
        if (!dec.decodeRgb888(input, sampleSize, fb, kFrameBufferSize, &decodedSize)) {
            Serial.println("[player] MP4 JPEG decode failed");
            logMp4Diagnostics(r);
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::BadVideo;
        }
        const uint64_t decodeEndUs = esp_timer_get_time();
        const uint64_t presentStartUs = esp_timer_get_time();
        if (!g_hdmi.present(fb)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }
        const uint64_t presentEndUs = esp_timer_get_time();

        const uint32_t intervalUs =
            durTs ? (uint32_t)((uint64_t)durTs * 1000000ULL / (uint64_t)info.timeScale) : kDefaultFrameIntervalUs;
        playedUs += intervalUs;
        nextUs += intervalUs;
        const int64_t nowUs = esp_timer_get_time();
        const int64_t sleepUs = (int64_t)nextUs - nowUs;
        logFramePerf("MP4", frameIdx, sampleSize, decodedSize, readEndUs - readStartUs, decodeEndUs - decodeStartUs,
                     presentEndUs - presentStartUs, presentEndUs - frameStartUs, intervalUs, sleepUs);
        logMp4Progress(frameIdx, totalFrames, playedUs);
        if (sleepUs > 0) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(sleepUs / 1000)));
        } else {
            nextUs = nowUs;
        }
        frameIdx++;
    }

    freeInputBuffer(input);
    dec.end();
    r.close();
    return PlayResult::Completed;
}

static void playerTask(void *)
{
    const HdmiFramebuffers fbs = g_hdmi.framebuffers();
    Serial.println("[player] task started");

    while (true) {
        Serial.println("[player] mounting SD card");
        if (!g_sd.mount()) {
            Serial.println("[player] mount failed, retrying");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        Serial.println("[player] SD mounted");

        std::string path;
        VideoContainerType type{};
        if (!findTestVideoFile(kMountPoint, path, type)) {
            Serial.println("[player] ERROR: test_video.(mp4|avi) not found");
            g_sd.unmount();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        Serial.print("[player] found video: ");
        Serial.println(path.c_str());
        logFileStat(path);

        if (type == VideoContainerType::Mp4) {
            Serial.println("[player] playing MP4");
            PlayResult res = playMp4(path, fbs);
            if (res == PlayResult::BadVideo) {
                Serial.println("[player] ERROR: unsupported or invalid video");
                g_sd.unmount();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            if (res == PlayResult::InternalError) {
                Serial.println("[player] ERROR: internal playback/display failure");
            }
            if (res != PlayResult::SdRemoved) {
                g_sd.unmount();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        } else {
            Serial.println("[player] playing AVI");
            PlayResult res = playAvi(path, fbs);
            if (res == PlayResult::BadVideo) {
                Serial.println("[player] ERROR: unsupported or invalid video");
                g_sd.unmount();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            if (res == PlayResult::InternalError) {
                Serial.println("[player] ERROR: internal playback/display failure");
            }
            if (res != PlayResult::SdRemoved) {
                g_sd.unmount();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }

        Serial.println("[player] SD removed");
        g_sd.unmount();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println("[main] setup start");

    if (!g_hdmi.begin()) {
        Serial.println("[main] HDMI init failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    Serial.println("[main] HDMI init OK");

    xTaskCreatePinnedToCore(playerTask, "player", 12288, nullptr, 5, nullptr, 1);
    Serial.println("[main] player task created");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
