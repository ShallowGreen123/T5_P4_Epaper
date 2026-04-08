/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "driver/i2c_master.h"
#include "driver/jpeg_encode.h"
#include "esp_bit_defs.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef CONFIG_CAMERA_WIFI_STREAM_MAX_STREAM_FPS
#define CONFIG_CAMERA_WIFI_STREAM_MAX_STREAM_FPS 12
#endif

#ifndef CONFIG_CAMERA_WIFI_STREAM_VIDEO_BUFFER_COUNT
#define CONFIG_CAMERA_WIFI_STREAM_VIDEO_BUFFER_COUNT 4
#endif

#ifndef CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV
#define CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV 0
#endif

#ifndef CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV
#define CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV 0
#endif

#define SGM38121_I2C_ADDR                    0x28
#define SGM38121_REG_CHIP_REV                0x00
#define SGM38121_REG_DISCH                   0x02
#define SGM38121_REG_DVDD1_VOUT              0x03
#define SGM38121_REG_DVDD2_VOUT              0x04
#define SGM38121_REG_AVDD1_VOUT              0x05
#define SGM38121_REG_AVDD2_VOUT              0x06
#define SGM38121_REG_FUNCTION                0x07
#define SGM38121_REG_SEQ_DVDD                0x0A
#define SGM38121_REG_SEQ_AVDD                0x0B
#define SGM38121_REG_ENABLE                  0x0E
#define SGM38121_ENABLE_DVDD1_BIT            BIT0
#define SGM38121_ENABLE_DVDD2_BIT            BIT1
#define SGM38121_ENABLE_AVDD1_BIT            BIT2
#define SGM38121_ENABLE_AVDD2_BIT            BIT3

#define CAMERA_SCCB_ADDR_SC2336              0x30
#define CAMERA_SCCB_ADDR_OV2710              0x36
#define CAMERA_SCCB_ADDR_OV5645              0x3C

#define CAMERA_POWER_SETTLE_MS               20
#define I2C_TIMEOUT_MS                       100
#define VIDEO_BUFFER_COUNT                   CONFIG_CAMERA_WIFI_STREAM_VIDEO_BUFFER_COUNT
#define WIFI_CONNECTED_BIT                   BIT0
#define WIFI_FAIL_BIT                        BIT1
#define HTTP_PART_BOUNDARY                   "frame"

static const char *TAG = "camera_wifi_stream";
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" HTTP_PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" HTTP_PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %" PRIu32 "\r\n\r\n";

typedef struct {
    jpeg_encoder_handle_t handle;
    jpeg_encode_cfg_t config;
    uint8_t *out_buf;
    uint32_t out_size;
    uint32_t input_size;
} jpeg_encoder_ctx_t;

typedef struct {
    int fd;
    bool streaming;
    uint8_t *buffer[VIDEO_BUFFER_COUNT];
    size_t buffer_len[VIDEO_BUFFER_COUNT];
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t frame_rate;
    jpeg_encoder_ctx_t encoder;
    SemaphoreHandle_t stream_lock;
} camera_stream_ctx_t;

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sgm;
static camera_stream_ctx_t s_camera = {
    .fd = -1,
};
static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_num;
static esp_netif_ip_info_t s_ip_info;
static httpd_handle_t s_httpd;

static void camera_stream_stop(camera_stream_ctx_t *video);

#if CONFIG_CAMERA_WIFI_STREAM_XCLK_GPIO >= 0
static esp_cam_sensor_xclk_handle_t s_xclk_handle;
#endif

static esp_err_t sgm_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_sgm, &reg, sizeof(reg), value, 1, I2C_TIMEOUT_MS);
}

static esp_err_t sgm_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(s_sgm, payload, sizeof(payload), I2C_TIMEOUT_MS);
}

static uint8_t sgm_encode_vout_mv_rounded(int target_mv, int min_mv, int max_mv, int offset_mv, int min_reg, int max_reg, int *actual_mv)
{
    int clamped_mv = target_mv;

    if (clamped_mv < min_mv) {
        clamped_mv = min_mv;
    }
    if (clamped_mv > max_mv) {
        clamped_mv = max_mv;
    }

    int reg_value = (clamped_mv - offset_mv + 4) / 8;
    if (reg_value < min_reg) {
        reg_value = min_reg;
    }
    if (reg_value > max_reg) {
        reg_value = max_reg;
    }

    if (actual_mv != NULL) {
        *actual_mv = offset_mv + (reg_value * 8);
    }

    return (uint8_t)reg_value;
}

