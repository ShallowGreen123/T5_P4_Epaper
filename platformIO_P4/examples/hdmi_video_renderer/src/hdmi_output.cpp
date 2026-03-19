#include "hdmi_output.h"

#include <Arduino.h>
#include <Wire.h>

#include "ExtensionIOXL9555.hpp"
#include "hdmi_config.h"

#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if __has_include(<esp_lcd_mipi_dsi.h>)
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_types.h>
#else
#error "Missing esp_lcd MIPI DSI headers"
#endif

#include "../../reference/managed_components/espressif__esp_lcd_lt8912b/include/esp_lcd_lt8912b.h"

static ExtensionIOXL9555 g_io;
static volatile uint32_t g_refresh_done_count = 0;
static volatile uint32_t g_color_trans_done_count = 0;

static bool probeI2cAddr(TwoWire &wire, uint8_t addr)
{
    wire.beginTransmission(addr);
    return wire.endTransmission() == 0;
}

static uint8_t findIoExpanderAddr()
{
    for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
        Serial.printf("[hdmi] probing io expander at 0x%02X\n", addr);
        if (probeI2cAddr(Wire, addr)) {
            Serial.printf("[hdmi] io expander found at 0x%02X\n", addr);
            return addr;
        }
    }
    Serial.println("[hdmi] io expander not found");
    return 0xFF;
}

bool HdmiOutput::begin()
{
    Serial.println("[hdmi] begin");
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    // Prefer a conservative clock for reliable bring-up on longer traces.
    Wire.setClock(100000);
    Serial.printf("[hdmi] Wire.begin done (sda=%d scl=%d clk=%u)\n", BOARD_I2C_SDA, BOARD_I2C_SCL, Wire.getClock());

    Serial.println("[hdmi] initIoExpander");
    if (!initIoExpander()) {
        Serial.println("[hdmi] initIoExpander failed");
        return false;
    }

    // Allow the HDMI/DSI hardware to power up and settle after reset.
    delay(300);

    Serial.println("[hdmi] initLt8912");
    if (!initLt8912()) {
        Serial.println("[hdmi] LT8912 probe fail");
        return false;
    }

    // Give the bridge some time after probe before starting DSI.
    delay(300);

    Serial.println("[hdmi] initDsiPhyPower");
    if (!initDsiPhyPower()) {
        Serial.println("[hdmi] initDsiPhyPower failed");
        return false;
    }

    Serial.println("[hdmi] initDsi");
    if (!initDsi()) {
        Serial.println("[hdmi] initDsi failed");
        return false;
    }

    Serial.println("[hdmi] begin ok");
    return true;
}

HdmiFramebuffers HdmiOutput::framebuffers() const
{
    return fbs_;
}

bool HdmiOutput::present(void *fb)
{
    if (!panel_ || !fb) {
        return false;
    }
    const uint32_t refresh_before = g_refresh_done_count;
    const uint32_t color_before = g_color_trans_done_count;
    auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, HDMI_FRAME_WIDTH, HDMI_FRAME_HEIGHT, fb);
    if (err != ESP_OK) {
        return false;
    }

    if (refresh_sem_) {
        auto sem = static_cast<SemaphoreHandle_t>(refresh_sem_);
        xSemaphoreTake(sem, 0);
        if (xSemaphoreTake(sem, pdMS_TO_TICKS(50)) != pdTRUE) {
            const uint32_t refresh_after = g_refresh_done_count;
            const uint32_t color_after = g_color_trans_done_count;
            Serial.printf("[hdmi] refresh wait timeout (refresh +%lu color +%lu)\n",
                          (unsigned long)(refresh_after - refresh_before),
                          (unsigned long)(color_after - color_before));
        }
    }
    return true;
}

bool HdmiOutput::initIoExpander()
{
    uint8_t addr = findIoExpanderAddr();
    if (addr == 0xFF) {
        return false;
    }
    if (!g_io.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, addr)) {
        return false;
    }

    g_io.pinMode(BOARD_PCA_12_1V8_EN, OUTPUT);
    g_io.digitalWrite(BOARD_PCA_12_1V8_EN, HIGH);
    delay(10);

    g_io.pinMode(BOARD_PCA_07_HDMI_EN, OUTPUT);
    g_io.digitalWrite(BOARD_PCA_07_HDMI_EN, HIGH);
    delay(10);

    g_io.pinMode(BOARD_HDMI_RST, OUTPUT);
    g_io.digitalWrite(BOARD_HDMI_RST, LOW);
    delay(50);
    g_io.digitalWrite(BOARD_HDMI_RST, HIGH);
    delay(50);
    return true;
}

