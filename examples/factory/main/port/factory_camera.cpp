#include "factory_camera.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

#include "factory_display.h"

#ifndef CONFIG_FACTORY_CAMERA_ENABLE
#define CONFIG_FACTORY_CAMERA_ENABLE 1
#endif

#ifndef CONFIG_FACTORY_CAMERA_I2C_FREQ_HZ
#define CONFIG_FACTORY_CAMERA_I2C_FREQ_HZ 100000
#endif

#ifndef CONFIG_FACTORY_CAMERA_ENABLE_POWER
#define CONFIG_FACTORY_CAMERA_ENABLE_POWER 1
#endif

#ifndef CONFIG_FACTORY_CAMERA_DVDD1_MV
#define CONFIG_FACTORY_CAMERA_DVDD1_MV 0
#endif

#ifndef CONFIG_FACTORY_CAMERA_DVDD2_MV
#define CONFIG_FACTORY_CAMERA_DVDD2_MV 0
#endif

#ifndef CONFIG_FACTORY_CAMERA_AVDD1_MV
#define CONFIG_FACTORY_CAMERA_AVDD1_MV 1800
#endif

#ifndef CONFIG_FACTORY_CAMERA_AVDD2_MV
#define CONFIG_FACTORY_CAMERA_AVDD2_MV 2800
#endif

#ifndef CONFIG_FACTORY_CAMERA_RESET_GPIO
#define CONFIG_FACTORY_CAMERA_RESET_GPIO -1
#endif

#ifndef CONFIG_FACTORY_CAMERA_PWDN_GPIO
#define CONFIG_FACTORY_CAMERA_PWDN_GPIO -1
#endif

#ifndef CONFIG_FACTORY_CAMERA_XCLK_GPIO
#define CONFIG_FACTORY_CAMERA_XCLK_GPIO -1
#endif

#ifndef CONFIG_FACTORY_CAMERA_XCLK_FREQ_HZ
#define CONFIG_FACTORY_CAMERA_XCLK_FREQ_HZ 24000000
#endif

#ifndef CONFIG_FACTORY_CAMERA_CAPTURE_WIDTH
#define CONFIG_FACTORY_CAMERA_CAPTURE_WIDTH 1280
#endif

#ifndef CONFIG_FACTORY_CAMERA_CAPTURE_HEIGHT
#define CONFIG_FACTORY_CAMERA_CAPTURE_HEIGHT 720
#endif

#ifndef CONFIG_FACTORY_CAMERA_VIDEO_BUFFER_COUNT
#define CONFIG_FACTORY_CAMERA_VIDEO_BUFFER_COUNT 3
#endif