static uint8_t sgm_encode_dvdd_mv_rounded(int target_mv, int *actual_mv)
{
    return sgm_encode_vout_mv_rounded(target_mv, 528, 1504, 504, 0x03, 0x7D, actual_mv);
}

static uint8_t sgm_encode_avdd_mv_rounded(int target_mv, int *actual_mv)
{
    return sgm_encode_vout_mv_rounded(target_mv, 1504, 3424, 1384, 0x0F, 0xFF, actual_mv);
}

static const char *probe_status_to_str(esp_err_t err)
{
    if (err == ESP_OK) {
        return "ACK";
    }
    if (err == ESP_ERR_TIMEOUT) {
        return "TIMEOUT";
    }
    return "NO-ACK";
}

static void camera_probe_known_addresses(const char *label)
{
    static const uint8_t addresses[] = {
        SGM38121_I2C_ADDR,
        CAMERA_SCCB_ADDR_SC2336,
        CAMERA_SCCB_ADDR_OV2710,
        CAMERA_SCCB_ADDR_OV5645,
    };

    if (s_i2c_bus == NULL) {
        ESP_LOGW(TAG, "Skip I2C diagnostics (%s): bus is not initialized", label);
        return;
    }

    ESP_LOGI(TAG, "I2C diagnostics (%s)", label);
    for (size_t i = 0; i < ARRAY_SIZE(addresses); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, addresses[i], I2C_TIMEOUT_MS);
        ESP_LOGI(TAG, "  0x%02X -> %s (%s)", addresses[i], probe_status_to_str(err), esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2C timeout while probing 0x%02X, check wiring and pull-ups", addresses[i]);
            break;
        }
    }
}

static bool stream_rate_limiter_should_send(uint32_t source_fps, uint32_t target_fps, uint32_t *credit)
{
    if (source_fps == 0 || target_fps == 0 || target_fps >= source_fps) {
        return true;
    }

    *credit += target_fps;
    if (*credit < source_fps) {
        return false;
    }

    *credit -= source_fps;
    return true;
}

static esp_err_t camera_init_i2c_for_power(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = CONFIG_CAMERA_WIFI_STREAM_I2C_PORT,
        .sda_io_num = CONFIG_CAMERA_WIFI_STREAM_I2C_SDA,
        .scl_io_num = CONFIG_CAMERA_WIFI_STREAM_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "create I2C master bus failed");
    ESP_LOGI(TAG,
             "I2C ready on port=%d SDA=%d SCL=%d freq=%d Hz",
             CONFIG_CAMERA_WIFI_STREAM_I2C_PORT,
             CONFIG_CAMERA_WIFI_STREAM_I2C_SDA,
             CONFIG_CAMERA_WIFI_STREAM_I2C_SCL,
             CONFIG_CAMERA_WIFI_STREAM_I2C_FREQ_HZ);
    return ESP_OK;
}

static esp_err_t camera_init_sgm_device(void)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SGM38121_I2C_ADDR,
        .scl_speed_hz = CONFIG_CAMERA_WIFI_STREAM_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_probe(s_i2c_bus, SGM38121_I2C_ADDR, I2C_TIMEOUT_MS),
                        TAG,
                        "SGM38121 probe failed at 0x%02X",
                        SGM38121_I2C_ADDR);
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_sgm), TAG, "add SGM38121 device failed");
    return ESP_OK;
}

static void camera_power_i2c_cleanup(void)
{
    if (s_sgm != NULL) {
        esp_err_t err = i2c_master_bus_rm_device(s_sgm);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Remove SGM38121 device failed: %s", esp_err_to_name(err));
        }
        s_sgm = NULL;
    }

    if (s_i2c_bus != NULL) {
        esp_err_t err = i2c_del_master_bus(s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Delete camera power I2C bus failed: %s", esp_err_to_name(err));
        }
        s_i2c_bus = NULL;
    }
}