bool HdmiOutput::initLt8912()
{
    if (i2c_bus_ && lt8912_io_main_ && lt8912_io_cec_ && lt8912_io_avi_) {
        return true;
    }

    pinMode(BOARD_HDMI_INT, INPUT);
    Wire.end();

    i2c_master_bus_config_t i2c_bus_conf = {};
    i2c_bus_conf.i2c_port = 0;
    i2c_bus_conf.sda_io_num = (gpio_num_t)BOARD_I2C_SDA;
    i2c_bus_conf.scl_io_num = (gpio_num_t)BOARD_I2C_SCL;
    i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_conf, &bus);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] i2c_new_master_bus failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    esp_lcd_panel_io_handle_t io_main = nullptr;
    esp_lcd_panel_io_handle_t io_cec = nullptr;
    esp_lcd_panel_io_handle_t io_avi = nullptr;

    esp_lcd_panel_io_i2c_config_t io_cfg_main = {};
    io_cfg_main.dev_addr = LT8912B_IO_I2C_MAIN_ADDRESS;
    io_cfg_main.control_phase_bytes = 1;
    io_cfg_main.dc_bit_offset = 0;
    io_cfg_main.lcd_cmd_bits = 8;
    io_cfg_main.lcd_param_bits = 8;
    io_cfg_main.flags.disable_control_phase = 1;
    io_cfg_main.scl_speed_hz = 100000;
    ret = esp_lcd_new_panel_io_i2c(bus, &io_cfg_main, &io_main);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] LT8912 main IO init failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg_cec = io_cfg_main;
    io_cfg_cec.dev_addr = LT8912B_IO_I2C_CEC_ADDRESS;
    ret = esp_lcd_new_panel_io_i2c(bus, &io_cfg_cec, &io_cec);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] LT8912 CEC/DSI IO init failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg_avi = io_cfg_main;
    io_cfg_avi.dev_addr = LT8912B_IO_I2C_AVI_ADDRESS;
    ret = esp_lcd_new_panel_io_i2c(bus, &io_cfg_avi, &io_avi);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] LT8912 AVI IO init failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    i2c_bus_ = bus;
    lt8912_io_main_ = io_main;
    lt8912_io_cec_ = io_cec;
    lt8912_io_avi_ = io_avi;
    lt8912_addr_ = LT8912B_IO_I2C_MAIN_ADDRESS;
    lt8912_on_ddc_ = false;
    Serial.printf("[hdmi] LT8912 IO handles ready (main=0x%02X cec=0x%02X avi=0x%02X)\n", LT8912B_IO_I2C_MAIN_ADDRESS,
                  LT8912B_IO_I2C_CEC_ADDRESS, LT8912B_IO_I2C_AVI_ADDRESS);
    return true;
}

bool HdmiOutput::initDsiPhyPower()
{
    if (dsi_phy_ldo_) {
        return true;
    }

    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BOARD_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BOARD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &dsi_phy_ldo_);
    if (err != ESP_OK) {
        Serial.printf("[hdmi] esp_ldo_acquire_channel failed: %s\n", esp_err_to_name(err));
        dsi_phy_ldo_ = nullptr;
        return false;
    }
    Serial.printf("[hdmi] DSI PHY LDO on (chan=%d voltage=%dmV)\n", BOARD_MIPI_DSI_PHY_PWR_LDO_CHAN, BOARD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
    delay(10);
    return true;
}

static bool refreshDoneCallback(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *user_ctx)
{
    auto sem = static_cast<SemaphoreHandle_t>(user_ctx);
    BaseType_t higher = pdFALSE;
    g_refresh_done_count++;
    xSemaphoreGiveFromISR(sem, &higher);
    return higher == pdTRUE;
}

static bool colorTransDoneCallback(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *)
{
    g_color_trans_done_count++;
    return false;
}

