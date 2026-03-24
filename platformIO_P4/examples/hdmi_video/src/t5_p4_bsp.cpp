#include "t5_p4_bsp.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"

namespace {

static const char *TAG = "t5_p4_bsp";

constexpr uint32_t kI2cClockHz = 400000;
constexpr uint32_t kSdClockHz = 25000000;
constexpr int kI2cTimeoutMs = 100;
constexpr uint32_t kLt8912I2cClockHz = 100000;
constexpr uint32_t kHdmiPowerStableDelayMs = 120;
constexpr uint32_t kHdmiProbeRetryDelayMs = 60;
constexpr uint32_t kHdmiProbeRetryCount = 3;
constexpr uint8_t kLt8912AddrMain = BOARD_I2C_ADDR_LT8912;
constexpr uint8_t kLt8912AddrCec = 0x49;
constexpr uint8_t kLt8912AddrAvi = 0x4A;

typedef struct {
    uint8_t reg;
    uint8_t data;
} lt8912_reg_t;

typedef struct {
    uint16_t hfp;
    uint16_t hs;
    uint16_t hbp;
    uint16_t hact;
    uint16_t htotal;
    uint16_t vfp;
    uint16_t vs;
    uint16_t vbp;
    uint16_t vact;
    uint16_t vtotal;
    bool h_polarity;
    bool v_polarity;
    uint8_t vic;
    uint8_t aspect_ratio;
    uint32_t pclk_mhz;
} lt8912_video_timing_t;

constexpr uint8_t kLt8912AspectRatio16By9 = 0x02;

static const lt8912_video_timing_t kLt8912Timing720p = {
    .hfp = 48,
    .hs = 32,
    .hbp = 80,
    .hact = 1280,
    .htotal = 1440,
    .vfp = 3,
    .vs = 5,
    .vbp = 13,
    .vact = 720,
    .vtotal = 741,
    .h_polarity = true,
    .v_polarity = false,
    .vic = 0,
    .aspect_ratio = kLt8912AspectRatio16By9,
    .pclk_mhz = 64,
};

static const lt8912_reg_t kCmdDigitalClockEn[] = {
    {0x02, 0xf7}, {0x08, 0xff}, {0x09, 0xff}, {0x0a, 0xff}, {0x0b, 0x7c}, {0x0c, 0xff},
};

static const lt8912_reg_t kCmdTxAnalog[] = {
    {0x31, 0xe1}, {0x32, 0xe1}, {0x33, 0x0c}, {0x37, 0x00}, {0x38, 0x22}, {0x60, 0x82},
};

static const lt8912_reg_t kCmdCbusAnalog[] = {
    {0x39, 0x45}, {0x3a, 0x00}, {0x3b, 0x00},
};

static const lt8912_reg_t kCmdHdmiPllAnalog[] = {
    {0x44, 0x31}, {0x55, 0x44}, {0x57, 0x01}, {0x5a, 0x02},
};

static const lt8912_reg_t kCmdDdsConfig[] = {
    {0x4e, 0x93}, {0x4f, 0x3E}, {0x50, 0x29}, {0x51, 0x80}, {0x1e, 0x4f}, {0x1f, 0x5e},
    {0x20, 0x01}, {0x21, 0x2c}, {0x22, 0x01}, {0x23, 0xfa}, {0x24, 0x00}, {0x25, 0xc8},
    {0x26, 0x00}, {0x27, 0x5e}, {0x28, 0x01}, {0x29, 0x2c}, {0x2a, 0x01}, {0x2b, 0xfa},
    {0x2c, 0x00}, {0x2d, 0xc8}, {0x2e, 0x00}, {0x42, 0x64}, {0x43, 0x00}, {0x44, 0x04},
    {0x45, 0x00}, {0x46, 0x59}, {0x47, 0x00}, {0x48, 0xf2}, {0x49, 0x06}, {0x4a, 0x00},
    {0x4b, 0x72}, {0x4c, 0x45}, {0x4d, 0x00}, {0x52, 0x08}, {0x53, 0x00}, {0x54, 0xb2},
    {0x55, 0x00}, {0x56, 0xe4}, {0x57, 0x0d}, {0x58, 0x00}, {0x59, 0xe4}, {0x5a, 0x8a},
    {0x5b, 0x00}, {0x5c, 0x34}, {0x51, 0x00},
};

static const lt8912_reg_t kCmdAudioIisMode[] = {
    {0xB2, 0x01},
};

static const lt8912_reg_t kCmdAudioIisEn[] = {
    {0x06, 0x08}, {0x07, 0xF0}, {0x34, 0xD2}, {0x0F, 0x2B},
};

static const lt8912_reg_t kCmdLvdsBypass[] = {
    {0x44, 0x30}, {0x51, 0x05}, {0x50, 0x24}, {0x51, 0x2d}, {0x52, 0x04}, {0x69, 0x0e},
    {0x69, 0x8e}, {0x6a, 0x00}, {0x6c, 0xb8}, {0x6b, 0x51}, {0x04, 0xfb}, {0x04, 0xff},
    {0x7f, 0x00}, {0xa8, 0x13},
};

bool g_i2c_ready = false;
bool g_hdmi_fallback_i2c_ready = false;
bool g_sd_ready = false;
bool g_i2s_ready = false;
sdmmc_card_t *g_sdmmc_card = nullptr;

TwoWire g_hdmi_fallback_wire(1);
TwoWire *g_i2c = &Wire;
TwoWire *g_lt8912_i2c = &Wire;
const char *g_lt8912_i2c_tag = "main";

uint8_t g_pca_output_state[2] = {0x00, 0x00};
uint8_t g_pca_config_state[2] = {0xFF, 0xFF};

i2s_chan_handle_t g_i2s_tx = nullptr;
i2s_chan_handle_t g_i2s_rx = nullptr;

esp_lcd_dsi_bus_handle_t g_dsi_bus = nullptr;
esp_lcd_panel_handle_t g_display_panel = nullptr;
esp_err_t map_wire_error(uint8_t wire_err)
{
    switch (wire_err) {
        case 0:
            return ESP_OK;
        case 5:
            return ESP_ERR_TIMEOUT;
        case 2:
        case 3:
            return ESP_ERR_NOT_FOUND;
        default:
            return ESP_FAIL;
    }
}

esp_err_t init_i2c_bus(TwoWire &wire,
                       bool *ready_flag,
                       int sda,
                       int scl,
                       uint32_t clock_hz,
                       uint16_t timeout_ms)
{
    if (ready_flag == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*ready_flag) {
        return ESP_OK;
    }

    wire.setTimeOut(timeout_ms);
    if (!wire.begin(sda, scl, clock_hz)) {
        return ESP_FAIL;
    }
    wire.setClock(clock_hz);
    wire.setTimeOut(timeout_ms);
    *ready_flag = true;
    return ESP_OK;
}

bool probe_i2c_device_on_bus(TwoWire &wire, uint8_t address)
{
    wire.beginTransmission(address);
    return wire.endTransmission(true) == 0;
}

bool wait_for_i2c_device(TwoWire &wire,
                         uint8_t address,
                         uint32_t retry_count,
                         uint32_t retry_delay_ms)
{
    for (uint32_t attempt = 0; attempt < retry_count; ++attempt) {
        if (probe_i2c_device_on_bus(wire, address)) {
            return true;
        }
        if ((attempt + 1) < retry_count) {
            delay(retry_delay_ms);
        }
    }
    return false;
}

esp_err_t ensure_hdmi_fallback_i2c_bus(void)
{
    if (BOARD_HDMI_DDC_SDA == BOARD_I2C_SDA && BOARD_HDMI_DDC_SCL == BOARD_I2C_SCL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return init_i2c_bus(g_hdmi_fallback_wire,
                        &g_hdmi_fallback_i2c_ready,
                        BOARD_HDMI_DDC_SDA,
                        BOARD_HDMI_DDC_SCL,
                        kLt8912I2cClockHz,
                        kI2cTimeoutMs);
}

bool probe_i2c_device(uint8_t address)
{
    if (!g_i2c_ready || g_i2c == nullptr) {
        return false;
    }
    return probe_i2c_device_on_bus(*g_i2c, address);
}

esp_err_t select_lt8912_i2c_bus(void)
{
    g_lt8912_i2c = g_i2c;
    g_lt8912_i2c_tag = "main";

    if (wait_for_i2c_device(*g_i2c,
                            kLt8912AddrMain,
                            kHdmiProbeRetryCount,
                            kHdmiProbeRetryDelayMs)) {
        Serial.printf("[bsp] LT8912 responded on %s I2C bus (SDA=%d SCL=%d)\n",
                      g_lt8912_i2c_tag,
                      BOARD_I2C_SDA,
                      BOARD_I2C_SCL);
        return ESP_OK;
    }

    Serial.printf("[bsp] LT8912 early probe missed on %s I2C bus (SDA=%d SCL=%d)\n",
                  g_lt8912_i2c_tag,
                  BOARD_I2C_SDA,
                  BOARD_I2C_SCL);

    const esp_err_t fallback_bus_err = ensure_hdmi_fallback_i2c_bus();
    if (fallback_bus_err == ESP_OK &&
        wait_for_i2c_device(g_hdmi_fallback_wire,
                            kLt8912AddrMain,
                            kHdmiProbeRetryCount,
                            kHdmiProbeRetryDelayMs)) {
        g_lt8912_i2c = &g_hdmi_fallback_wire;
        g_lt8912_i2c_tag = "hdmi-ddc";
        Serial.printf("[bsp] LT8912 responded on fallback I2C bus (SDA=%d SCL=%d)\n",
                      BOARD_HDMI_DDC_SDA,
                      BOARD_HDMI_DDC_SCL);
        return ESP_OK;
    }

    if (fallback_bus_err == ESP_OK) {
        Serial.printf("[bsp] LT8912 also missed on fallback I2C bus (SDA=%d SCL=%d)\n",
                      BOARD_HDMI_DDC_SDA,
                      BOARD_HDMI_DDC_SCL);
    } else if (fallback_bus_err != ESP_ERR_NOT_SUPPORTED) {
        Serial.printf("[bsp] fallback HDMI I2C bus init failed: %s\n", esp_err_to_name(fallback_bus_err));
    }

    g_lt8912_i2c = g_i2c;
    g_lt8912_i2c_tag = "main";
    Serial.println("[bsp] LT8912 not ready during early probe, continuing with late init on main I2C path");
    return ESP_OK;
}

esp_err_t ensure_pca9535(void)
{
    return probe_i2c_device(BOARD_I2C_ADDR_PCA9535) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t ensure_es8311(void)
{
    return probe_i2c_device(BOARD_I2C_ADDR_ES8311) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t pca_write_registers(uint8_t start_reg, const uint8_t *data, size_t size)
{
    if (g_i2c == nullptr || data == nullptr || size > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    g_i2c->beginTransmission(BOARD_I2C_ADDR_PCA9535);
    g_i2c->write(start_reg);
    g_i2c->write(data, size);
    return map_wire_error(g_i2c->endTransmission(true));
}

esp_err_t pca_set_output(uint8_t pin, bool level)
{
    ESP_RETURN_ON_ERROR(ensure_pca9535(), TAG, "PCA9535 add device failed");

    const uint8_t port = pin / 8;
    const uint8_t bit = 1U << (pin % 8);
    if (port > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    g_pca_config_state[port] &= static_cast<uint8_t>(~bit);
    if (level) {
        g_pca_output_state[port] |= bit;
    } else {
        g_pca_output_state[port] &= static_cast<uint8_t>(~bit);
    }

    ESP_RETURN_ON_ERROR(pca_write_registers(0x06, g_pca_config_state, sizeof(g_pca_config_state)),
                        TAG, "PCA9535 config write failed");
    ESP_RETURN_ON_ERROR(pca_write_registers(0x02, g_pca_output_state, sizeof(g_pca_output_state)),
                        TAG, "PCA9535 output write failed");
    return ESP_OK;
}

esp_err_t lt8912_write(uint8_t address, uint8_t reg, uint8_t value)
{
    if (g_lt8912_i2c == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    g_lt8912_i2c->beginTransmission(address);
    g_lt8912_i2c->write(reg);
    g_lt8912_i2c->write(value);
    return map_wire_error(g_lt8912_i2c->endTransmission(true));
}

esp_err_t lt8912_read(uint8_t address, uint8_t reg, uint8_t *value)
{
    if (g_lt8912_i2c == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    g_lt8912_i2c->beginTransmission(address);
    g_lt8912_i2c->write(reg);
    esp_err_t err = map_wire_error(g_lt8912_i2c->endTransmission(false));
    if (err != ESP_OK) {
        return err;
    }

    if (g_lt8912_i2c->requestFrom(address, static_cast<size_t>(1), true) != 1) {
        return ESP_FAIL;
    }
    *value = static_cast<uint8_t>(g_lt8912_i2c->read());
    return ESP_OK;
}

esp_err_t lt8912_write_array(uint8_t address,
                             const lt8912_reg_t *regs,
                             size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(lt8912_write(address, regs[i].reg, regs[i].data), TAG, "LT8912 write failed");
    }
    return ESP_OK;
}

esp_err_t lt8912_send_mipi_analog(bool pn_swap)
{
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x3e, pn_swap ? 0xf6 : 0xd6), TAG, "LT8912 mipi analog failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x3f, 0xd4), TAG, "LT8912 mipi analog failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x41, 0x3c), TAG, "LT8912 mipi analog failed");
    return ESP_OK;
}

esp_err_t lt8912_send_mipi_basic(uint8_t lane_count, bool lane_swap)
{
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x10, 0x01), TAG, "LT8912 basic config failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x11, 0x10), TAG, "LT8912 basic config failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x13, lane_count), TAG, "LT8912 basic config failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x14, 0x00), TAG, "LT8912 basic config failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x15, lane_swap ? 0xa8 : 0x00), TAG, "LT8912 basic config failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x1a, 0x03), TAG, "LT8912 basic config failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x1b, 0x03), TAG, "LT8912 basic config failed");
    return ESP_OK;
}