static esp_err_t camera_enable_power(void)
{
#if CONFIG_CAMERA_WIFI_STREAM_ENABLE_CAMERA_POWER
    int avdd1_actual_mv = 0;
    int avdd2_actual_mv = 0;
    int dvdd1_actual_mv = 0;
    int dvdd2_actual_mv = 0;
    uint8_t avdd1_reg = sgm_encode_avdd_mv_rounded(CONFIG_CAMERA_WIFI_STREAM_AVDD1_MV, &avdd1_actual_mv);
    uint8_t avdd2_reg = sgm_encode_avdd_mv_rounded(CONFIG_CAMERA_WIFI_STREAM_AVDD2_MV, &avdd2_actual_mv);
    uint8_t dvdd1_reg = 0;
    uint8_t dvdd2_reg = 0;
    uint8_t enable_mask = SGM38121_ENABLE_AVDD1_BIT | SGM38121_ENABLE_AVDD2_BIT;
    uint8_t chip_rev = 0;
    esp_err_t ret;

    if (CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV > 0) {
        dvdd1_reg = sgm_encode_dvdd_mv_rounded(CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV, &dvdd1_actual_mv);
        enable_mask |= SGM38121_ENABLE_DVDD1_BIT;
    }
    if (CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV > 0) {
        dvdd2_reg = sgm_encode_dvdd_mv_rounded(CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV, &dvdd2_actual_mv);
        enable_mask |= SGM38121_ENABLE_DVDD2_BIT;
    }

    ESP_RETURN_ON_ERROR(camera_init_i2c_for_power(), TAG, "camera power I2C init failed");
    camera_probe_known_addresses("before_power_on");

    ret = camera_init_sgm_device();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop camera initialization because SGM38121 power setup failed: %s", esp_err_to_name(ret));
        camera_probe_known_addresses("sgm_probe_failure");
        camera_power_i2c_cleanup();
        return ret;
    }

    if (sgm_read_reg(SGM38121_REG_CHIP_REV, &chip_rev) == ESP_OK) {
        ESP_LOGI(TAG, "SGM38121 CHIP_REV=0x%02X", chip_rev);
    }

    ESP_LOGI(TAG,
             "Enabling camera rails: DVDD1=%d mV DVDD2=%d mV AVDD1=%d mV AVDD2=%d mV",
             dvdd1_actual_mv,
             dvdd2_actual_mv,
             avdd1_actual_mv,
             avdd2_actual_mv);

    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_SEQ_DVDD, 0x00), fail, TAG, "set DVDD sequence failed");
    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_SEQ_AVDD, 0x00), fail, TAG, "set AVDD sequence failed");
    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_FUNCTION, 0x00), fail, TAG, "disable wake-up failed");
    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_DISCH, 0x00), fail, TAG, "set discharge mode failed");
    if (CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV > 0) {
        if (dvdd1_actual_mv != CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV) {
            ESP_LOGW(TAG,
                     "DVDD1 requested %d mV, rounded/clamped to %d mV (reg=0x%02X)",
                     CONFIG_CAMERA_WIFI_STREAM_DVDD1_MV,
                     dvdd1_actual_mv,
                     dvdd1_reg);
        }
        ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_DVDD1_VOUT, dvdd1_reg), fail, TAG, "write DVDD1 failed");
    }
    if (CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV > 0) {
        if (dvdd2_actual_mv != CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV) {
            ESP_LOGW(TAG,
                     "DVDD2 requested %d mV, rounded/clamped to %d mV (reg=0x%02X)",
                     CONFIG_CAMERA_WIFI_STREAM_DVDD2_MV,
                     dvdd2_actual_mv,
                     dvdd2_reg);
        }
        ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_DVDD2_VOUT, dvdd2_reg), fail, TAG, "write DVDD2 failed");
    }
    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_AVDD1_VOUT, avdd1_reg), fail, TAG, "write AVDD1 failed");
    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_AVDD2_VOUT, avdd2_reg), fail, TAG, "write AVDD2 failed");
    ESP_GOTO_ON_ERROR(sgm_write_reg(SGM38121_REG_ENABLE, enable_mask), fail, TAG, "enable camera rails failed");

    vTaskDelay(pdMS_TO_TICKS(CAMERA_POWER_SETTLE_MS));
    camera_probe_known_addresses("after_power_on");
    camera_power_i2c_cleanup();
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Stop camera initialization because SGM38121 power setup failed: %s", esp_err_to_name(ret));
    camera_power_i2c_cleanup();
    return ret;
#else
    ESP_LOGW(TAG, "Camera power enable on boot is disabled");
    return ESP_OK;
#endif
}

