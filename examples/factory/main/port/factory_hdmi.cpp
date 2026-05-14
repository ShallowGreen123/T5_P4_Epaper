#include "factory_hdmi.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_lt8912b.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "bsp/esp-bsp.h"
#include "board_config.h"

namespace {

static const char *TAG = "factory_hdmi";

constexpr uint16_t kHdmiWidth = 800;
constexpr uint16_t kHdmiHeight = 600;
constexpr uint8_t kBytesPerPixel = 3;
constexpr uint8_t kDsiLaneCount = 2;
constexpr uint32_t kDsiLaneBitrateMbps = 1000;
constexpr uint8_t kDpiFramebuffers = 2;
constexpr uint8_t kDsiPhyLdoChannel = 3;
constexpr uint16_t kDsiPhyLdoVoltageMv = 2500;
constexpr int kI2cTimeoutMs = 100;
constexpr TickType_t kHdmiReadyRetryDelay = pdMS_TO_TICKS(100);
constexpr int kHdmiReadyRetryCount = 10;
constexpr TickType_t kFlushTimeout = pdMS_TO_TICKS(1000);
constexpr TickType_t kStopWaitDelay = pdMS_TO_TICKS(20);
constexpr int kStopWaitLoops = 100;

static esp_lcd_dsi_bus_handle_t s_dsi_bus = nullptr;
static esp_lcd_panel_io_handle_t s_io_main = nullptr;
static esp_lcd_panel_io_handle_t s_io_cec = nullptr;
static esp_lcd_panel_io_handle_t s_io_avi = nullptr;
static esp_lcd_panel_handle_t s_panel = nullptr;
static esp_ldo_channel_handle_t s_dsi_ldo = nullptr;
static SemaphoreHandle_t s_flush_done = nullptr;
static TaskHandle_t s_render_task = nullptr;
static uint8_t *s_framebuffers[2] = {};
static volatile bool s_stop_requested = false;
static bool s_hdmi_int_gpio_configured = false;

static char s_last_error[96] = "None";
static factory_hdmi_state_t s_state = {
    .initialized = false,
    .powered = false,
    .ready = false,
    .running = false,
    .mode = FACTORY_HDMI_MODE_PATTERN,
    .width = kHdmiWidth,
    .height = kHdmiHeight,
    .frame_count = 0,
    .fps = 0,
    .free_psram = 0,
    .status_text = "Stopped",
    .last_error = s_last_error,
};

static uint8_t clamp_u8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (uint8_t)value;
}

static void refresh_free_psram()
{
    s_state.free_psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static void clear_error()
{
    snprintf(s_last_error, sizeof(s_last_error), "None");
    s_state.last_error = s_last_error;
}

static void set_error(const char *context, esp_err_t err)
{
    snprintf(s_last_error, sizeof(s_last_error), "%s: %s", context, esp_err_to_name(err));
    s_state.last_error = s_last_error;
    s_state.status_text = "Error";
    ESP_LOGE(TAG, "%s", s_last_error);
}

static i2c_master_bus_handle_t shared_i2c_bus()
{
    if (bsp_i2c_init() != ESP_OK) {
        return nullptr;
    }
    return bsp_i2c_get_handle();
}

static esp_err_t ensure_hdmi_int_gpio()
{
    if (s_hdmi_int_gpio_configured) {
        return ESP_OK;
    }

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << FACTORY_HDMI_INT_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "Configure HDMI INT GPIO failed");
    s_hdmi_int_gpio_configured = true;
    return ESP_OK;
}

static void log_i2c_probe(const char *name, uint8_t address)
{
    i2c_master_bus_handle_t bus = shared_i2c_bus();
    if (bus == nullptr) {
        return;
    }

    esp_err_t err = i2c_master_probe(bus, address, kI2cTimeoutMs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s detected at 0x%02X", name, address);
    } else {
        ESP_LOGW(TAG, "%s probe at 0x%02X failed: %s", name, address, esp_err_to_name(err));
    }
}

