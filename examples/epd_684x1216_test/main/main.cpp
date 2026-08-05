#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "M5GFX.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"
#include "t5_p4_board.h"

namespace {

constexpr char kTag[] = "epd_684x1216_test";

// The source driver shifts 1216 pixels per line and the gate driver scans 684 lines.
// M5GFX rotates this native landscape buffer into the panel's 684 x 1216 portrait view.
constexpr int kNativeWidth = 1216;
constexpr int kNativeHeight = 684;
constexpr uint8_t kPortraitRotation = 1;
constexpr int kEpdBusSpeedHz = 20000000;
constexpr int kDummyClocksPerLine = 10;
constexpr int kLinePaddingBytes = kDummyClocksPerLine * 2;
constexpr int kVcomMillivolts = -1600;

static_assert((kNativeWidth % 16) == 0, "Panel_EPD requires a width divisible by 16");

constexpr gpio_num_t kEpdD0 = GPIO_NUM_27;
constexpr gpio_num_t kEpdD1 = GPIO_NUM_28;
constexpr gpio_num_t kEpdD2 = GPIO_NUM_29;
constexpr gpio_num_t kEpdD3 = GPIO_NUM_30;
constexpr gpio_num_t kEpdD4 = GPIO_NUM_31;
constexpr gpio_num_t kEpdD5 = GPIO_NUM_32;
constexpr gpio_num_t kEpdD6 = GPIO_NUM_33;
constexpr gpio_num_t kEpdD7 = GPIO_NUM_34;
constexpr gpio_num_t kEpdD8 = GPIO_NUM_50;
constexpr gpio_num_t kEpdD9 = GPIO_NUM_49;
constexpr gpio_num_t kEpdD10 = GPIO_NUM_23;
constexpr gpio_num_t kEpdD11 = GPIO_NUM_22;
constexpr gpio_num_t kEpdD12 = GPIO_NUM_11;
constexpr gpio_num_t kEpdD13 = GPIO_NUM_21;
constexpr gpio_num_t kEpdD14 = GPIO_NUM_20;
constexpr gpio_num_t kEpdD15 = GPIO_NUM_2;

// The real OE and power controls are on PCA9535. GPIO51 only fills the matching
// M5GFX config fields and is not wired to the panel.
constexpr gpio_num_t kI80DummyDc = GPIO_NUM_51;
constexpr gpio_num_t kFrontlight1 = GPIO_NUM_53;
constexpr gpio_num_t kFrontlight2 = GPIO_NUM_54;

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom1 = 0x03;
constexpr uint8_t kTpsRegVcom2 = 0x04;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsRegRevision = 0x10;
constexpr uint8_t kTpsEnableAllRails = 0x3F;
constexpr uint8_t kTpsVcom2Reserved = 0x04;
constexpr uint8_t kTpsPowerGoodMask = 0xFA;
constexpr uint8_t kTpsPowerGoodExpected = 0xFA;
constexpr uint8_t kTpsPanelRailsMask = 0x5A;
constexpr int kPowerGoodTimeoutMs = 400;
constexpr int kPowerDownTimeoutMs = 150;

i2c_master_dev_handle_t s_tps = nullptr;

esp_err_t ensure_tps_device()
{
    if (s_tps != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != nullptr, ESP_ERR_INVALID_STATE, kTag, "shared I2C bus is not initialized");

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = T5_BOARD_I2C_ADDR_TPS651851;
    dev_cfg.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
    return i2c_master_bus_add_device(bus, &dev_cfg, &s_tps);
}

esp_err_t tps_write(uint8_t reg, const uint8_t *data, size_t size)
{
    ESP_RETURN_ON_ERROR(ensure_tps_device(), kTag, "create TPS651851 I2C device failed");
    uint8_t buffer[4] = {reg, 0, 0, 0};
    ESP_RETURN_ON_FALSE(size <= sizeof(buffer) - 1, ESP_ERR_INVALID_ARG, kTag, "TPS write too long");
    if (size > 0) {
        memcpy(&buffer[1], data, size);
    }
    return i2c_master_transmit(s_tps, buffer, size + 1, 100);
}

esp_err_t tps_write_u8(uint8_t reg, uint8_t value)
{
    return tps_write(reg, &value, 1);
}

esp_err_t tps_read_u8(uint8_t reg, uint8_t *value)
{
    ESP_RETURN_ON_ERROR(ensure_tps_device(), kTag, "create TPS651851 I2C device failed");
    return i2c_master_transmit_receive(s_tps, &reg, 1, value, 1, 100);
}

struct PcaPowerState {
    bool epd_oe;
    bool epd_mode;
    bool tps_wakeup;
    bool tps_pwrup;
    bool vcom_ctrl;
    bool power_good;
    bool interrupt;
};

esp_err_t read_pca_power_state(PcaPowerState *state)
{
    ESP_RETURN_ON_FALSE(state != nullptr, ESP_ERR_INVALID_ARG, kTag, "null PCA state");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_EPD_OE, &state->epd_oe),
                        kTag, "read EPD OE failed");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_EPD_MODE, &state->epd_mode),
                        kTag, "read EPD MODE failed");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_TPS_WAKEUP, &state->tps_wakeup),
                        kTag, "read TPS WAKEUP failed");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_TPS_PWRUP, &state->tps_pwrup),
                        kTag, "read TPS PWRUP failed");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_VCOM_CTRL, &state->vcom_ctrl),
                        kTag, "read VCOM CTRL failed");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_TPS_PWR_GOOD, &state->power_good),
                        kTag, "read TPS PWR_GOOD failed");
    return t5_board_pca9535_get_level(T5_BOARD_PCA_IO_TPS_INT, &state->interrupt);
}