static esp_err_t camera_start_xclk(void)
{
#if CONFIG_CAMERA_WIFI_STREAM_XCLK_GPIO >= 0
    esp_cam_sensor_xclk_config_t xclk_config = {
        .esp_clock_router_cfg = {
            .xclk_pin = CONFIG_CAMERA_WIFI_STREAM_XCLK_GPIO,
            .xclk_freq_hz = CONFIG_CAMERA_WIFI_STREAM_XCLK_FREQ_HZ,
        },
    };

    ESP_LOGI(TAG,
             "Starting camera XCLK on GPIO%d at %d Hz",
             CONFIG_CAMERA_WIFI_STREAM_XCLK_GPIO,
             CONFIG_CAMERA_WIFI_STREAM_XCLK_FREQ_HZ);
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle),
                        TAG,
                        "allocate XCLK failed");
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_config), TAG, "start XCLK failed");
#endif
    return ESP_OK;
}

static void camera_stop_xclk(void)
{
#if CONFIG_CAMERA_WIFI_STREAM_XCLK_GPIO >= 0
    if (s_xclk_handle != NULL) {
        esp_err_t err = esp_cam_sensor_xclk_stop(s_xclk_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Stop XCLK failed: %s", esp_err_to_name(err));
        }
        err = esp_cam_sensor_xclk_free(s_xclk_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Free XCLK failed: %s", esp_err_to_name(err));
        }
        s_xclk_handle = NULL;
    }
#endif
}

static esp_err_t camera_video_system_init(void)
{
    const esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = CONFIG_CAMERA_WIFI_STREAM_I2C_PORT,
                .scl_pin = CONFIG_CAMERA_WIFI_STREAM_I2C_SCL,
                .sda_pin = CONFIG_CAMERA_WIFI_STREAM_I2C_SDA,
            },
            .freq = CONFIG_CAMERA_WIFI_STREAM_I2C_FREQ_HZ,
        },
        .reset_pin = (gpio_num_t)CONFIG_CAMERA_WIFI_STREAM_RESET_GPIO,
        .pwdn_pin = (gpio_num_t)CONFIG_CAMERA_WIFI_STREAM_PWDN_GPIO,
        .dont_init_ldo = false,
    };
    const esp_video_init_config_t video_config = {
        .csi = &csi_config,
    };

    ESP_RETURN_ON_ERROR(camera_start_xclk(), TAG, "camera XCLK init failed");
    ESP_LOGI(TAG, "Initializing ESP video MIPI-CSI device %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    esp_err_t ret = esp_video_init(&video_config);
    if (ret != ESP_OK) {
        camera_stop_xclk();
        ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static uint32_t frame_size_for_format(uint32_t width, uint32_t height, uint32_t pixelformat)
{
    switch (pixelformat) {
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_UYVY:
        return width * height * 2;
    case V4L2_PIX_FMT_RGB24:
        return width * height * 3;
    case V4L2_PIX_FMT_GREY:
    case V4L2_PIX_FMT_SBGGR8:
        return width * height;
#if CONFIG_ESP32P4_REV_MIN_FULL >= 300
    case V4L2_PIX_FMT_YUV420:
        return width * height * 3 / 2;
#endif
    default:
        return 0;
    }
}

static esp_err_t jpeg_encoder_config_from_v4l2(uint32_t pixelformat,
                                               uint32_t width,
                                               uint32_t height,
                                               uint8_t quality,
                                               jpeg_encode_cfg_t *config,
                                               uint32_t *input_size)
{
    memset(config, 0, sizeof(*config));
    config->height = height;
    config->width = width;
    config->image_quality = quality;

    switch (pixelformat) {
    case V4L2_PIX_FMT_SBGGR8:
    case V4L2_PIX_FMT_GREY:
        config->src_type = JPEG_ENCODE_IN_FORMAT_GRAY;
        config->sub_sample = JPEG_DOWN_SAMPLING_GRAY;
        break;
    case V4L2_PIX_FMT_RGB565:
        config->src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
        config->sub_sample = JPEG_DOWN_SAMPLING_YUV422;
        break;
    case V4L2_PIX_FMT_RGB24:
        config->src_type = JPEG_ENCODE_IN_FORMAT_RGB888;
        config->sub_sample = JPEG_DOWN_SAMPLING_YUV444;
        break;
    case V4L2_PIX_FMT_UYVY:
        config->src_type = JPEG_ENCODE_IN_FORMAT_YUV422;
        config->sub_sample = JPEG_DOWN_SAMPLING_YUV422;
        break;
#if CONFIG_ESP32P4_REV_MIN_FULL >= 300
    case V4L2_PIX_FMT_YUV420:
        config->src_type = JPEG_ENCODE_IN_FORMAT_YUV420;
        config->sub_sample = JPEG_DOWN_SAMPLING_YUV420;
        break;
#endif
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }

    *input_size = frame_size_for_format(width, height, pixelformat);
    return (*input_size > 0) ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static bool jpeg_encoder_supports_v4l2(uint32_t pixelformat)
{
    jpeg_encode_cfg_t config = {0};
    uint32_t input_size = 0;
    return jpeg_encoder_config_from_v4l2(pixelformat, 16, 16, CONFIG_CAMERA_WIFI_STREAM_JPEG_QUALITY, &config, &input_size) == ESP_OK;
}

static esp_err_t jpeg_encoder_init(jpeg_encoder_ctx_t *encoder,
                                   uint32_t pixelformat,
                                   uint32_t width,
                                   uint32_t height,
                                   uint8_t quality)
{
    jpeg_encode_memory_alloc_cfg_t output_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    jpeg_encode_engine_cfg_t engine_cfg = {
        .timeout_ms = 5000,
    };
    size_t allocated_size = 0;
    esp_err_t ret;

    ESP_RETURN_ON_ERROR(jpeg_encoder_config_from_v4l2(pixelformat, width, height, quality, &encoder->config, &encoder->input_size),
                        TAG,
                        "unsupported JPEG encoder input format " V4L2_FMT_STR,
                        V4L2_FMT_STR_ARG(pixelformat));

    ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&engine_cfg, &encoder->handle), TAG, "create JPEG encoder failed");

    encoder->out_buf = jpeg_alloc_encoder_mem(encoder->input_size * 3 / 4, &output_mem_cfg, &allocated_size);
    if (encoder->out_buf == NULL) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "allocate JPEG output buffer failed");
        jpeg_del_encoder_engine(encoder->handle);
        encoder->handle = NULL;
        return ret;
    }
    encoder->out_size = (uint32_t)allocated_size;

    ESP_LOGI(TAG,
             "JPEG encoder ready: input_format=" V4L2_FMT_STR " quality=%d input=%" PRIu32 " output=%" PRIu32,
             V4L2_FMT_STR_ARG(pixelformat),
             quality,
             encoder->input_size,
             encoder->out_size);
    return ESP_OK;
}