esp_err_t lt8912_send_video_setup(const lt8912_video_timing_t &timing)
{
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x18, timing.hs % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x19, timing.vs % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x1c, timing.hact % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x1d, timing.hact / 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x2f, 0x0c), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x34, timing.htotal % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x35, timing.htotal / 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x36, timing.vtotal % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x37, timing.vtotal / 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x38, timing.vbp % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x39, timing.vbp / 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x3a, timing.vfp % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x3b, timing.vfp / 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x3c, timing.hbp % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x3d, timing.hbp / 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x3e, timing.hfp % 256), TAG, "LT8912 video setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrCec, 0x3f, timing.hfp / 256), TAG, "LT8912 video setup failed");
    return ESP_OK;
}

esp_err_t lt8912_send_avi_infoframe(const lt8912_video_timing_t &timing)
{
    const uint8_t sync_polarity = (timing.h_polarity ? 0x02 : 0x00) |
                                  (timing.v_polarity ? 0x01 : 0x00);
    const uint8_t pb2 = static_cast<uint8_t>((timing.aspect_ratio << 4) | 0x08);
    const uint8_t pb4 = timing.vic;
    const uint8_t pb0 = static_cast<uint8_t>(((pb2 + pb4) <= 0x5f) ? (0x5f - pb2 - pb4)
                                                                    : (0x15f - pb2 - pb4));

    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrAvi, 0x3c, 0x41), TAG, "LT8912 AVI setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0xab, sync_polarity), TAG, "LT8912 AVI setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrAvi, 0x43, pb0), TAG, "LT8912 AVI setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrAvi, 0x44, 0x10), TAG, "LT8912 AVI setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrAvi, 0x45, pb2), TAG, "LT8912 AVI setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrAvi, 0x46, 0x00), TAG, "LT8912 AVI setup failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrAvi, 0x47, pb4), TAG, "LT8912 AVI setup failed");
    return ESP_OK;
}