typedef struct {
    const esp_lcd_dsi_bus_config_t *cfg;
    esp_lcd_dsi_bus_handle_t *out_bus;
    esp_err_t ret;
    TaskHandle_t notify_task;
} DsiBusCreateCtx;

static void dsiBusCreateTask(void *arg)
{
    auto *ctx = static_cast<DsiBusCreateCtx *>(arg);
    ctx->ret = esp_lcd_new_dsi_bus(ctx->cfg, ctx->out_bus);
    xTaskNotifyGive(ctx->notify_task);
    vTaskDelete(nullptr);
}

static esp_err_t newDsiBusWithTimeout(const esp_lcd_dsi_bus_config_t *cfg, esp_lcd_dsi_bus_handle_t *out_bus, uint32_t timeout_ms)
{
    DsiBusCreateCtx ctx = {
        .cfg = cfg,
        .out_bus = out_bus,
        .ret = ESP_FAIL,
        .notify_task = xTaskGetCurrentTaskHandle(),
    };

    TaskHandle_t task = nullptr;
    // Keep the stack modest; most heavy lifting happens in ROM/IDF code.
    BaseType_t ok = xTaskCreate(dsiBusCreateTask, "dsi_new_bus", 4096, &ctx, tskIDLE_PRIORITY + 1, &task);
    if (ok != pdPASS || !task) {
        return ESP_ERR_NO_MEM;
    }

    const uint32_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (ulTaskNotifyTake(pdTRUE, ticks) == 0) {
        vTaskDelete(task);
        return ESP_ERR_TIMEOUT;
    }
    return ctx.ret;
}