static void jpeg_encoder_deinit(jpeg_encoder_ctx_t *encoder)
{
    if (encoder->out_buf != NULL) {
        free(encoder->out_buf);
        encoder->out_buf = NULL;
    }
    if (encoder->handle != NULL) {
        esp_err_t err = jpeg_del_encoder_engine(encoder->handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Delete JPEG encoder failed: %s", esp_err_to_name(err));
        }
        encoder->handle = NULL;
    }
}

static esp_err_t select_capture_format(int fd, struct v4l2_format *format)
{
    static const uint32_t preferred_formats[] = {
        V4L2_PIX_FMT_JPEG,
        V4L2_PIX_FMT_RGB565,
        V4L2_PIX_FMT_UYVY,
        V4L2_PIX_FMT_RGB24,
        V4L2_PIX_FMT_GREY,
#if CONFIG_ESP32P4_REV_MIN_FULL >= 300
        V4L2_PIX_FMT_YUV420,
#endif
    };
    struct v4l2_format current = {0};

    current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_ERROR(ioctl(fd, VIDIOC_G_FMT, &current), TAG, "get initial video format failed");

    for (size_t i = 0; i < ARRAY_SIZE(preferred_formats); ++i) {
        struct v4l2_format candidate = current;
        candidate.fmt.pix.pixelformat = preferred_formats[i];

        if (preferred_formats[i] != V4L2_PIX_FMT_JPEG && !jpeg_encoder_supports_v4l2(preferred_formats[i])) {
            continue;
        }

        if (ioctl(fd, VIDIOC_S_FMT, &candidate) == 0) {
            ESP_RETURN_ON_ERROR(ioctl(fd, VIDIOC_G_FMT, &candidate), TAG, "get selected video format failed");
            if (candidate.fmt.pix.pixelformat == preferred_formats[i]) {
                *format = candidate;
                ESP_LOGI(TAG,
                         "Selected capture format " V4L2_FMT_STR " %" PRIu32 "x%" PRIu32,
                         V4L2_FMT_STR_ARG(format->fmt.pix.pixelformat),
                         format->fmt.pix.width,
                         format->fmt.pix.height);
                return ESP_OK;
            }
        }
    }

    if (current.fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG || jpeg_encoder_supports_v4l2(current.fmt.pix.pixelformat)) {
        *format = current;
        ESP_LOGW(TAG,
                 "Using initial capture format " V4L2_FMT_STR " %" PRIu32 "x%" PRIu32,
                 V4L2_FMT_STR_ARG(format->fmt.pix.pixelformat),
                 format->fmt.pix.width,
                 format->fmt.pix.height);
        return ESP_OK;
    }

    ESP_LOGE(TAG,
             "No supported capture/JPEG encoder format found, initial format was " V4L2_FMT_STR,
             V4L2_FMT_STR_ARG(current.fmt.pix.pixelformat));
    return ESP_ERR_NOT_SUPPORTED;
}