esp_err_t lt8912_mipi_rx_logic_reset(void)
{
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x03, 0x7f), TAG, "LT8912 logic reset failed");
    delay(10);
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x03, 0xff), TAG, "LT8912 logic reset failed");
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x05, 0xfb), TAG, "LT8912 DDS reset failed");
    delay(10);
    ESP_RETURN_ON_ERROR(lt8912_write(kLt8912AddrMain, 0x05, 0xff), TAG, "LT8912 DDS reset failed");
    return ESP_OK;
}

esp_err_t lt8912_hdmi_output(bool on)
{
    return lt8912_write(kLt8912AddrMain, 0x33, on ? 0x0e : 0x0c);
}

esp_err_t lt8912_detect_input(void)
{
    uint8_t hsync_l = 0;
    uint8_t hsync_h = 0;
    uint8_t vsync_l = 0;
    uint8_t vsync_h = 0;

    ESP_RETURN_ON_ERROR(lt8912_read(kLt8912AddrMain, 0x9c, &hsync_l), TAG, "LT8912 detect input failed");
    ESP_RETURN_ON_ERROR(lt8912_read(kLt8912AddrMain, 0x9d, &hsync_h), TAG, "LT8912 detect input failed");
    ESP_RETURN_ON_ERROR(lt8912_read(kLt8912AddrMain, 0x9e, &vsync_l), TAG, "LT8912 detect input failed");
    ESP_RETURN_ON_ERROR(lt8912_read(kLt8912AddrMain, 0x9f, &vsync_h), TAG, "LT8912 detect input failed");

    Serial.printf("[bsp] LT8912 MIPI input sync h=%02X%02X v=%02X%02X\n",
                  hsync_h, hsync_l, vsync_h, vsync_l);
    return ESP_OK;
}