namespace {

static const char *TAG = "factory_camera";

constexpr uint8_t SGM38121_I2C_ADDR = 0x28;
constexpr uint8_t SGM38121_REG_CHIP_REV = 0x00;
constexpr uint8_t SGM38121_REG_DISCH = 0x02;
constexpr uint8_t SGM38121_REG_DVDD1_VOUT = 0x03;
constexpr uint8_t SGM38121_REG_DVDD2_VOUT = 0x04;
constexpr uint8_t SGM38121_REG_AVDD1_VOUT = 0x05;
constexpr uint8_t SGM38121_REG_AVDD2_VOUT = 0x06;
constexpr uint8_t SGM38121_REG_FUNCTION = 0x07;
constexpr uint8_t SGM38121_REG_SEQ_DVDD = 0x0A;
constexpr uint8_t SGM38121_REG_SEQ_AVDD = 0x0B;
constexpr uint8_t SGM38121_REG_ENABLE = 0x0E;
constexpr uint8_t SGM38121_ENABLE_DVDD1_BIT = BIT0;
constexpr uint8_t SGM38121_ENABLE_DVDD2_BIT = BIT1;
constexpr uint8_t SGM38121_ENABLE_AVDD1_BIT = BIT2;
constexpr uint8_t SGM38121_ENABLE_AVDD2_BIT = BIT3;

constexpr uint8_t CAMERA_SCCB_ADDR_SC2336 = 0x30;
constexpr uint8_t CAMERA_SCCB_ADDR_OV2710 = 0x36;
constexpr uint8_t CAMERA_SCCB_ADDR_OV5645 = 0x3C;

constexpr int kI2cTimeoutMs = 100;
constexpr int kCameraPowerSettleMs = 20;
constexpr size_t kVideoBufferCount = CONFIG_FACTORY_CAMERA_VIDEO_BUFFER_COUNT;

struct CameraContext {
    int fd;
    bool initialized;
    bool video_initialized;
    bool streaming;
    void *buffer[kVideoBufferCount];
    size_t buffer_len[kVideoBufferCount];
    factory_camera_frame_info_t frame;
};

static CameraContext s_camera = {};
static bool s_context_ready = false;

#if CONFIG_FACTORY_CAMERA_XCLK_GPIO >= 0
static esp_cam_sensor_xclk_handle_t s_xclk_handle = nullptr;
#endif

static void ensure_context()
{
    if (s_context_ready) {
        return;
    }

    memset(&s_camera, 0, sizeof(s_camera));
    s_camera.fd = -1;
    s_context_ready = true;
}

static bool check_esp(esp_err_t err, const char *operation)
{
    if (err == ESP_OK) {
        return true;
    }

    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    return false;
}

static bool check_ioctl(int ret, const char *operation)
{
    if (ret == 0) {
        return true;
    }

    ESP_LOGE(TAG, "%s failed: errno=%d (%s)", operation, errno, strerror(errno));
    return false;
}

static uint8_t sgm_encode_vout_mv_rounded(
    int target_mv,
    int min_mv,
    int max_mv,
    int offset_mv,
    int min_reg,
    int max_reg,
    int *actual_mv)
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

    if (actual_mv != nullptr) {
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

static esp_err_t add_sgm_device(i2c_master_dev_handle_t *out_dev)
{
    i2c_master_bus_handle_t bus = factory_display_get_i2c_bus();
    if (bus == nullptr || out_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_master_probe(bus, SGM38121_I2C_ADDR, kI2cTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = SGM38121_I2C_ADDR;
    dev_cfg.scl_speed_hz = CONFIG_FACTORY_CAMERA_I2C_FREQ_HZ;
    return i2c_master_bus_add_device(bus, &dev_cfg, out_dev);
}

static esp_err_t sgm_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, sizeof(reg), value, 1, kI2cTimeoutMs);
}

static esp_err_t sgm_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(dev, payload, sizeof(payload), kI2cTimeoutMs);
}

static bool is_supported_pixel_format(uint32_t pixel_format)
{
    switch (pixel_format) {
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_RGB24:
        case V4L2_PIX_FMT_GREY:
            return true;
        default:
            return false;
    }
}

static bool select_capture_format(int fd, struct v4l2_format *out_format)
{
    static const uint32_t preferred_formats[] = {
        V4L2_PIX_FMT_RGB565,
        V4L2_PIX_FMT_UYVY,
        V4L2_PIX_FMT_YUYV,
        V4L2_PIX_FMT_RGB24,
        V4L2_PIX_FMT_GREY,
    };

    struct v4l2_format current = {};
    current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!check_ioctl(ioctl(fd, VIDIOC_G_FMT, &current), "get initial video format")) {
        return false;
    }

    for (size_t i = 0; i < sizeof(preferred_formats) / sizeof(preferred_formats[0]); ++i) {
        struct v4l2_format candidate = current;
        candidate.fmt.pix.width = CONFIG_FACTORY_CAMERA_CAPTURE_WIDTH;
        candidate.fmt.pix.height = CONFIG_FACTORY_CAMERA_CAPTURE_HEIGHT;
        candidate.fmt.pix.pixelformat = preferred_formats[i];

        if (ioctl(fd, VIDIOC_S_FMT, &candidate) != 0) {
            continue;
        }
        if (!check_ioctl(ioctl(fd, VIDIOC_G_FMT, &candidate), "get selected video format")) {
            return false;
        }
        if (is_supported_pixel_format(candidate.fmt.pix.pixelformat)) {
            *out_format = candidate;
            ESP_LOGI(TAG,
                     "selected capture format %s %" PRIu32 "x%" PRIu32,
                     factory_camera_pixel_format_name(candidate.fmt.pix.pixelformat),
                     candidate.fmt.pix.width,
                     candidate.fmt.pix.height);
            return true;
        }
    }