static void log_video_capability(int fd)
{
    struct v4l2_capability capability = {0};

    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        ESP_LOGW(TAG, "Query video capability failed");
        return;
    }

    ESP_LOGI(TAG, "video driver=%s card=%s bus=%s", capability.driver, capability.card, capability.bus_info);
}

static void log_camera_chip_id(int fd)
{
    esp_cam_sensor_id_t chip_id = {0};
    struct v4l2_ext_control control[1] = {0};
    struct v4l2_ext_controls controls = {
        .ctrl_class = V4L2_CTRL_CLASS_ESP_CAM_IOCTL,
        .count = 1,
        .controls = control,
    };

    control[0].id = ESP_CAM_SENSOR_IOC_G_CHIP_ID;
    control[0].p_u8 = (uint8_t *)&chip_id;
    control[0].size = sizeof(chip_id);
    if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &controls) == 0) {
        ESP_LOGI(TAG, "camera chip id: pid=0x%" PRIx16, chip_id.pid);
    } else {
        ESP_LOGW(TAG, "Get camera chip id failed");
    }
}

static esp_err_t camera_stream_start(camera_stream_ctx_t *video)
{
    esp_err_t ret = ESP_OK;
    struct v4l2_format format = {0};
    struct v4l2_streamparm sparm = {0};
    struct v4l2_requestbuffers req = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    video->fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    ESP_RETURN_ON_FALSE(video->fd >= 0, ESP_FAIL, TAG, "open %s failed, errno=%d", ESP_VIDEO_MIPI_CSI_DEVICE_NAME, errno);

    log_video_capability(video->fd);
    log_camera_chip_id(video->fd);

    ESP_GOTO_ON_ERROR(select_capture_format(video->fd, &format), fail, TAG, "select capture format failed");

    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video->fd, VIDIOC_G_PARM, &sparm) == 0 &&
            sparm.parm.capture.timeperframe.numerator != 0 &&
            sparm.parm.capture.timeperframe.denominator != 0) {
        video->frame_rate = sparm.parm.capture.timeperframe.denominator / sparm.parm.capture.timeperframe.numerator;
    }
    if (video->frame_rate == 0) {
        video->frame_rate = 25;
    }

    req.count = VIDEO_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_REQBUFS, &req), fail, TAG, "request video buffers failed");
    ESP_GOTO_ON_FALSE(req.count >= VIDEO_BUFFER_COUNT, ESP_ERR_NO_MEM, fail, TAG, "driver returned too few video buffers");

    for (int i = 0; i < VIDEO_BUFFER_COUNT; ++i) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };

        ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_QUERYBUF, &buf), fail, TAG, "query video buffer failed");
        video->buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video->fd, buf.m.offset);
        ESP_GOTO_ON_FALSE(video->buffer[i] != MAP_FAILED, ESP_ERR_NO_MEM, fail, TAG, "mmap video buffer failed");
        video->buffer_len[i] = buf.length;
        ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_QBUF, &buf), fail, TAG, "queue video buffer failed");
    }

    video->width = format.fmt.pix.width;
    video->height = format.fmt.pix.height;
    video->pixel_format = format.fmt.pix.pixelformat;

    if (video->pixel_format != V4L2_PIX_FMT_JPEG) {
        ESP_GOTO_ON_ERROR(jpeg_encoder_init(&video->encoder,
                                            video->pixel_format,
                                            video->width,
                                            video->height,
                                            CONFIG_CAMERA_WIFI_STREAM_JPEG_QUALITY),
                          fail,
                          TAG,
                          "JPEG encoder init failed");
    }

    video->stream_lock = xSemaphoreCreateBinary();
    ESP_GOTO_ON_FALSE(video->stream_lock != NULL, ESP_ERR_NO_MEM, fail, TAG, "create stream semaphore failed");
    xSemaphoreGive(video->stream_lock);

    ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_STREAMON, &type), fail, TAG, "start video stream failed");
    video->streaming = true;

    ESP_LOGI(TAG,
             "video0: width=%" PRIu32 " height=%" PRIu32 " format=" V4L2_FMT_STR " fps=%" PRIu32,
             video->width,
             video->height,
             V4L2_FMT_STR_ARG(video->pixel_format),
             video->frame_rate);
    return ESP_OK;