bool lt8912_is_hpd_ready(void)
{
    uint8_t data = 0;
    if (lt8912_read(kLt8912AddrMain, 0xc1, &data) != ESP_OK) {
        return false;
    }
    return (data & 0x80U) != 0;
}

esp_err_t lt8912_create_ios(void)
{
    if (!g_i2c_ready || g_lt8912_i2c == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    Serial.printf("[bsp] LT8912 register access bound to Arduino Wire on %s I2C bus\n", g_lt8912_i2c_tag);
    return ESP_OK;
}

esp_err_t lt8912_init_bridge(void)
{
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrMain,
                                           kCmdDigitalClockEn,
                                           sizeof(kCmdDigitalClockEn) / sizeof(kCmdDigitalClockEn[0])),
                        TAG, "LT8912 digital clock init failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrMain,
                                           kCmdTxAnalog,
                                           sizeof(kCmdTxAnalog) / sizeof(kCmdTxAnalog[0])),
                        TAG, "LT8912 tx analog init failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrMain,
                                           kCmdCbusAnalog,
                                           sizeof(kCmdCbusAnalog) / sizeof(kCmdCbusAnalog[0])),
                        TAG, "LT8912 cbus init failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrMain,
                                           kCmdHdmiPllAnalog,
                                           sizeof(kCmdHdmiPllAnalog) / sizeof(kCmdHdmiPllAnalog[0])),
                        TAG, "LT8912 pll init failed");
    ESP_RETURN_ON_ERROR(lt8912_send_mipi_analog(false), TAG, "LT8912 MIPI analog failed");
    ESP_RETURN_ON_ERROR(lt8912_send_mipi_basic(2, false), TAG, "LT8912 MIPI basic failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrCec,
                                           kCmdDdsConfig,
                                           sizeof(kCmdDdsConfig) / sizeof(kCmdDdsConfig[0])),
                        TAG, "LT8912 DDS init failed");
    ESP_RETURN_ON_ERROR(lt8912_send_video_setup(kLt8912Timing720p), TAG, "LT8912 video timing failed");
    ESP_RETURN_ON_ERROR(lt8912_detect_input(), TAG, "LT8912 input detect failed");
    ESP_RETURN_ON_ERROR(lt8912_send_video_setup(kLt8912Timing720p), TAG, "LT8912 video timing failed");
    ESP_RETURN_ON_ERROR(lt8912_send_avi_infoframe(kLt8912Timing720p), TAG, "LT8912 AVI infoframe failed");
    ESP_RETURN_ON_ERROR(lt8912_mipi_rx_logic_reset(), TAG, "LT8912 logic reset failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrMain,
                                           kCmdAudioIisMode,
                                           sizeof(kCmdAudioIisMode) / sizeof(kCmdAudioIisMode[0])),
                        TAG, "LT8912 audio mode failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrAvi,
                                           kCmdAudioIisEn,
                                           sizeof(kCmdAudioIisEn) / sizeof(kCmdAudioIisEn[0])),
                        TAG, "LT8912 audio enable failed");
    ESP_RETURN_ON_ERROR(lt8912_write_array(kLt8912AddrMain,
                                           kCmdLvdsBypass,
                                           sizeof(kCmdLvdsBypass) / sizeof(kCmdLvdsBypass[0])),
                        TAG, "LT8912 bypass init failed");
    ESP_RETURN_ON_ERROR(lt8912_hdmi_output(true), TAG, "LT8912 HDMI enable failed");
    return ESP_OK;
}

} // namespace