    if (is_supported_pixel_format(current.fmt.pix.pixelformat)) {
        *out_format = current;
        ESP_LOGW(TAG,
                 "using initial capture format %s %" PRIu32 "x%" PRIu32,
                 factory_camera_pixel_format_name(current.fmt.pix.pixelformat),
                 current.fmt.pix.width,
                 current.fmt.pix.height);
        return true;
    }

    ESP_LOGE(TAG, "no supported capture format found");
    return false;
}

static bool start_xclk()
{
#if CONFIG_FACTORY_CAMERA_XCLK_GPIO >= 0
    if (s_xclk_handle != nullptr) {
        return true;
    }

    esp_cam_sensor_xclk_config_t xclk_config = {};
    xclk_config.esp_clock_router_cfg.xclk_pin = CONFIG_FACTORY_CAMERA_XCLK_GPIO;
    xclk_config.esp_clock_router_cfg.xclk_freq_hz = CONFIG_FACTORY_CAMERA_XCLK_FREQ_HZ;

    if (!check_esp(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle),
                   "allocate camera XCLK")) {
        return false;
    }
    if (!check_esp(esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_config), "start camera XCLK")) {
        esp_cam_sensor_xclk_free(s_xclk_handle);
        s_xclk_handle = nullptr;
        return false;
    }
#endif
    return true;
}

static void stop_xclk()
{
#if CONFIG_FACTORY_CAMERA_XCLK_GPIO >= 0
    if (s_xclk_handle == nullptr) {
        return;
    }

    esp_err_t err = esp_cam_sensor_xclk_stop(s_xclk_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "stop XCLK failed: %s", esp_err_to_name(err));
    }
    err = esp_cam_sensor_xclk_free(s_xclk_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "free XCLK failed: %s", esp_err_to_name(err));
    }
    s_xclk_handle = nullptr;
#endif
}

static void log_video_capability(int fd)
{
    struct v4l2_capability capability = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        ESP_LOGW(TAG, "query video capability failed");
        return;
    }

    ESP_LOGI(TAG, "video driver=%s card=%s bus=%s", capability.driver, capability.card, capability.bus_info);
}

}  // namespace