fail:
    camera_stream_stop(video);
    return ret;
}

static void camera_stream_stop(camera_stream_ctx_t *video)
{
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (video->streaming && video->fd >= 0) {
        ioctl(video->fd, VIDIOC_STREAMOFF, &type);
        video->streaming = false;
    }

    for (int i = 0; i < VIDEO_BUFFER_COUNT; ++i) {
        if (video->buffer[i] != NULL && video->buffer[i] != MAP_FAILED) {
            munmap(video->buffer[i], video->buffer_len[i]);
        }
        video->buffer[i] = NULL;
        video->buffer_len[i] = 0;
    }

    jpeg_encoder_deinit(&video->encoder);

    if (video->stream_lock != NULL) {
        vSemaphoreDelete(video->stream_lock);
        video->stream_lock = NULL;
    }

    if (video->fd >= 0) {
        close(video->fd);
        video->fd = -1;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d", disconnected->reason);
        if (s_wifi_retry_num < CONFIG_CAMERA_WIFI_STREAM_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGI(TAG, "Retry WiFi connection (%d/%d)", s_wifi_retry_num, CONFIG_CAMERA_WIFI_STREAM_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_ip_info = event->ip_info;
        s_wifi_retry_num = 0;
        ESP_LOGI(TAG, "WiFi connected, IPv4=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    if (strlen(CONFIG_CAMERA_WIFI_STREAM_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "WiFi SSID is empty; set it in menuconfig before flashing");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "create WiFi event group failed");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta_netif != NULL, ESP_ERR_NO_MEM, TAG, "create default WiFi STA failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL),
                        TAG,
                        "register WiFi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL),
                        TAG,
                        "register IP event handler failed");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_CAMERA_WIFI_STREAM_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_CAMERA_WIFI_STREAM_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set WiFi STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set WiFi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start WiFi failed");

#if !CONFIG_CAMERA_WIFI_STREAM_ENABLE_WIFI_PS
    esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi power save disabled for lower stream latency");
    } else {
        ESP_LOGW(TAG, "Disable WiFi power save failed: %s", esp_err_to_name(ps_ret));
    }
#endif

    ESP_LOGI(TAG, "Connecting to WiFi SSID \"%s\" via ESP32-C6 hosted link", CONFIG_CAMERA_WIFI_STREAM_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Open http://" IPSTR "/ or http://" IPSTR "/stream", IP2STR(&s_ip_info.ip), IP2STR(&s_ip_info.ip));
        return ESP_OK;
    }

    ESP_LOGE(TAG, "WiFi connection failed after %d retry/retries", CONFIG_CAMERA_WIFI_STREAM_MAXIMUM_RETRY);
    return ESP_FAIL;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Camera Stream</title></head><body style=\"margin:0;background:#111;display:grid;place-items:center;min-height:100vh\">"
        "<img src=\"/stream\" style=\"max-width:100%;height:auto\">"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_stream_ctx_t *video = (camera_stream_ctx_t *)req->user_ctx;
    esp_err_t ret = ESP_OK;
    char part_header[96];
    char fps_header[16];
    uint32_t source_fps = video->frame_rate;
    uint32_t target_fps = CONFIG_CAMERA_WIFI_STREAM_MAX_STREAM_FPS;
    uint32_t frame_credit;
    uint32_t frames_sent = 0;
    uint32_t frames_skipped = 0;

    if (source_fps == 0) {
        source_fps = 25;
    }
    if (target_fps == 0 || target_fps > source_fps) {
        target_fps = source_fps;
    }
    frame_credit = source_fps;

    if (xSemaphoreTake(video->stream_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "stream busy");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MJPEG stream client connected");
    ESP_GOTO_ON_ERROR(httpd_resp_set_type(req, STREAM_CONTENT_TYPE), done, TAG, "set stream content type failed");
    ESP_GOTO_ON_ERROR(httpd_resp_set_hdr(req, "Cache-Control", "no-cache"), done, TAG, "set cache header failed");
    ESP_GOTO_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"), done, TAG, "set CORS header failed");
    ESP_GOTO_ON_FALSE(snprintf(fps_header, sizeof(fps_header), "%" PRIu32, target_fps) > 0,
                      ESP_FAIL,
                      done,
                      TAG,
                      "format FPS header failed");
    ESP_GOTO_ON_ERROR(httpd_resp_set_hdr(req, "X-Framerate", fps_header), done, TAG, "set framerate header failed");
    if (target_fps < source_fps) {
        ESP_LOGI(TAG, "Limiting MJPEG output to %" PRIu32 " fps from %" PRIu32 " fps capture", target_fps, source_fps);
    }

    while (true) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        uint8_t *jpeg_data = NULL;
        uint32_t jpeg_size = 0;
        bool have_buffer = false;

        ret = ioctl(video->fd, VIDIOC_DQBUF, &buf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "dequeue video frame failed");
            ret = ESP_FAIL;
            break;
        }
        have_buffer = true;

        if (!(buf.flags & V4L2_BUF_FLAG_DONE)) {
            ESP_LOGW(TAG, "skip incomplete video frame");
            ioctl(video->fd, VIDIOC_QBUF, &buf);
            have_buffer = false;
            continue;
        }

        if (!stream_rate_limiter_should_send(source_fps, target_fps, &frame_credit)) {
            frames_skipped++;
            goto frame_done;
        }

        if (video->pixel_format == V4L2_PIX_FMT_JPEG) {
            jpeg_data = video->buffer[buf.index];
            jpeg_size = buf.bytesused;
        } else {
            ret = jpeg_encoder_process(video->encoder.handle,
                                       &video->encoder.config,
                                       video->buffer[buf.index],
                                       video->encoder.input_size,
                                       video->encoder.out_buf,
                                       video->encoder.out_size,
                                       &jpeg_size);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "encode JPEG frame failed: %s", esp_err_to_name(ret));
                goto frame_done;
            }
            jpeg_data = video->encoder.out_buf;
        }

        ret = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (ret != ESP_OK) {
            goto frame_done;
        }

        int header_len = snprintf(part_header, sizeof(part_header), STREAM_PART, jpeg_size);
        if (header_len <= 0 || header_len >= (int)sizeof(part_header)) {
            ret = ESP_FAIL;
            goto frame_done;
        }

        ret = httpd_resp_send_chunk(req, part_header, header_len);
        if (ret != ESP_OK) {
            goto frame_done;
        }

        ret = httpd_resp_send_chunk(req, (const char *)jpeg_data, jpeg_size);
        if (ret == ESP_OK) {
            frames_sent++;
        }