bool HdmiOutput::initDsi()
{
    if (panel_) {
        return true;
    }
    if (!lt8912_io_main_ || !lt8912_io_cec_ || !lt8912_io_avi_) {
        Serial.println("[hdmi] LT8912 IO handles are not ready");
        return false;
    }

    esp_lcd_dsi_bus_handle_t bus = static_cast<esp_lcd_dsi_bus_handle_t>(dsi_bus_);
    esp_lcd_dsi_bus_config_t bus_config = {};
    // ESP32-P4 (legacy DPHY PLLREF sources): using *_DEFAULT may map to XTAL on newer enums,
    // which will abort() in the current low-level driver. Use a known-supported source.
    bus_config.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;
    bus_config.num_data_lanes = 2;
    bus_config.lane_bit_rate_mbps = HDMI_DSI_LANE_BIT_RATE_MBPS;

    esp_err_t ret = ESP_FAIL;
    if (!bus) {
        // Try multiple DSI bus IDs; some variants use bus 1 instead of bus 0.
        for (int bus_id = 0; bus_id < 2; bus_id++) {
            bus_config.bus_id = bus_id;
            Serial.printf("[hdmi] trying DSI bus_id=%d\n", bus_id);
            ret = newDsiBusWithTimeout(&bus_config, &bus, 2000);
            if (ret == ESP_OK) {
                Serial.printf("[hdmi] DSI bus_id=%d ok\n", bus_id);
                dsi_bus_ = bus;
                break;
            }
            if (ret == ESP_ERR_TIMEOUT) {
                Serial.printf("[hdmi] esp_lcd_new_dsi_bus bus_id=%d timeout (PLL lock / lane stop)\n", bus_id);
                continue;
            }
            Serial.printf("[hdmi] esp_lcd_new_dsi_bus bus_id=%d failed: %s\n", bus_id, esp_err_to_name(ret));
        }
    }
    if (!bus) {
        return false;
    }

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_dpi_panel_config_t dpi_config = {};
    dpi_config.virtual_channel = 0;
    dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz = HDMI_DPI_CLOCK_MHZ;
    dpi_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
    dpi_config.in_color_format = LCD_COLOR_FMT_RGB888;
    dpi_config.out_color_format = LCD_COLOR_FMT_RGB888;
    dpi_config.num_fbs = HDMI_FRAMEBUFFER_COUNT;
    dpi_config.video_timing.h_size = HDMI_FRAME_WIDTH;
    dpi_config.video_timing.v_size = HDMI_FRAME_HEIGHT;
    dpi_config.video_timing.hsync_back_porch = HDMI_HSYNC_BACK_PORCH;
    dpi_config.video_timing.hsync_pulse_width = HDMI_HSYNC_PULSE_WIDTH;
    dpi_config.video_timing.hsync_front_porch = HDMI_HSYNC_FRONT_PORCH;
    dpi_config.video_timing.vsync_back_porch = HDMI_VSYNC_BACK_PORCH;
    dpi_config.video_timing.vsync_pulse_width = HDMI_VSYNC_PULSE_WIDTH;
    dpi_config.video_timing.vsync_front_porch = HDMI_VSYNC_FRONT_PORCH;
    dpi_config.flags.disable_lp = true;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
    dpi_config.flags.use_dma2d = true;
#endif
    const esp_lcd_panel_lt8912b_video_timing_t video_timing = ESP_LCD_LT8912B_VIDEO_TIMING_800x600_60Hz();
    lt8912b_vendor_config_t vendor_config = {
        .video_timing = video_timing,
        .mipi_config = {
            .dsi_bus = bus,
            .dpi_config = &dpi_config,
            .lane_num = bus_config.num_data_lanes,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 24;
    panel_config.vendor_config = &vendor_config;
    const esp_lcd_panel_lt8912b_io_t io_all = {
        .main = static_cast<esp_lcd_panel_io_handle_t>(lt8912_io_main_),
        .cec_dsi = static_cast<esp_lcd_panel_io_handle_t>(lt8912_io_cec_),
        .avi = static_cast<esp_lcd_panel_io_handle_t>(lt8912_io_avi_),
    };

    Serial.printf("[hdmi] LT8912B config: %ux%u RGB888 @ %lu MHz\n", HDMI_FRAME_WIDTH, HDMI_FRAME_HEIGHT,
                  (unsigned long)video_timing.pclk_mhz);

    ret = esp_lcd_new_panel_lt8912b(&io_all, &panel_config, &panel);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_new_panel_lt8912b failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    ret = esp_lcd_panel_reset(panel);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_panel_reset failed: %s\n", esp_err_to_name(ret));
        return false;
    }
    ret = esp_lcd_panel_init(panel);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_panel_init failed: %s\n", esp_err_to_name(ret));
        return false;
    }
    ret = esp_lcd_panel_disp_on_off(panel, true);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        Serial.println("[hdmi] esp_lcd_panel_disp_on_off not supported by LT8912B driver, continuing");
    } else if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_panel_disp_on_off failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    bool hdmi_ready = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        hdmi_ready = esp_lcd_panel_lt8912b_is_ready(static_cast<esp_lcd_panel_t *>(panel));
        Serial.printf("[hdmi] LT8912 ready check %d/10: %s\n", attempt + 1, hdmi_ready ? "ready" : "not ready");
        if (hdmi_ready) {
            break;
        }
        delay(100);
    }
    if (!hdmi_ready) {
        Serial.println("[hdmi] LT8912 HPD not asserted yet, continuing anyway");
    }

    void *fb0 = nullptr;
    void *fb1 = nullptr;
    void *fb2 = nullptr;
    ret = esp_lcd_dpi_panel_get_frame_buffer(panel, HDMI_FRAMEBUFFER_COUNT, &fb0, &fb1, &fb2);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_dpi_panel_get_frame_buffer failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    fbs_.fb[0] = fb0;
    fbs_.fb[1] = fb1;
    fbs_.fb[2] = fb2;
    fbs_.count = HDMI_FRAMEBUFFER_COUNT;

    if (!refresh_sem_) {
        refresh_sem_ = xSemaphoreCreateBinary();
    }
    if (!refresh_sem_) {
        Serial.println("[hdmi] create refresh semaphore failed");
        return false;
    }

    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_color_trans_done = colorTransDoneCallback;
    cbs.on_refresh_done = refreshDoneCallback;
    ret = esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, refresh_sem_);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_dpi_panel_register_event_callbacks failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    Serial.println("[hdmi] showing DSI test pattern for 800 ms");
    ret = esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
    if (ret == ESP_OK) {
        delay(800);
        esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_NONE);
    } else {
        Serial.printf("[hdmi] esp_lcd_dpi_panel_set_pattern failed: %s\n", esp_err_to_name(ret));
    }

    panel_ = panel;
    return true;
}