extern "C" bool factory_camera_power_on(void)
{
#if !CONFIG_FACTORY_CAMERA_ENABLE || !CONFIG_FACTORY_CAMERA_ENABLE_POWER
    return true;
#else
    i2c_master_dev_handle_t sgm = nullptr;
    uint8_t chip_rev = 0;
    int avdd1_actual_mv = 0;
    int avdd2_actual_mv = 0;
    int dvdd1_actual_mv = 0;
    int dvdd2_actual_mv = 0;
    const uint8_t avdd1_reg = sgm_encode_avdd_mv_rounded(CONFIG_FACTORY_CAMERA_AVDD1_MV, &avdd1_actual_mv);
    const uint8_t avdd2_reg = sgm_encode_avdd_mv_rounded(CONFIG_FACTORY_CAMERA_AVDD2_MV, &avdd2_actual_mv);
    uint8_t enable_mask = SGM38121_ENABLE_AVDD1_BIT | SGM38121_ENABLE_AVDD2_BIT;

    if (!check_esp(add_sgm_device(&sgm), "add SGM38121 camera power device")) {
        return false;
    }

    if (sgm_read_reg(sgm, SGM38121_REG_CHIP_REV, &chip_rev) == ESP_OK) {
        ESP_LOGI(TAG, "SGM38121 CHIP_REV=0x%02X", chip_rev);
    }

    ESP_LOGI(TAG,
             "enable camera rails: AVDD1=%d mV AVDD2=%d mV",
             avdd1_actual_mv,
             avdd2_actual_mv);

    bool ok = true;
    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_SEQ_DVDD, 0x00), "set SGM38121 DVDD sequence");
    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_SEQ_AVDD, 0x00), "set SGM38121 AVDD sequence");
    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_FUNCTION, 0x00), "disable SGM38121 wake-up");
    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_DISCH, 0x00), "set SGM38121 discharge mode");

    if (CONFIG_FACTORY_CAMERA_DVDD1_MV > 0) {
        const uint8_t dvdd1_reg = sgm_encode_dvdd_mv_rounded(CONFIG_FACTORY_CAMERA_DVDD1_MV, &dvdd1_actual_mv);
        enable_mask |= SGM38121_ENABLE_DVDD1_BIT;
        ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_DVDD1_VOUT, dvdd1_reg), "write SGM38121 DVDD1");
    }
    if (CONFIG_FACTORY_CAMERA_DVDD2_MV > 0) {
        const uint8_t dvdd2_reg = sgm_encode_dvdd_mv_rounded(CONFIG_FACTORY_CAMERA_DVDD2_MV, &dvdd2_actual_mv);
        enable_mask |= SGM38121_ENABLE_DVDD2_BIT;
        ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_DVDD2_VOUT, dvdd2_reg), "write SGM38121 DVDD2");
    }

    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_AVDD1_VOUT, avdd1_reg), "write SGM38121 AVDD1");
    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_AVDD2_VOUT, avdd2_reg), "write SGM38121 AVDD2");
    ok = ok && check_esp(sgm_write_reg(sgm, SGM38121_REG_ENABLE, enable_mask), "enable SGM38121 camera rails");

    esp_err_t rm_err = i2c_master_bus_rm_device(sgm);
    if (rm_err != ESP_OK) {
        ESP_LOGW(TAG, "remove SGM38121 device failed: %s", esp_err_to_name(rm_err));
    }

    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(kCameraPowerSettleMs));
    }
    return ok;
#endif
}

extern "C" void factory_camera_power_off(void)
{
#if CONFIG_FACTORY_CAMERA_ENABLE && CONFIG_FACTORY_CAMERA_ENABLE_POWER
    i2c_master_dev_handle_t sgm = nullptr;
    if (add_sgm_device(&sgm) != ESP_OK) {
        return;
    }

    esp_err_t err = sgm_write_reg(sgm, SGM38121_REG_ENABLE, 0x00);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "disable camera rails failed: %s", esp_err_to_name(err));
    }
    err = i2c_master_bus_rm_device(sgm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "remove SGM38121 device failed: %s", esp_err_to_name(err));
    }
#endif
}