esp_err_t bsp_i2c_init(void)
{
    if (g_i2c_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_i2c_bus(*g_i2c,
                                     &g_i2c_ready,
                                     BOARD_I2C_SDA,
                                     BOARD_I2C_SCL,
                                     kI2cClockHz,
                                     kI2cTimeoutMs),
                        TAG,
                        "I2C master bus init failed");

    Serial.printf("[bsp] I2C ready on SDA=%d SCL=%d @ %lu Hz\n",
                  BOARD_I2C_SDA,
                  BOARD_I2C_SCL,
                  (unsigned long)kI2cClockHz);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    // Arduino Wire keeps the controller internal, so the BSP-style handle is unavailable in this phase.
    return nullptr;
}

esp_err_t bsp_hdmi_power_on(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(ensure_pca9535(), TAG, "PCA9535 init failed");

    ESP_RETURN_ON_ERROR(pca_set_output(BOARD_PCA_12_1V8_EN, true), TAG, "1V8 enable failed");
    delay(10);
    ESP_RETURN_ON_ERROR(pca_set_output(BOARD_PCA_07_HDMI_EN, true), TAG, "HDMI enable failed");
    delay(10);
    ESP_RETURN_ON_ERROR(pca_set_output(BOARD_PCA_06_HDMI_RST, false), TAG, "HDMI reset low failed");
    delay(50);
    ESP_RETURN_ON_ERROR(pca_set_output(BOARD_PCA_06_HDMI_RST, true), TAG, "HDMI reset high failed");
    delay(kHdmiPowerStableDelayMs);

    pinMode(BOARD_HDMI_INT, INPUT);

    ESP_RETURN_ON_ERROR(select_lt8912_i2c_bus(), TAG, "LT8912 bus select failed");
    Serial.printf("[bsp] HDMI rails enabled, LT8912 probe path selected: %s\n", g_lt8912_i2c_tag);
    return ESP_OK;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    if (ret_panel) {
        *ret_panel = nullptr;
    }
    if (ret_io) {
        *ret_io = nullptr;
    }
    if (config == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->hdmi_resolution != BSP_HDMI_RES_1280x720) {
        Serial.printf("[bsp] current BSP only supports HDMI 1280x720, got %d\n",
                      static_cast<int>(config->hdmi_resolution));
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (g_display_panel != nullptr) {
        if (ret_panel) {
            *ret_panel = g_display_panel;
        }
        if (ret_io) {
            *ret_io = nullptr;
        }
        return ESP_OK;
    }

    Serial.printf("[bsp] display init: %ux%u, DSI lanes=%u, bitrate=%lu Mbps\n",
                  BSP_LCD_H_RES,
                  BSP_LCD_V_RES,
                  BSP_LCD_MIPI_DSI_LANE_NUM,
                  (unsigned long)config->dsi_bus.lane_bit_rate_mbps);

    ESP_RETURN_ON_ERROR(bsp_hdmi_power_on(), TAG, "HDMI rail enable failed");
    ESP_RETURN_ON_ERROR(lt8912_create_ios(), TAG, "LT8912 IO setup failed");

    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = config->dsi_bus.phy_clk_src,
        .lane_bit_rate_mbps = static_cast<float>(config->dsi_bus.lane_bit_rate_mbps),
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &g_dsi_bus), TAG, "DSI bus init failed");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = static_cast<float>(kLt8912Timing720p.pclk_mhz),
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = 1,
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_pulse_width = kLt8912Timing720p.hs,
            .hsync_back_porch = kLt8912Timing720p.hbp,
            .hsync_front_porch = kLt8912Timing720p.hfp,
            .vsync_pulse_width = kLt8912Timing720p.vs,
            .vsync_back_porch = kLt8912Timing720p.vbp,
            .vsync_front_porch = kLt8912Timing720p.vfp,
        },
        .flags = {
            .use_dma2d = 0,
            .disable_lp = 1,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(g_dsi_bus, &dpi_cfg, &g_display_panel),
                        TAG,
                        "DPI panel create failed");

    esp_err_t ret = lt8912_init_bridge();
    if (ret != ESP_OK) {
        bsp_display_delete();
        return ret;
    }

    ret = esp_lcd_panel_reset(g_display_panel);
    if (ret != ESP_OK) {
        bsp_display_delete();
        return ret;
    }

    ret = esp_lcd_panel_init(g_display_panel);
    if (ret != ESP_OK) {
        bsp_display_delete();
        return ret;
    }

    Serial.printf("[bsp] LT8912 HPD %s\n", lt8912_is_hpd_ready() ? "ready" : "not ready yet");

    if (ret_panel) {
        *ret_panel = g_display_panel;
    }
    if (ret_io) {
        *ret_io = nullptr;
    }
    return ESP_OK;
}

