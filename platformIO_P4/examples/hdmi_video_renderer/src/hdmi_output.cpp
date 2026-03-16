#include "hdmi_output.h"

#include <Arduino.h>
#include <Wire.h>

#include "ExtensionIOXL9555.hpp"
#include "hdmi_config.h"

#include "esp_err.h"
#include "esp_ldo_regulator.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if __has_include(<esp_lcd_mipi_dsi.h>)
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_types.h>
#else
#error "Missing esp_lcd MIPI DSI headers"
#endif

static ExtensionIOXL9555 g_io;
static TwoWire g_ddcWire(1);

static bool probeI2cAddr(TwoWire &wire, uint8_t addr)
{
    wire.beginTransmission(addr);
    return wire.endTransmission() == 0;
}

static void dumpI2cScan(TwoWire &wire, uint8_t from, uint8_t to)
{
    Serial.printf("[hdmi] i2c scan 0x%02X..0x%02X: ", from, to);
    bool any = false;
    for (uint8_t addr = from; addr <= to; addr++) {
        if (probeI2cAddr(wire, addr)) {
            Serial.printf("0x%02X ", addr);
            any = true;
        }
    }
    if (!any) {
        Serial.print("(none)");
    }
    Serial.println();
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
        Serial.println("[hdmi] DSI deskew error");
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
    auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, HDMI_FRAME_WIDTH, HDMI_FRAME_HEIGHT, fb);
    return err == ESP_OK;
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
    pinMode(BOARD_HDMI_INT, INPUT);

    // Try a handful of commonly-used LT8912 I2C addresses (7-bit).
    // Keep the preferred/default first to match docs/tests.
    static const uint8_t candidates[] = {
        HDMI_LT8912_I2C_ADDR, 0x48, 0x49, 0x4A, 0x4B, 0x24, 0x2D,
    };

    lt8912_on_ddc_ = false;

    for (int attempt = 0; attempt < 10; attempt++) {
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            const uint8_t addr = candidates[i];
            if (probeI2cAddr(Wire, addr)) {
                lt8912_addr_ = addr;
                Serial.printf("[hdmi] LT8912 I2C ack at 0x%02X on SYS-I2C (attempt=%d)\n", addr, attempt + 1);
                return true;
            }
        }
        delay(20);
    }

    // Some LT8912 modules expose the control I2C on the HDMI DDC pins instead of the main system I2C.
    // Probe the DDC bus as a fallback.
    g_ddcWire.begin(BOARD_HDMI_DDC_SDA, BOARD_HDMI_DDC_SCL);
    g_ddcWire.setClock(100000);
    Serial.printf("[hdmi] probing LT8912 on DDC-I2C (sda=%d scl=%d clk=%u)\n", BOARD_HDMI_DDC_SDA, BOARD_HDMI_DDC_SCL, g_ddcWire.getClock());

    for (int attempt = 0; attempt < 10; attempt++) {
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            const uint8_t addr = candidates[i];
            if (probeI2cAddr(g_ddcWire, addr)) {
                lt8912_addr_ = addr;
                lt8912_on_ddc_ = true;
                Serial.printf("[hdmi] LT8912 I2C ack at 0x%02X on DDC-I2C (attempt=%d)\n", addr, attempt + 1);
                return true;
            }
        }
        delay(20);
    }

    Serial.printf("[hdmi] LT8912 not found on SYS-I2C or DDC-I2C (preferred=0x%02X)\n", HDMI_LT8912_I2C_ADDR);
    Serial.print("[hdmi] SYS-I2C ");
    dumpI2cScan(Wire, 0x20, 0x77);
    Serial.print("[hdmi] DDC-I2C ");
    dumpI2cScan(g_ddcWire, 0x20, 0x77);
    return false;
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
    xSemaphoreGiveFromISR(sem, &higher);
    return higher == pdTRUE;
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
    esp_lcd_dsi_bus_handle_t bus = nullptr;
    esp_lcd_dsi_bus_config_t bus_config = {};
    // ESP32-P4 (legacy DPHY PLLREF sources): using *_DEFAULT may map to XTAL on newer enums,
    // which will abort() in the current low-level driver. Use a known-supported source.
    bus_config.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;
    bus_config.num_data_lanes = 2;
    bus_config.lane_bit_rate_mbps = HDMI_DSI_LANE_BIT_RATE_MBPS;

    // Try multiple DSI bus IDs; some variants use bus 1 instead of bus 0.
    esp_err_t ret = ESP_FAIL;
    for (int bus_id = 0; bus_id < 2; bus_id++) {
        bus_config.bus_id = bus_id;
        Serial.printf("[hdmi] trying DSI bus_id=%d\n", bus_id);
        ret = newDsiBusWithTimeout(&bus_config, &bus, 2000);
        if (ret == ESP_OK) {
            Serial.printf("[hdmi] DSI bus_id=%d ok\n", bus_id);
            break;
        }
        if (ret == ESP_ERR_TIMEOUT) {
            Serial.printf("[hdmi] esp_lcd_new_dsi_bus bus_id=%d timeout (PLL lock / lane stop)\n", bus_id);
            continue;
        }
        Serial.printf("[hdmi] esp_lcd_new_dsi_bus bus_id=%d failed: %s\n", bus_id, esp_err_to_name(ret));
    }
    if (ret != ESP_OK) {
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
    esp_lcd_video_timing_t timing = {};
    timing.h_size = HDMI_FRAME_WIDTH;
    timing.v_size = HDMI_FRAME_HEIGHT;
    timing.hsync_pulse_width = 44;
    timing.hsync_back_porch = 148;
    timing.hsync_front_porch = 88;
    timing.vsync_pulse_width = 5;
    timing.vsync_back_porch = 36;
    timing.vsync_front_porch = 4;
    dpi_config.video_timing = timing;
    dpi_config.num_fbs = HDMI_FRAMEBUFFER_COUNT;
    dpi_config.flags.use_dma2d = true;

    ret = esp_lcd_new_panel_dpi(bus, &dpi_config, &panel);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_new_panel_dpi failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    ret = esp_lcd_panel_init(panel);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_panel_init failed: %s\n", esp_err_to_name(ret));
        return false;
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

    static SemaphoreHandle_t refresh_sem = xSemaphoreCreateBinary();
    if (!refresh_sem) {
        Serial.println("[hdmi] create refresh semaphore failed");
        return false;
    }

    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_refresh_done = refreshDoneCallback;
    ret = esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, refresh_sem);
    if (ret != ESP_OK) {
        Serial.printf("[hdmi] esp_lcd_dpi_panel_register_event_callbacks failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    panel_ = panel;
    return true;
}
