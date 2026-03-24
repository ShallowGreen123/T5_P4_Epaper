#include <Arduino.h>
#include <ctype.h>
#include <SD.h>
#include <string.h>

#include "app_stream_adapter.h"
#include "driver/jpeg_decode.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "t5_p4_bsp.h"

namespace {

struct DemoState {
    app_stream_adapter_handle_t adapter = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    jpeg_decoder_handle_t jpeg_decoder = nullptr;
    uint8_t *decode_buffer = nullptr;
    size_t decode_buffer_size = 0;
    bool decode_buffer_is_hw = false;
    bool eos = false;
    bool error = false;
    esp_err_t last_error = ESP_OK;
    uint32_t frame_logs = 0;
    uint32_t stream_width = 0;
    uint32_t stream_height = 0;
    uint32_t stream_fps = 0;
    uint32_t stream_duration = 0;
};

DemoState g_demo;

void free_decode_buffer()
{
    if (g_demo.decode_buffer == nullptr) {
        return;
    }

    if (g_demo.decode_buffer_is_hw) {
        free(g_demo.decode_buffer);
    } else {
        heap_caps_free(g_demo.decode_buffer);
    }

    g_demo.decode_buffer = nullptr;
    g_demo.decode_buffer_size = 0;
    g_demo.decode_buffer_is_hw = false;
}

uint8_t *alloc_jpeg_output_buffer(size_t required_size,
                                  size_t *allocated_size,
                                  bool *is_hw_buffer)
{
    constexpr size_t kAlignment = 64;
    const size_t aligned_size = (required_size + kAlignment - 1) & ~(kAlignment - 1);

    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    void *buffer = jpeg_alloc_decoder_mem(aligned_size, &mem_cfg, allocated_size);
    if (buffer != nullptr) {
        if (is_hw_buffer != nullptr) {
            *is_hw_buffer = true;
        }
        return static_cast<uint8_t *>(buffer);
    }

    buffer = heap_caps_aligned_calloc(kAlignment, 1, aligned_size, MALLOC_CAP_SPIRAM);
    if (buffer != nullptr) {
        if (allocated_size != nullptr) {
            *allocated_size = aligned_size;
        }
        if (is_hw_buffer != nullptr) {
            *is_hw_buffer = false;
        }
    }
    return static_cast<uint8_t *>(buffer);
}

esp_err_t ensure_jpeg_decoder()
{
    if (g_demo.jpeg_decoder != nullptr) {
        return ESP_OK;
    }

    jpeg_decode_engine_cfg_t decoder_cfg = {
        .intr_priority = 0,
        .timeout_ms = 1000,
    };
    return jpeg_new_decoder_engine(&decoder_cfg, &g_demo.jpeg_decoder);
}

esp_err_t ensure_decode_buffer(uint32_t width, uint32_t height)
{
    if (width > BSP_LCD_H_RES || height > BSP_LCD_V_RES) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const size_t required_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U;
    if (g_demo.decode_buffer != nullptr && g_demo.decode_buffer_size >= required_size) {
        return ESP_OK;
    }

    free_decode_buffer();
    g_demo.decode_buffer = alloc_jpeg_output_buffer(required_size,
                                                    &g_demo.decode_buffer_size,
                                                    &g_demo.decode_buffer_is_hw);
    return (g_demo.decode_buffer != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

void print_sd_root()
{
    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("[main] cannot open SD root directory");
        return;
    }

    Serial.println("[main] SD root entries:");
    File entry = root.openNextFile();
    while (entry) {
        Serial.printf("  %s%s (%lu bytes)\n",
                      entry.name(),
                      entry.isDirectory() ? "/" : "",
                      static_cast<unsigned long>(entry.size()));
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

bool has_suffix_ci(const char *name, const char *suffix)
{
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) {
        return false;
    }

    const char *tail = name + (name_len - suffix_len);
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower(static_cast<unsigned char>(tail[i])) !=
            tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

String find_media_file()
{
    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        return String();
    }

    String avi_candidate;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *name = entry.name();
            if (has_suffix_ci(name, ".mp4")) {
                entry.close();
                root.close();
                return String("/") + name;
            }
            if (avi_candidate.isEmpty() && has_suffix_ci(name, ".avi")) {
                avi_candidate = String("/") + name;
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    return avi_candidate;
}

esp_err_t on_stream_frame(uint8_t *buffer,
                          uint32_t buffer_size,
                          uint32_t width,
                          uint32_t height,
                          uint32_t frame_index,
                          void *user_data)
{
    (void)user_data;

    if (g_demo.panel == nullptr) {
        g_demo.error = true;
        g_demo.last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    if (width > BSP_LCD_H_RES || height > BSP_LCD_V_RES) {
        if (g_demo.frame_logs == 0) {
            Serial.printf("[main] frame %lux%lu exceeds HDMI canvas %ux%u, scaling is deferred to next phase\n",
                          static_cast<unsigned long>(width),
                          static_cast<unsigned long>(height),
                          BSP_LCD_H_RES,
                          BSP_LCD_V_RES);
            g_demo.frame_logs++;
        }
        g_demo.error = true;
        g_demo.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = ensure_jpeg_decoder();
    if (err != ESP_OK) {
        g_demo.error = true;
        g_demo.last_error = err;
        return err;
    }

    err = ensure_decode_buffer(width, height);
    if (err != ESP_OK) {
        g_demo.error = true;
        g_demo.last_error = err;
        return err;
    }

    jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB888,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t decoded_size = 0;
    err = jpeg_decoder_process(g_demo.jpeg_decoder,
                               &decode_cfg,
                               buffer,
                               buffer_size,
                               g_demo.decode_buffer,
                               static_cast<uint32_t>(g_demo.decode_buffer_size),
                               &decoded_size);
    if (err != ESP_OK) {
        Serial.printf("[main] JPEG decode failed on frame #%lu: %s\n",
                      static_cast<unsigned long>(frame_index),
                      esp_err_to_name(err));
        g_demo.error = true;
        g_demo.last_error = err;
        return err;
    }

    err = esp_lcd_panel_draw_bitmap(g_demo.panel, 0, 0, width, height, g_demo.decode_buffer);
    if (err != ESP_OK) {
        Serial.printf("[main] HDMI draw failed on frame #%lu: %s\n",
                      static_cast<unsigned long>(frame_index),
                      esp_err_to_name(err));
        g_demo.error = true;
        g_demo.last_error = err;
        return err;
    }

    if (g_demo.frame_logs < 8 || (frame_index % 30U) == 0) {
        Serial.printf("[main] HDMI frame #%lu drawn, %lux%lu, jpeg=%lu bytes, rgb=%lu bytes\n",
                      static_cast<unsigned long>(frame_index),
                      static_cast<unsigned long>(width),
                      static_cast<unsigned long>(height),
                      static_cast<unsigned long>(buffer_size),
                      static_cast<unsigned long>(decoded_size));
        g_demo.frame_logs++;
    }

    return ESP_OK;
}

void on_stream_event(app_stream_event_t event, esp_err_t error, void *user_data)
{
    (void)user_data;
    if (event == APP_STREAM_EVENT_EOS) {
        g_demo.eos = true;
        Serial.println("[main] stream reached EOS");
        return;
    }

    g_demo.error = true;
    g_demo.last_error = error;
    Serial.printf("[main] stream error: %s\n", esp_err_to_name(error));
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("[main] hdmi_video phase4 minimal HDMI render");
    Serial.println("[main] constraints: ESP32-P4 + PlatformIO/Arduino + SD local media + HDMI + MJPEG/MP4 path");

    bsp_display_config_t display_config = {
        .hdmi_resolution = BSP_HDMI_RES_1280x720,
        .dsi_bus = {
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        },
    };

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_err_t err = bsp_display_new(&display_config, &panel, &io);
    if (err == ESP_OK) {
        g_demo.panel = panel;
        g_demo.panel_io = io;
        Serial.println("[main] HDMI display init OK");
    } else {
        Serial.printf("[main] HDMI display init failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = bsp_sdcard_mount();
    if (err == ESP_OK) {
        Serial.println("[main] SD mount OK");
        print_sd_root();
    } else {
        Serial.printf("[main] SD mount failed: %s\n", esp_err_to_name(err));
        return;
    }

    esp_codec_dev_handle_t audio = bsp_audio_codec_speaker_init();
    if (audio != nullptr) {
        Serial.println("[main] audio codec handle ready");
    } else {
        Serial.println("[main] audio codec handle not ready in current phase");
    }

    String media_file = find_media_file();
    if (media_file.isEmpty()) {
        Serial.println("[main] no .mp4 or .avi media file found in SD root");
        return;
    }
    Serial.printf("[main] selected media: %s\n", media_file.c_str());

    err = app_stream_adapter_probe_video_info(media_file.c_str(),
                                              &g_demo.stream_width,
                                              &g_demo.stream_height,
                                              &g_demo.stream_fps,
                                              &g_demo.stream_duration);
    if (err != ESP_OK) {
        Serial.printf("[main] media probe failed: %s\n", esp_err_to_name(err));
        return;
    }

    Serial.printf("[main] probed media: %lux%lu, %lu fps, duration=%lu ms\n",
                  static_cast<unsigned long>(g_demo.stream_width),
                  static_cast<unsigned long>(g_demo.stream_height),
                  static_cast<unsigned long>(g_demo.stream_fps),
                  static_cast<unsigned long>(g_demo.stream_duration));

    if (g_demo.stream_width > BSP_LCD_H_RES || g_demo.stream_height > BSP_LCD_V_RES) {
        Serial.printf("[main] video exceeds current no-scale path: %lux%lu > %ux%u\n",
                      static_cast<unsigned long>(g_demo.stream_width),
                      static_cast<unsigned long>(g_demo.stream_height),
                      BSP_LCD_H_RES,
                      BSP_LCD_V_RES);
        return;
    }

    err = ensure_jpeg_decoder();
    if (err != ESP_OK) {
        Serial.printf("[main] JPEG decoder init failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = ensure_decode_buffer(g_demo.stream_width, g_demo.stream_height);
    if (err != ESP_OK) {
        Serial.printf("[main] decode buffer allocation failed: %s\n", esp_err_to_name(err));
        return;
    }

    app_stream_adapter_config_t adapter_config = {
        .frame_cb = on_stream_frame,
        .event_cb = on_stream_event,
        .user_data = nullptr,
        .decode_buffers = nullptr,
        .buffer_count = 0,
        .buffer_size = 0,
        .audio_dev = audio,
        .jpeg_config = APP_STREAM_JPEG_CONFIG_DEFAULT_RGB888(),
        .target_width = 0,
        .target_height = 0,
    };

    err = app_stream_adapter_init(&adapter_config, &g_demo.adapter);
    if (err != ESP_OK) {
        Serial.printf("[main] adapter init failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = app_stream_adapter_set_file(g_demo.adapter, media_file.c_str(), false);
    if (err != ESP_OK) {
        Serial.printf("[main] adapter set file failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = app_stream_adapter_start(g_demo.adapter);
    if (err != ESP_OK) {
        Serial.printf("[main] adapter start failed: %s\n", esp_err_to_name(err));
        return;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    uint32_t duration = 0;
    err = app_stream_adapter_get_info(g_demo.adapter, &width, &height, &fps, &duration);
    if (err == ESP_OK) {
        Serial.printf("[main] stream info: %lux%lu, %lu fps, duration=%lu ms\n",
                      static_cast<unsigned long>(width),
                      static_cast<unsigned long>(height),
                      static_cast<unsigned long>(fps),
                      static_cast<unsigned long>(duration));
    }

    Serial.println("[main] phase4 started: SD local MP4/AVI -> MJPEG -> JPEG decode -> HDMI draw");
}

void loop()
{
    delay(1000);

    if (g_demo.adapter == nullptr) {
        Serial.println("[main] idle: adapter not initialized");
        return;
    }

    app_stream_stats_t stats = {};
    const esp_err_t err = app_stream_adapter_get_stats(g_demo.adapter, &stats);
    if (err == ESP_OK) {
        Serial.printf("[main] stats: frames=%lu current_fps=%.2f eos=%d error=%d last=%s\n",
                      static_cast<unsigned long>(stats.frames_processed),
                      stats.current_fps,
                      g_demo.eos ? 1 : 0,
                      g_demo.error ? 1 : 0,
                      esp_err_to_name(g_demo.last_error));
    } else {
        Serial.printf("[main] stats unavailable: %s\n", esp_err_to_name(err));
    }
}