frame_done:
        if (have_buffer) {
            esp_err_t queue_ret = ioctl(video->fd, VIDIOC_QBUF, &buf);
            if (queue_ret != ESP_OK) {
                ESP_LOGE(TAG, "queue video frame failed");
                ret = ESP_FAIL;
            }
        }

        if (ret != ESP_OK) {
            break;
        }
    }

done:
    xSemaphoreGive(video->stream_lock);
    ESP_LOGI(TAG,
             "MJPEG stream client disconnected, sent=%" PRIu32 " skipped=%" PRIu32,
             frames_sent,
             frames_skipped);
    return ret;
}

static esp_err_t http_server_start(camera_stream_ctx_t *video)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = video,
    };
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = video,
    };

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "start HTTP server failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root_uri), TAG, "register / handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &stream_uri), TAG, "register /stream handler failed");
    ESP_LOGI(TAG, "HTTP server ready");
    return ESP_OK;
}

static esp_err_t app_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 camera WiFi MJPEG stream example");
    ESP_LOGI(TAG, "Hosted WiFi expects ESP32-C6 slave firmware on the onboard module");

    ESP_ERROR_CHECK(app_nvs_init());
    ESP_ERROR_CHECK(camera_enable_power());
    ESP_ERROR_CHECK(camera_video_system_init());
    ESP_ERROR_CHECK(camera_stream_start(&s_camera));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_ERROR_CHECK(http_server_start(&s_camera));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    camera_stream_stop(&s_camera);
    esp_video_deinit();
    camera_stop_xclk();
}