void log_pca_power_state(const char *stage)
{
    PcaPowerState state = {};
    const esp_err_t err = read_pca_power_state(&state);
    if (err != ESP_OK) {
        ESP_LOGE("EPD_DIAG", "PCA %s read failed: %s", stage, esp_err_to_name(err));
        return;
    }
    ESP_LOGI("EPD_DIAG",
             "PCA %s: OE=%u MODE=%u WAKEUP=%u PWRUP=%u VCOM_CTRL=%u PWR_GOOD=%u INT=%u",
             stage, state.epd_oe, state.epd_mode, state.tps_wakeup,
             state.tps_pwrup, state.vcom_ctrl, state.power_good, state.interrupt);
}

void log_tps_power_state(const char *stage)
{
    uint8_t enable = 0;
    uint8_t vcom1 = 0;
    uint8_t vcom2 = 0;
    uint8_t power_good = 0;
    uint8_t revision = 0;
    esp_err_t err = tps_read_u8(kTpsRegEnable, &enable);
    if (err == ESP_OK) {
        err = tps_read_u8(kTpsRegVcom1, &vcom1);
    }
    if (err == ESP_OK) {
        err = tps_read_u8(kTpsRegVcom2, &vcom2);
    }
    if (err == ESP_OK) {
        err = tps_read_u8(kTpsRegPowerGood, &power_good);
    }
    if (err == ESP_OK) {
        err = tps_read_u8(kTpsRegRevision, &revision);
    }
    if (err != ESP_OK) {
        ESP_LOGE("EPD_DIAG", "TPS %s read failed: %s", stage, esp_err_to_name(err));
        return;
    }

    const int vcom_mv = -10 * (vcom1 | ((vcom2 & 0x01U) << 8));
    ESP_LOGI("EPD_DIAG",
             "TPS %s: ENABLE=0x%02X VCOM1=0x%02X VCOM2=0x%02X (%d mV) "
             "PG=0x%02X [VB=%u VDDH=%u VN=%u VPOS=%u VEE=%u VNEG=%u] REVID=0x%02X",
             stage, enable, vcom1, vcom2, vcom_mv, power_good,
             (power_good >> 7) & 1U, (power_good >> 6) & 1U,
             (power_good >> 5) & 1U, (power_good >> 4) & 1U,
             (power_good >> 3) & 1U, (power_good >> 1) & 1U,
             revision);
}

void log_heap(const char *stage)
{
    ESP_LOGI(kTag, "%s heap: internal=%u psram=%u",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

esp_err_t keep_frontlight_off()
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << kFrontlight1) | (1ULL << kFrontlight2);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "configure frontlight pins failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(kFrontlight1, 0), kTag, "disable frontlight 1 failed");
    return gpio_set_level(kFrontlight2, 0);
}

class T5P4EpdBus : public lgfx::Bus_EPD {
public:
    bool init() override
    {
        if (bsp_i2c_init() != ESP_OK) {
            ESP_LOGE(kTag, "I2C init failed");
            return false;
        }
        if (t5_board_pca9535_init() != ESP_OK) {
            ESP_LOGE(kTag, "PCA9535 init failed");
            return false;
        }
        if (ensure_tps_device() != ESP_OK) {
            ESP_LOGE(kTag, "TPS651851 init failed");
            return false;
        }

        log_pca_power_state("after-init");

        const bool ok = lgfx::Bus_EPD::init();
        if (!ok) {
            ESP_LOGE(kTag, "M5GFX EPD bus init failed");
        }
        return ok;
    }