extern "C" bool factory_camera_init(void)
{
#if !CONFIG_FACTORY_CAMERA_ENABLE
    return false;
#else
    ensure_context();
    if (s_camera.initialized) {
        return true;
    }

    i2c_master_bus_handle_t shared_i2c = factory_display_get_i2c_bus();
    if (shared_i2c == nullptr) {
        ESP_LOGE(TAG, "shared I2C bus is not ready");
        return false;
    }
    if (!start_xclk()) {
        return false;
    }

    esp_video_init_csi_config_t csi_config = {};
    csi_config.sccb_config.init_sccb = false;
    csi_config.sccb_config.i2c_handle = shared_i2c;
    csi_config.sccb_config.freq = CONFIG_FACTORY_CAMERA_I2C_FREQ_HZ;
    csi_config.reset_pin = (gpio_num_t)CONFIG_FACTORY_CAMERA_RESET_GPIO;
    csi_config.pwdn_pin = (gpio_num_t)CONFIG_FACTORY_CAMERA_PWDN_GPIO;
    csi_config.dont_init_ldo = false;

    esp_video_init_config_t video_config = {};
    video_config.csi = &csi_config;

    if (!check_esp(esp_video_init(&video_config), "initialize ESP video")) {
        stop_xclk();
        return false;
    }
    s_camera.video_initialized = true;

    s_camera.fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    if (s_camera.fd < 0) {
        ESP_LOGE(TAG, "open %s failed: errno=%d (%s)", ESP_VIDEO_MIPI_CSI_DEVICE_NAME, errno, strerror(errno));
        factory_camera_deinit();
        return false;
    }

    log_video_capability(s_camera.fd);

    struct v4l2_format format = {};
    if (!select_capture_format(s_camera.fd, &format)) {
        factory_camera_deinit();
        return false;
    }

    struct v4l2_streamparm sparm = {};
    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_camera.fd, VIDIOC_G_PARM, &sparm) == 0 &&
        sparm.parm.capture.timeperframe.numerator != 0 &&
        sparm.parm.capture.timeperframe.denominator != 0) {
        s_camera.frame.frame_rate =
            sparm.parm.capture.timeperframe.denominator / sparm.parm.capture.timeperframe.numerator;
    }
    if (s_camera.frame.frame_rate == 0) {
        s_camera.frame.frame_rate = 25;
    }

    struct v4l2_requestbuffers req = {};
    req.count = kVideoBufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (!check_ioctl(ioctl(s_camera.fd, VIDIOC_REQBUFS, &req), "request video buffers")) {
        factory_camera_deinit();
        return false;
    }
    if (req.count < kVideoBufferCount) {
        ESP_LOGE(TAG, "driver returned too few buffers: %u", (unsigned)req.count);
        factory_camera_deinit();
        return false;
    }

    for (size_t i = 0; i < kVideoBufferCount; ++i) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (!check_ioctl(ioctl(s_camera.fd, VIDIOC_QUERYBUF, &buf), "query video buffer")) {
            factory_camera_deinit();
            return false;
        }

        s_camera.buffer[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_camera.fd, buf.m.offset);
        if (s_camera.buffer[i] == MAP_FAILED) {
            s_camera.buffer[i] = nullptr;
            ESP_LOGE(TAG, "mmap video buffer failed");
            factory_camera_deinit();
            return false;
        }
        s_camera.buffer_len[i] = buf.length;

        if (!check_ioctl(ioctl(s_camera.fd, VIDIOC_QBUF, &buf), "queue video buffer")) {
            factory_camera_deinit();
            return false;
        }
    }

    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!check_ioctl(ioctl(s_camera.fd, VIDIOC_STREAMON, &type), "start video stream")) {
        factory_camera_deinit();
        return false;
    }

    s_camera.streaming = true;
    s_camera.frame.width = format.fmt.pix.width;
    s_camera.frame.height = format.fmt.pix.height;
    s_camera.frame.pixel_format = format.fmt.pix.pixelformat;
    s_camera.frame.bytes_per_frame =
        factory_camera_frame_size(s_camera.frame.width, s_camera.frame.height, s_camera.frame.pixel_format);
    s_camera.initialized = true;

    ESP_LOGI(TAG,
             "camera ready: %" PRIu32 "x%" PRIu32 " %s, frame=%" PRIu32 " bytes, fps=%" PRIu32,
             s_camera.frame.width,
             s_camera.frame.height,
             factory_camera_pixel_format_name(s_camera.frame.pixel_format),
             s_camera.frame.bytes_per_frame,
             s_camera.frame.frame_rate);
    return true;
#endif
}