static esp_err_t hdmi_power_on()
{
    if (s_state.powered) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_hdmi_int_gpio(), TAG, "HDMI INT GPIO init failed");
    ESP_RETURN_ON_ERROR(t5_board_hdmi_power_on(), TAG, "HDMI power sequence failed");

    s_state.powered = true;
    log_i2c_probe("LT8912B main", FACTORY_HDMI_I2C_ADDR_LT8912B_MAIN);
    log_i2c_probe("LT8912B CEC", FACTORY_HDMI_I2C_ADDR_LT8912B_CEC);
    log_i2c_probe("LT8912B AVI", FACTORY_HDMI_I2C_ADDR_LT8912B_AVI);
    return ESP_OK;
}

static void hdmi_power_off()
{
    if (!s_state.powered) {
        s_state.powered = false;
        return;
    }

    if (t5_board_hdmi_power_off() != ESP_OK) {
        ESP_LOGW(TAG, "HDMI power off failed");
    }
    s_state.powered = false;
    s_state.ready = false;
}

static esp_err_t enable_dsi_phy_power()
{
    if (s_dsi_ldo != nullptr) {
        return ESP_OK;
    }

    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = kDsiPhyLdoChannel;
    ldo_cfg.voltage_mv = kDsiPhyLdoVoltageMv;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_ldo), TAG, "Acquire DSI PHY LDO failed");
    return ESP_OK;
}

static void disable_dsi_phy_power()
{
    if (s_dsi_ldo != nullptr) {
        esp_ldo_release_channel(s_dsi_ldo);
        s_dsi_ldo = nullptr;
    }
}

static void delete_panel_handles()
{
    if (s_panel != nullptr) {
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
    }
    if (s_io_main != nullptr) {
        esp_lcd_panel_io_del(s_io_main);
        s_io_main = nullptr;
    }
    if (s_io_cec != nullptr) {
        esp_lcd_panel_io_del(s_io_cec);
        s_io_cec = nullptr;
    }
    if (s_io_avi != nullptr) {
        esp_lcd_panel_io_del(s_io_avi);
        s_io_avi = nullptr;
    }
    if (s_dsi_bus != nullptr) {
        esp_lcd_del_dsi_bus(s_dsi_bus);
        s_dsi_bus = nullptr;
    }
    disable_dsi_phy_power();
}