    bool powerControl(bool power_on) override
    {
        if (_pwr_on == power_on) {
            return true;
        }

        wait();
        const esp_err_t err = power_on ? power_on_sequence() : power_off_sequence();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "EPD power %s failed: %s", power_on ? "on" : "off", esp_err_to_name(err));
            if (power_on) {
                power_off_sequence();
            }
            return false;
        }

        _pwr_on = power_on;
        return true;
    }

private:
    esp_err_t set_expander(uint8_t io, bool level)
    {
        return t5_board_pca9535_set_level(io, level);
    }

    esp_err_t wait_pca_power_good()
    {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            bool level = false;
            ESP_RETURN_ON_ERROR(t5_board_pca9535_get_level(T5_BOARD_PCA_IO_TPS_PWR_GOOD, &level),
                                kTag, "read TPS PWR_GOOD failed");
            if (level) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t wait_tps_power_good()
    {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            uint8_t pg = 0;
            ESP_RETURN_ON_ERROR(tps_read_u8(kTpsRegPowerGood, &pg), kTag, "read TPS PG failed");
            if ((pg & kTpsPowerGoodMask) == kTpsPowerGoodExpected) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t wait_tps_power_down()
    {
        for (int i = 0; i < kPowerDownTimeoutMs; ++i) {
            uint8_t pg = 0;
            ESP_RETURN_ON_ERROR(tps_read_u8(kTpsRegPowerGood, &pg), kTag, "read TPS PG failed");
            // PWR_GOOD monitors the four panel rails. VB/VN may remain in
            // regulation while TPS651851 is awake in standby mode.
            if ((pg & kTpsPanelRailsMask) == 0) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t power_on_sequence()
    {
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_OE, false), kTag, "disable EPD OE failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_MODE, true), kTag, "set EPD MODE failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, false), kTag, "clear TPS PWRUP failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, false), kTag, "clear VCOM CTRL failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_WAKEUP, true), kTag, "set TPS WAKEUP failed");
        vTaskDelay(pdMS_TO_TICKS(1));

        log_pca_power_state("standby");

        const int vcom = std::clamp(kVcomMillivolts / -10, 0, 0x01FF);
        const uint8_t vcom_data[2] = {
            static_cast<uint8_t>(vcom & 0xFF),
            static_cast<uint8_t>(((vcom >> 8) & 0x01) | kTpsVcom2Reserved),
        };
        ESP_RETURN_ON_ERROR(tps_write(kTpsRegVcom1, vcom_data, sizeof(vcom_data)), kTag, "set TPS VCOM failed");
        ESP_RETURN_ON_ERROR(tps_write_u8(kTpsRegEnable, kTpsEnableAllRails), kTag, "enable TPS rails failed");
        log_tps_power_state("configured");

        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, true), kTag, "set TPS PWRUP failed");
        ESP_RETURN_ON_ERROR(wait_pca_power_good(), kTag, "PCA TPS PWR_GOOD timeout");
        ESP_RETURN_ON_ERROR(wait_tps_power_good(), kTag, "TPS PG timeout");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, true), kTag, "set VCOM CTRL failed");
        vTaskDelay(pdMS_TO_TICKS(1));
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_OE, true), kTag, "enable EPD OE failed");
        log_pca_power_state("rails-ready");
        log_tps_power_state("rails-ready");
        return ESP_OK;
    }

    esp_err_t power_off_sequence()
    {
        log_pca_power_state("before-power-off");
        log_tps_power_state("before-power-off");

        esp_err_t err = set_expander(T5_BOARD_PCA_IO_EPD_OE, false);
        esp_rom_delay_us(20);  // Datasheet requires at least 12 us before VPOS/VNEG turn off.
        err |= set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, false);
        err |= set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, false);
        err |= set_expander(T5_BOARD_PCA_IO_EPD_MODE, false);
        const esp_err_t down_err = wait_tps_power_down();
        if (down_err != ESP_OK) {
            ESP_LOGW("EPD_DIAG", "TPS power-down status failed after %d ms: %s",
                     kPowerDownTimeoutMs, esp_err_to_name(down_err));
        }
        log_tps_power_state("rails-off");
        err |= set_expander(T5_BOARD_PCA_IO_TPS_WAKEUP, false);
        log_pca_power_state("powered-off");
        return err;
    }
};