extern "C" void factory_camera_deinit(void)
{
    ensure_context();

    if (s_camera.streaming && s_camera.fd >= 0) {
        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(s_camera.fd, VIDIOC_STREAMOFF, &type) != 0) {
            ESP_LOGW(TAG, "stream off failed: errno=%d (%s)", errno, strerror(errno));
        }
    }
    s_camera.streaming = false;

    for (size_t i = 0; i < kVideoBufferCount; ++i) {
        if (s_camera.buffer[i] != nullptr && s_camera.buffer[i] != MAP_FAILED) {
            munmap(s_camera.buffer[i], s_camera.buffer_len[i]);
        }
        s_camera.buffer[i] = nullptr;
        s_camera.buffer_len[i] = 0;
    }

    if (s_camera.fd >= 0) {
        close(s_camera.fd);
        s_camera.fd = -1;
    }

    if (s_camera.video_initialized) {
        esp_err_t err = esp_video_deinit();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_video_deinit failed: %s", esp_err_to_name(err));
        }
        s_camera.video_initialized = false;
    }

    stop_xclk();
    s_camera.initialized = false;
    memset(&s_camera.frame, 0, sizeof(s_camera.frame));
}

extern "C" bool factory_camera_capture(void *out_buf, size_t buf_size, uint32_t *out_len)
{
    ensure_context();
    if (!s_camera.initialized || s_camera.fd < 0 || out_buf == nullptr) {
        return false;
    }

    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (!check_ioctl(ioctl(s_camera.fd, VIDIOC_DQBUF, &buf), "dequeue video frame")) {
        return false;
    }

    bool ok = true;
    if (buf.index >= kVideoBufferCount || s_camera.buffer[buf.index] == nullptr) {
        ESP_LOGE(TAG, "invalid video buffer index %u", (unsigned)buf.index);
        ok = false;
    } else if (buf.bytesused > buf_size) {
        ESP_LOGE(TAG, "output buffer too small: need %u, have %u", (unsigned)buf.bytesused, (unsigned)buf_size);
        ok = false;
    } else if ((buf.flags & V4L2_BUF_FLAG_DONE) == 0) {
        ESP_LOGW(TAG, "captured buffer was not marked done");
        ok = false;
    } else {
        memcpy(out_buf, s_camera.buffer[buf.index], buf.bytesused);
        if (out_len != nullptr) {
            *out_len = buf.bytesused;
        }
    }

    if (!check_ioctl(ioctl(s_camera.fd, VIDIOC_QBUF, &buf), "queue video buffer")) {
        return false;
    }

    return ok;
}

extern "C" bool factory_camera_get_frame_info(factory_camera_frame_info_t *out_info)
{
    ensure_context();
    if (!s_camera.initialized || out_info == nullptr) {
        return false;
    }

    *out_info = s_camera.frame;
    return true;
}

extern "C" bool factory_camera_is_detected(void)
{
    i2c_master_bus_handle_t bus = factory_display_get_i2c_bus();
    if (bus == nullptr) {
        return false;
    }

    static const uint8_t addresses[] = {
        CAMERA_SCCB_ADDR_OV2710,
        CAMERA_SCCB_ADDR_SC2336,
        CAMERA_SCCB_ADDR_OV5645,
    };

    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        esp_err_t err = i2c_master_probe(bus, addresses[i], kI2cTimeoutMs);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "detected camera ACK at SCCB address 0x%02X", addresses[i]);
            return true;
        }
    }

    ESP_LOGW(TAG, "no known camera SCCB address responded");
    return false;
}

extern "C" uint32_t factory_camera_frame_size(uint32_t width, uint32_t height, uint32_t pixel_format)
{
    switch (pixel_format) {
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
            return width * height * 2U;
        case V4L2_PIX_FMT_RGB24:
            return width * height * 3U;
        case V4L2_PIX_FMT_GREY:
            return width * height;
        default:
            return 0;
    }
}

extern "C" const char *factory_camera_pixel_format_name(uint32_t pixel_format)
{
    switch (pixel_format) {
        case V4L2_PIX_FMT_RGB565:
            return "RGB565";
        case V4L2_PIX_FMT_UYVY:
            return "UYVY";
        case V4L2_PIX_FMT_YUYV:
            return "YUYV";
        case V4L2_PIX_FMT_RGB24:
            return "RGB24";
        case V4L2_PIX_FMT_GREY:
            return "GREY";
        default:
            return "UNKNOWN";
    }
}