void bsp_display_delete(void)
{
    if (g_display_panel != nullptr) {
        esp_lcd_panel_del(g_display_panel);
        g_display_panel = nullptr;
    }
    if (g_dsi_bus != nullptr) {
        esp_lcd_del_dsi_bus(g_dsi_bus);
        g_dsi_bus = nullptr;
    }
    g_lt8912_i2c = g_i2c;
    g_lt8912_i2c_tag = "main";
}

esp_err_t bsp_sdcard_mount(void)
{
    if (g_sd_ready) {
        return ESP_OK;
    }

    SPI.begin(BOARD_SD_SCK, BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CS);
    if (!SD.begin(BOARD_SD_CS, SPI, kSdClockHz, BSP_SD_MOUNT_POINT)) {
        Serial.printf("[bsp] SD mount failed on CS=%d\n", BOARD_SD_CS);
        return ESP_FAIL;
    }

    g_sd_ready = true;
    Serial.printf("[bsp] SD mounted over SPI at %s\n", BSP_SD_MOUNT_POINT);
    return ESP_OK;
}

sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    return g_sdmmc_card;
}

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (g_i2s_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &g_i2s_tx, &g_i2s_rx), TAG, "I2S channel alloc failed");

    const i2s_std_config_t default_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = static_cast<gpio_num_t>(BOARD_ES8311_I2S_MCLK),
            .bclk = static_cast<gpio_num_t>(BOARD_ES8311_I2S_SCLK),
            .ws = static_cast<gpio_num_t>(BOARD_ES8311_I2S_LRCK),
            .dout = static_cast<gpio_num_t>(BOARD_ES8311_I2S_DSDIN),
            .din = static_cast<gpio_num_t>(BOARD_ES8311_I2S_ASDOUT),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    const i2s_std_config_t *cfg = i2s_config ? i2s_config : &default_cfg;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_tx, cfg), TAG, "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_tx), TAG, "I2S TX enable failed");

    if (g_i2s_rx != nullptr) {
        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_rx, cfg), TAG, "I2S RX init failed");
        ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_rx), TAG, "I2S RX enable failed");
    }

    g_i2s_ready = true;
    Serial.println("[bsp] I2S clocks prepared for ES8311");
    return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        Serial.printf("[bsp] I2C init failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }

    err = ensure_pca9535();
    if (err != ESP_OK) {
        Serial.printf("[bsp] PCA9535 init failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }

    err = bsp_audio_init(nullptr);
    if (err != ESP_OK) {
        Serial.printf("[bsp] I2S init failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }

    err = pca_set_output(BOARD_PCA_05_SHUTDOWN, true);
    if (err != ESP_OK) {
        Serial.printf("[bsp] ES8311 shutdown pin enable failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }
    delay(10);

    err = ensure_es8311();
    if (err != ESP_OK) {
        Serial.printf("[bsp] ES8311 add device failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }

    const bool es8311_present = probe_i2c_device(BOARD_I2C_ADDR_ES8311);
    Serial.printf("[bsp] ES8311 @ 0x%02X %s\n",
                  BOARD_I2C_ADDR_ES8311,
                  es8311_present ? "detected" : "not detected");
    Serial.println("[bsp] current phase keeps codec handle as null until esp_codec_dev is wired in");
    return nullptr;
}
