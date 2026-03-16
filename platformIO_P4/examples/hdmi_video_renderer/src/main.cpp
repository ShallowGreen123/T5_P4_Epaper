#include <Arduino.h>

#include <errno.h>
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
static constexpr size_t kRgb888Size = (size_t)kWidth * (size_t)kHeight * 3;
static constexpr const char *kMountPoint = "/sdcard";

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

static PlayResult playAvi(const std::string &path, const HdmiFramebuffers &fbs)
{
    AviMjpegReader r;
    if (!r.open(path.c_str())) {
        return PlayResult::SdRemoved;
    }
    AviMjpegInfo info{};
    r.info(info);

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
    uint64_t lastLogUs = nextUs;

    while (true) {
        const uint64_t frameStartUs = esp_timer_get_time();
        if (frameStartUs - lastLogUs >= 1000000) {
            Serial.printf("[player] AVI frame %u (1s interval)\n", (unsigned)frameIdx);
            lastLogUs = frameStartUs;
        }

        size_t jpegSize = 0;
        if (!r.readNextJpeg(input, inputCap, jpegSize)) {
            if (!r.rewindToFirstFrame()) {
                freeInputBuffer(input);
                dec.end();
                r.close();
                return PlayResult::SdRemoved;
            }
            continue;
        }

        void *fb = fbs.fb[frameIdx % (size_t)fbs.count];
        if (!fb) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }
        if (!dec.decodeRgb888(input, jpegSize, static_cast<uint8_t *>(fb), kRgb888Size)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::BadVideo;
        }
        if (!g_hdmi.present(fb)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }

        const uint64_t frameEndUs = esp_timer_get_time();
        const uint64_t frameDurationUs = frameEndUs - frameStartUs;
        if (frameDurationUs > 0) {
            Serial.printf("[player] AVI frame %u decode+present %.0f ms\n", (unsigned)frameIdx, frameDurationUs / 1000.0);
        }

        const uint32_t intervalUs = info.frameIntervalUs ? info.frameIntervalUs : 16666;
        nextUs += intervalUs;
        const int64_t nowUs = esp_timer_get_time();
        const int64_t sleepUs = (int64_t)nextUs - nowUs;
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
        return PlayResult::BadVideo;
    }
    Mp4MjpegInfo info{};
    if (!r.info(info) || info.timeScale == 0) {
        r.close();
        return PlayResult::BadVideo;
    }

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
    uint64_t lastLogUs = nextUs;

    while (true) {
        const uint64_t frameStartUs = esp_timer_get_time();
        if (frameStartUs - lastLogUs >= 1000000) {
            Serial.printf("[player] MP4 frame %u (1s interval)\n", (unsigned)frameIdx);
            lastLogUs = frameStartUs;
        }

        size_t sampleSize = 0;
        uint32_t durTs = 0;
        if (!r.readNextSample(input, inputCap, sampleSize, durTs)) {
            if (!r.rewindToFirstFrame()) {
                freeInputBuffer(input);
                dec.end();
                r.close();
                return PlayResult::SdRemoved;
            }
            continue;
        }

        void *fb = fbs.fb[frameIdx % (size_t)fbs.count];
        if (!fb) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }
        if (!dec.decodeRgb888(input, sampleSize, static_cast<uint8_t *>(fb), kRgb888Size)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::BadVideo;
        }
        if (!g_hdmi.present(fb)) {
            freeInputBuffer(input);
            dec.end();
            r.close();
            return PlayResult::InternalError;
        }

        const uint64_t frameEndUs = esp_timer_get_time();
        const uint64_t frameDurationUs = frameEndUs - frameStartUs;
        if (frameDurationUs > 0) {
            Serial.printf("[player] MP4 frame %u decode+present %.0f ms\n", (unsigned)frameIdx, frameDurationUs / 1000.0);
        }

        const uint32_t intervalUs = durTs ? (uint32_t)((uint64_t)durTs * 1000000ULL / (uint64_t)info.timeScale) : 16666;
        nextUs += intervalUs;
        const int64_t nowUs = esp_timer_get_time();
        const int64_t sleepUs = (int64_t)nextUs - nowUs;
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

        if (type == VideoContainerType::Mp4) {
            Serial.println("[player] playing MP4");
            PlayResult res = playMp4(path, fbs);
            if (res == PlayResult::BadVideo) {
                Serial.println("[player] ERROR: unsupported or invalid video");
                g_sd.unmount();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
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