class T5P4M5GFX : public lgfx::LGFX_Device {
public:
    T5P4M5GFX()
    {
        auto bus_cfg = bus_.config();
        bus_cfg.bus_speed = kEpdBusSpeedHz;
        bus_cfg.pin_data[0] = kEpdD0;
        bus_cfg.pin_data[1] = kEpdD1;
        bus_cfg.pin_data[2] = kEpdD2;
        bus_cfg.pin_data[3] = kEpdD3;
        bus_cfg.pin_data[4] = kEpdD4;
        bus_cfg.pin_data[5] = kEpdD5;
        bus_cfg.pin_data[6] = kEpdD6;
        bus_cfg.pin_data[7] = kEpdD7;
        bus_cfg.pin_data[8] = kEpdD8;
        bus_cfg.pin_data[9] = kEpdD9;
        bus_cfg.pin_data[10] = kEpdD10;
        bus_cfg.pin_data[11] = kEpdD11;
        bus_cfg.pin_data[12] = kEpdD12;
        bus_cfg.pin_data[13] = kEpdD13;
        bus_cfg.pin_data[14] = kEpdD14;
        bus_cfg.pin_data[15] = kEpdD15;
        bus_cfg.pin_pwr = kI80DummyDc;
        bus_cfg.pin_spv = T5_BOARD_EPD_STV;
        bus_cfg.pin_ckv = T5_BOARD_EPD_CKV;
        bus_cfg.pin_sph = T5_BOARD_EPD_STH;
        bus_cfg.pin_oe = kI80DummyDc;
        bus_cfg.pin_le = T5_BOARD_EPD_LEH;
        bus_cfg.pin_cl = T5_BOARD_EPD_CKH;
        bus_cfg.bus_width = 16;
        bus_.config(bus_cfg);

        panel_.setBus(&bus_);

        auto panel_cfg = panel_.config();
        panel_cfg.memory_width = kNativeWidth;
        panel_cfg.memory_height = kNativeHeight;
        panel_cfg.panel_width = kNativeWidth;
        panel_cfg.panel_height = kNativeHeight;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = 0;
        panel_cfg.bus_shared = false;
        panel_.config(panel_cfg);

        auto detail = panel_.config_detail();
        detail.line_padding = kLinePaddingBytes;
        detail.task_priority = 3;
        panel_.config_detail(detail);

        setPanel(&panel_);
    }

private:
    T5P4EpdBus bus_;
    lgfx::Panel_EPD panel_;
};

T5P4M5GFX display;

uint32_t gray(uint8_t value)
{
    return display.color888(value, value, value);
}

void draw_gray_ramp(int x, int y, int width, int height)
{
    const int swatch_width = width / 16;
    for (int i = 0; i < 16; ++i) {
        const int left = x + i * swatch_width;
        const int right = (i == 15) ? x + width : left + swatch_width;
        const uint8_t level = static_cast<uint8_t>(i * 17);
        display.fillRect(left, y, right - left, height, gray(level));
        display.drawRect(left, y, right - left, height, TFT_BLACK);
        display.setTextColor(i < 8 ? TFT_WHITE : TFT_BLACK, gray(level));
        display.setTextSize(1);
        display.setCursor(left + 5, y + height / 2 - 4);
        display.printf("%X", i);
    }
}

void draw_checkerboard(int x, int y, int width, int height, int cell)
{
    for (int row = 0; row < height / cell; ++row) {
        for (int col = 0; col < width / cell; ++col) {
            display.fillRect(x + col * cell, y + row * cell, cell, cell,
                             ((row + col) & 1) ? TFT_BLACK : TFT_WHITE);
        }
    }
    display.drawRect(x, y, width, height, TFT_BLACK);
}

void draw_line_test(int x, int y, int width, int height)
{
    display.drawRect(x, y, width, height, TFT_BLACK);
    for (int i = 1; i < width; i += 8) {
        display.drawFastVLine(x + i, y + 1, height - 2, (i & 8) ? TFT_BLACK : gray(128));
    }
    for (int i = 1; i < height; i += 8) {
        display.drawFastHLine(x + 1, y + i, width - 2, (i & 8) ? TFT_BLACK : gray(128));
    }
    for (int i = 0; i < 12; ++i) {
        display.drawLine(x + 2, y + height - 3 - i * 5,
                         x + width - 3, y + 2 + i * 5, gray(static_cast<uint8_t>(i * 20)));
    }
}