static esp_err_t init_panel()
{
    if (s_panel != nullptr) {
        return ESP_OK;
    }

    esp_err_t ret = enable_dsi_phy_power();
    if (ret != ESP_OK) {
        set_error("Enable DSI PHY", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = kDsiLaneCount;
    bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps = kDsiLaneBitrateMbps;
    ret = esp_lcd_new_dsi_bus(&bus_config, &s_dsi_bus);
    if (ret != ESP_OK) {
        set_error("Create DSI bus", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    ret = hdmi_power_on();
    if (ret != ESP_OK) {
        set_error("HDMI power on", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = LT8912B_IO_I2C_MAIN_ADDRESS;
    io_config.control_phase_bytes = 1;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = FACTORY_I2C_FREQ_HZ;
    ret = esp_lcd_new_panel_io_i2c(shared_i2c_bus(), &io_config, &s_io_main);
    if (ret != ESP_OK) {
        set_error("Create LT8912B main IO", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    esp_lcd_panel_io_i2c_config_t io_cec_config = io_config;
    io_cec_config.dev_addr = LT8912B_IO_I2C_CEC_ADDRESS;
    ret = esp_lcd_new_panel_io_i2c(shared_i2c_bus(), &io_cec_config, &s_io_cec);
    if (ret != ESP_OK) {
        set_error("Create LT8912B CEC IO", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    esp_lcd_panel_io_i2c_config_t io_avi_config = io_config;
    io_avi_config.dev_addr = LT8912B_IO_I2C_AVI_ADDRESS;
    ret = esp_lcd_new_panel_io_i2c(shared_i2c_bus(), &io_avi_config, &s_io_avi);
    if (ret != ESP_OK) {
        set_error("Create LT8912B AVI IO", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    esp_lcd_dpi_panel_config_t dpi_config = {};
    dpi_config.virtual_channel = 0;
    dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz = 40;
    dpi_config.in_color_format = LCD_COLOR_FMT_RGB888;
    dpi_config.num_fbs = kDpiFramebuffers;
    dpi_config.video_timing.h_size = kHdmiWidth;
    dpi_config.video_timing.v_size = kHdmiHeight;
    dpi_config.video_timing.hsync_back_porch = 88;
    dpi_config.video_timing.hsync_pulse_width = 128;
    dpi_config.video_timing.hsync_front_porch = 48;
    dpi_config.video_timing.vsync_back_porch = 23;
    dpi_config.video_timing.vsync_pulse_width = 4;
    dpi_config.video_timing.vsync_front_porch = 1;
    dpi_config.flags.disable_lp = true;

    esp_lcd_panel_lt8912b_video_timing_t video_timing = {};
    video_timing.hfp = 48;
    video_timing.hs = 128;
    video_timing.hbp = 88;
    video_timing.hact = kHdmiWidth;
    video_timing.htotal = 1056;
    video_timing.vfp = 1;
    video_timing.vs = 4;
    video_timing.vbp = 23;
    video_timing.vact = kHdmiHeight;
    video_timing.vtotal = 628;
    video_timing.h_polarity = true;
    video_timing.v_polarity = true;
    video_timing.vic = 0;
    video_timing.aspect_ratio = LT8912B_ASPECT_RATION_16_9;
    video_timing.pclk_mhz = 40;

    lt8912b_vendor_config_t vendor_config = {};
    vendor_config.video_timing = video_timing;
    vendor_config.mipi_config.dsi_bus = s_dsi_bus;
    vendor_config.mipi_config.dpi_config = &dpi_config;
    vendor_config.mipi_config.lane_num = kDsiLaneCount;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 24;
    panel_config.vendor_config = &vendor_config;

    esp_lcd_panel_lt8912b_io_t io_all = {};
    io_all.main = s_io_main;
    io_all.cec_dsi = s_io_cec;
    io_all.avi = s_io_avi;
    ret = esp_lcd_new_panel_lt8912b(&io_all, &panel_config, &s_panel);
    if (ret != ESP_OK) {
        set_error("Create LT8912B panel", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }
    ret = esp_lcd_panel_reset(s_panel);
    if (ret != ESP_OK) {
        set_error("Reset LT8912B", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }
    ret = esp_lcd_panel_init(s_panel);
    if (ret != ESP_OK) {
        set_error("Init LT8912B", ret);
        delete_panel_handles();
        hdmi_power_off();
        return ret;
    }

    for (int attempt = 0; attempt < kHdmiReadyRetryCount; ++attempt) {
        s_state.ready = esp_lcd_panel_lt8912b_is_ready(s_panel);
        ESP_LOGI(TAG, "LT8912B ready check %d/%d: %s",
                 attempt + 1, kHdmiReadyRetryCount, s_state.ready ? "ready" : "not ready");
        if (s_state.ready) {
            break;
        }
        vTaskDelay(kHdmiReadyRetryDelay);
    }

    return ESP_OK;
}

static bool IRAM_ATTR flush_done_cb(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;

    BaseType_t high_task_woken = pdFALSE;
    if (s_flush_done != nullptr) {
        xSemaphoreGiveFromISR(s_flush_done, &high_task_woken);
    }
    return high_task_woken == pdTRUE;
}

static bool allocate_buffers()
{
    const size_t frame_size = (size_t)kHdmiWidth * kHdmiHeight * kBytesPerPixel;
    size_t alignment = 64;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment) != ESP_OK || alignment == 0) {
        alignment = 64;
    }

    for (size_t i = 0; i < 2; ++i) {
        if (s_framebuffers[i] == nullptr) {
            s_framebuffers[i] = (uint8_t *)heap_caps_aligned_calloc(
                alignment, 1, frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (s_framebuffers[i] == nullptr) {
            snprintf(s_last_error, sizeof(s_last_error), "Allocate framebuffer %u failed", (unsigned)i);
            s_state.last_error = s_last_error;
            s_state.status_text = "No memory";
            return false;
        }
    }

    refresh_free_psram();
    return true;
}

static void free_buffers()
{
    for (uint8_t *&buffer : s_framebuffers) {
        if (buffer != nullptr) {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
    }
    refresh_free_psram();
}

static void put_pixel(uint8_t *buffer, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (buffer == nullptr || x < 0 || y < 0 || x >= kHdmiWidth || y >= kHdmiHeight) {
        return;
    }

    const size_t offset = ((size_t)y * kHdmiWidth + (size_t)x) * kBytesPerPixel;
    buffer[offset + 0] = r;
    buffer[offset + 1] = g;
    buffer[offset + 2] = b;
}

static void fill_rect(uint8_t *buffer, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    const int x0 = x < 0 ? 0 : x;
    const int y0 = y < 0 ? 0 : y;
    const int x1 = (x + w) > kHdmiWidth ? kHdmiWidth : (x + w);
    const int y1 = (y + h) > kHdmiHeight ? kHdmiHeight : (y + h);

    for (int py = y0; py < y1; ++py) {
        uint8_t *row = buffer + ((size_t)py * kHdmiWidth + (size_t)x0) * kBytesPerPixel;
        for (int px = x0; px < x1; ++px) {
            row[0] = r;
            row[1] = g;
            row[2] = b;
            row += kBytesPerPixel;
        }
    }
}

static void draw_line_h(uint8_t *buffer, int x, int y, int w, uint8_t r, uint8_t g, uint8_t b)
{
    fill_rect(buffer, x, y, w, 1, r, g, b);
}

static void draw_line_v(uint8_t *buffer, int x, int y, int h, uint8_t r, uint8_t g, uint8_t b)
{
    fill_rect(buffer, x, y, 1, h, r, g, b);
}

static void draw_hdmi_logo(uint8_t *buffer)
{
    const int x = 30;
    const int y = 22;
    const int s = 5;
    const int h = 9 * s;
    const int w = 6 * s;
    const int gap = 2 * s;

    fill_rect(buffer, 0, 0, kHdmiWidth, 82, 8, 12, 18);

    int lx = x;
    fill_rect(buffer, lx, y, s, h, 245, 245, 245);
    fill_rect(buffer, lx + w - s, y, s, h, 245, 245, 245);
    fill_rect(buffer, lx, y + 4 * s, w, s, 245, 245, 245);

    lx += w + gap;
    fill_rect(buffer, lx, y, s, h, 245, 245, 245);
    fill_rect(buffer, lx, y, w - s, s, 245, 245, 245);
    fill_rect(buffer, lx, y + h - s, w - s, s, 245, 245, 245);
    fill_rect(buffer, lx + w - s, y + s, s, h - 2 * s, 245, 245, 245);

    lx += w + gap;
    fill_rect(buffer, lx, y, s, h, 245, 245, 245);
    fill_rect(buffer, lx + w - s, y, s, h, 245, 245, 245);
    for (int i = 0; i < 4; ++i) {
        fill_rect(buffer, lx + s + i * s, y + (i + 1) * s, s, s, 245, 245, 245);
        fill_rect(buffer, lx + w - 2 * s - i * s, y + (i + 1) * s, s, s, 245, 245, 245);
    }

    lx += w + gap;
    fill_rect(buffer, lx, y, w, s, 245, 245, 245);
    fill_rect(buffer, lx + 2 * s, y, s, h, 245, 245, 245);
    fill_rect(buffer, lx, y + h - s, w, s, 245, 245, 245);

    fill_rect(buffer, 300, 26, 220, 10, 74, 222, 128);
    fill_rect(buffer, 300, 44, 320, 10, 70, 160, 255);
    fill_rect(buffer, 300, 62, 150, 10, 255, 210, 70);
}

static void draw_base_pattern(uint8_t *buffer)
{
    static const uint8_t colors[][3] = {
        {255, 255, 255},
        {255, 255, 0},
        {0, 255, 255},
        {0, 255, 0},
        {255, 0, 255},
        {255, 0, 0},
        {0, 0, 255},
        {20, 20, 20},
    };

    const int bar_h = 280;
    const int bar_w = kHdmiWidth / 8;
    for (int i = 0; i < 8; ++i) {
        fill_rect(buffer, i * bar_w, 82, bar_w + 1, bar_h,
                  colors[i][0], colors[i][1], colors[i][2]);
    }

    fill_rect(buffer, 0, 362, kHdmiWidth, kHdmiHeight - 362, 22, 26, 32);

    for (int x = 0; x < kHdmiWidth; x += 40) {
        draw_line_v(buffer, x, 362, kHdmiHeight - 362, 68, 72, 80);
    }
    for (int y = 362; y < kHdmiHeight; y += 40) {
        draw_line_h(buffer, 0, y, kHdmiWidth, 68, 72, 80);
    }

    for (int x = 0; x < kHdmiWidth; ++x) {
        const uint8_t shade = (uint8_t)((x * 255) / (kHdmiWidth - 1));
        fill_rect(buffer, x, 500, 1, 56, shade, shade, shade);
    }

    fill_rect(buffer, 30, 390, 220, 72, 0, 0, 0);
    fill_rect(buffer, 36, 396, 208, 60, 255, 255, 255);
    for (int y = 396; y < 456; y += 8) {
        draw_line_h(buffer, 36, y, 208, 0, 0, 0);
    }
    for (int x = 36; x < 244; x += 8) {
        draw_line_v(buffer, x, 396, 60, 0, 0, 0);
    }

    draw_hdmi_logo(buffer);
}

static void draw_motion_overlay(uint8_t *buffer, uint32_t frame)
{
    const int span = kHdmiWidth - 120;
    const int x = 40 + (int)((frame * 9U) % (uint32_t)span);
    const int y = 390 + (int)(((frame / 3U) % 80U));
    const int scan_y = 82 + (int)((frame * 5U) % 470U);
    const int pulse = (int)((frame * 7U) & 0xFFU);

    fill_rect(buffer, x, y, 96, 58, 255, 255, 255);
    fill_rect(buffer, x + 6, y + 6, 84, 46, 0, 0, 0);
    fill_rect(buffer, x + 14, y + 14, 68, 30, clamp_u8(255 - pulse), clamp_u8(80 + pulse / 2), 255);

    fill_rect(buffer, 0, scan_y, kHdmiWidth, 3, 255, 255, 255);
    fill_rect(buffer, 640, 24, 128, 44, clamp_u8(40 + pulse), 255, clamp_u8(255 - pulse / 2));
}

static void draw_frame(uint8_t *buffer, factory_hdmi_mode_t mode, uint32_t frame)
{
    draw_base_pattern(buffer);
    if (mode == FACTORY_HDMI_MODE_MOTION) {
        draw_motion_overlay(buffer, frame);
    }
}

static esp_err_t submit_frame(uint8_t *buffer)
{
    if (s_panel == nullptr || buffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_flush_done != nullptr) {
        xSemaphoreTake(s_flush_done, 0);
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, kHdmiWidth, kHdmiHeight, buffer);
    if (err != ESP_OK) {
        return err;
    }

    if (s_flush_done != nullptr &&
        xSemaphoreTake(s_flush_done, kFlushTimeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void render_task(void *arg)
{
    (void)arg;

    uint32_t local_frame = 0;
    uint32_t last_fps_frame = 0;
    int64_t last_fps_time_us = esp_timer_get_time();
    const uint32_t target_fps = CONFIG_FACTORY_HDMI_TARGET_FPS > 0 ? CONFIG_FACTORY_HDMI_TARGET_FPS : 30;
    const TickType_t frame_delay = pdMS_TO_TICKS(1000U / target_fps);

    while (!s_stop_requested) {
        const factory_hdmi_mode_t mode = s_state.mode;
        uint8_t *buffer = s_framebuffers[local_frame & 1U];

        draw_frame(buffer, mode, local_frame);
        esp_err_t err = submit_frame(buffer);
        if (err == ESP_OK) {
            s_state.frame_count++;
        } else if (err == ESP_ERR_TIMEOUT) {
            snprintf(s_last_error, sizeof(s_last_error), "Flush timeout");
            s_state.last_error = s_last_error;
        } else {
            set_error("draw_bitmap", err);
        }

        local_frame++;
        if ((local_frame % 30U) == 0U && s_panel != nullptr) {
            s_state.ready = esp_lcd_panel_lt8912b_is_ready(s_panel);
        }

        const int64_t now_us = esp_timer_get_time();
        const int64_t fps_window_us = now_us - last_fps_time_us;
        if (fps_window_us >= 1000000) {
            const uint32_t frames = s_state.frame_count - last_fps_frame;
            s_state.fps = (uint32_t)((uint64_t)frames * 1000000ULL / (uint64_t)fps_window_us);
            last_fps_frame = s_state.frame_count;
            last_fps_time_us = now_us;
            refresh_free_psram();
        }

        vTaskDelay(frame_delay > 0 ? frame_delay : 1);
    }

    s_render_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" bool factory_hdmi_init(void)
{
    s_state.initialized = true;
    s_state.width = kHdmiWidth;
    s_state.height = kHdmiHeight;
    refresh_free_psram();
    return true;
}

extern "C" void factory_hdmi_deinit(void)
{
    factory_hdmi_stop();
}

extern "C" bool factory_hdmi_start(factory_hdmi_mode_t mode)
{
    factory_hdmi_init();
    clear_error();
    s_state.mode = mode;

    if (s_state.running) {
        s_state.status_text = "Running";
        return true;
    }

    s_state.status_text = "Starting";
    refresh_free_psram();

    esp_err_t err = init_panel();
    if (err != ESP_OK) {
        set_error("HDMI init", err);
        return false;
    }

    if (!allocate_buffers()) {
        delete_panel_handles();
        hdmi_power_off();
        return false;
    }

    if (s_flush_done == nullptr) {
        s_flush_done = xSemaphoreCreateBinary();
    }
    if (s_flush_done == nullptr) {
        snprintf(s_last_error, sizeof(s_last_error), "Create flush semaphore failed");
        s_state.last_error = s_last_error;
        s_state.status_text = "Error";
        free_buffers();
        delete_panel_handles();
        hdmi_power_off();
        return false;
    }

    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_color_trans_done = flush_done_cb;
    err = esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks, nullptr);
    if (err != ESP_OK) {
        set_error("Register HDMI callback", err);
        free_buffers();
        delete_panel_handles();
        hdmi_power_off();
        return false;
    }

    s_stop_requested = false;
    s_state.frame_count = 0;
    s_state.fps = 0;
    s_state.running = true;
    s_state.status_text = "Running";

    BaseType_t task_ok = xTaskCreate(
        render_task,
        "factory_hdmi",
        CONFIG_FACTORY_HDMI_TASK_STACK_SIZE,
        nullptr,
        4,
        &s_render_task);
    if (task_ok != pdPASS) {
        snprintf(s_last_error, sizeof(s_last_error), "Create HDMI task failed");
        s_state.last_error = s_last_error;
        s_state.status_text = "Error";
        s_state.running = false;
        free_buffers();
        delete_panel_handles();
        hdmi_power_off();
        return false;
    }

    return true;
}

extern "C" void factory_hdmi_stop(void)
{
    s_stop_requested = true;
    for (int i = 0; i < kStopWaitLoops && s_render_task != nullptr; ++i) {
        vTaskDelay(kStopWaitDelay);
    }

    if (s_render_task != nullptr) {
        ESP_LOGW(TAG, "HDMI render task did not stop before cleanup");
    }

    if (s_flush_done != nullptr) {
        vSemaphoreDelete(s_flush_done);
        s_flush_done = nullptr;
    }

    free_buffers();
    delete_panel_handles();
    hdmi_power_off();

    s_state.running = false;
    s_state.ready = false;
    s_state.fps = 0;
    s_state.status_text = "Stopped";
    refresh_free_psram();
}

extern "C" void factory_hdmi_set_mode(factory_hdmi_mode_t mode)
{
    s_state.mode = mode;
}

extern "C" const factory_hdmi_state_t *factory_hdmi_get_state(void)
{
    refresh_free_psram();
    return &s_state;
}