void draw_orientation_marks(int width, int height)
{
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 8);
    display.print("TL");
    display.setCursor(width - 32, 8);
    display.print("TR");
    display.setCursor(8, height - 24);
    display.print("BL");
    display.setCursor(width - 32, height - 24);
    display.print("BR");
}

void draw_test_pattern()
{
    display.setRotation(kPortraitRotation);
    display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    display.setTextWrap(false);
    display.fillScreen(TFT_WHITE);

    const int width = display.width();
    const int height = display.height();
    display.drawRect(0, 0, width, height, TFT_BLACK);
    display.drawRect(3, 3, width - 6, height - 6, TFT_BLACK);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(3);
    display.setCursor(76, 24);
    display.print("T5-P4 16-bit EPD TEST");
    display.setTextSize(1);
    display.setCursor(76, 62);
    display.printf("E0470A01-AF-CF  %d x %d portrait  I80 %d MHz  VCOM %.2f V",
                   width, height, kEpdBusSpeedHz / 1000000, kVcomMillivolts / 1000.0);
    display.drawFastHLine(20, 88, width - 40, TFT_BLACK);

    display.setTextSize(2);
    display.setCursor(24, 104);
    display.print("16-LEVEL GRAYSCALE");
    draw_gray_ramp(24, 134, width - 48, 180);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(24, 340);
    display.print("CHECKERBOARD");
    display.setCursor(360, 340);
    display.print("LINE / CLOCK TEST");
    draw_checkerboard(24, 372, 288, 240, 12);
    draw_line_test(360, 372, width - 384, 240);

    display.setTextSize(2);
    display.setCursor(24, 642);
    display.print("GEOMETRY");
    display.drawRect(24, 674, width - 48, 170, TFT_BLACK);
    display.fillRect(48, 702, 120, 56, gray(64));
    display.fillRect(48, 770, 120, 46, gray(192));
    display.drawCircle(258, 758, 64, TFT_BLACK);
    display.fillCircle(410, 758, 64, gray(112));
    display.drawTriangle(520, 816, 610, 688, 650, 816, TFT_BLACK);

    display.setCursor(24, 874);
    display.print("1-PIXEL RESOLUTION / DATA ORDER");
    display.drawRect(24, 906, width - 48, 208, TFT_BLACK);
    for (int x = 40; x < width / 2; x += 2) {
        display.drawFastVLine(x, 922, 176, TFT_BLACK);
    }
    for (int y = 922; y < 1098; y += 2) {
        display.drawFastHLine(width / 2 + 16, y, width / 2 - 56, TFT_BLACK);
    }

    display.setTextSize(1);
    display.setCursor(76, height - 68);
    display.printf("Native scan: %d x %d | 16-bit D0..D15 | %d dummy clocks/line | padding %d bytes",
                   kNativeWidth, kNativeHeight, kDummyClocksPerLine, kLinePaddingBytes);
    display.setCursor(76, height - 50);
    display.print("Corner labels must read TL/TR/BL/BR in their physical corners.");

    draw_orientation_marks(width, height);
}

void condition_panel_to_white()
{
    display.setRotation(kPortraitRotation);
    display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    display.fillScreen(TFT_WHITE);
    ESP_LOGI(kTag, "conditioning panel with a full white refresh");
    display.display();
    display.waitDisplay();
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "starting E0470A01-AF-CF 684x1216 M5GFX test");
    ESP_LOGI(kTag, "native scan=%dx%d, 16-bit bus=%d MHz, line padding=%d bytes",
             kNativeWidth, kNativeHeight, kEpdBusSpeedHz / 1000000, kLinePaddingBytes);
    ESP_LOGW("EPD_DIAG",
             "D0..D15/CKH/XSTL/XLE/CKV/STV are output-only; log cannot prove fly-wire continuity at the FPC");
    log_heap("before init");

    if (keep_frontlight_off() != ESP_OK) {
        ESP_LOGE(kTag, "failed to force frontlight off");
        return;
    }

    if (!display.init_without_reset(false)) {
        ESP_LOGE(kTag, "display init failed");
        return;
    }

    log_heap("after init");
    condition_panel_to_white();
    draw_test_pattern();
    ESP_LOGI(kTag, "drawing size after rotation: %dx%d",
             static_cast<int>(display.width()), static_cast<int>(display.height()));
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
    log_heap("after refresh");

    ESP_LOGI(kTag, "test pattern rendered; EPD rails are off");
}
