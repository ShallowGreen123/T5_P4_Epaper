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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"
#include "t5_p4_board.h"

namespace {

constexpr char kTag[] = "m5gfx_epd_test";

constexpr int kPanelWidth = 1440;
constexpr int kPanelHeight = 720;
constexpr int kEpdBusSpeedHz = 40000000;
constexpr int kVcomMillivolts = -1600;
constexpr uint8_t kPanelMirrorXRotation = 6;

constexpr gpio_num_t kEpdD0 = GPIO_NUM_27;
constexpr gpio_num_t kEpdD1 = GPIO_NUM_28;
constexpr gpio_num_t kEpdD2 = GPIO_NUM_29;
constexpr gpio_num_t kEpdD3 = GPIO_NUM_30;
constexpr gpio_num_t kEpdD4 = GPIO_NUM_31;
constexpr gpio_num_t kEpdD5 = GPIO_NUM_32;
constexpr gpio_num_t kEpdD6 = GPIO_NUM_33;
constexpr gpio_num_t kEpdD7 = GPIO_NUM_34;
constexpr gpio_num_t kDummyDc = GPIO_NUM_22;

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsEnableAllRails = 0x3F;
constexpr uint8_t kTpsPowerGoodMask = 0xFA;
constexpr uint8_t kTpsPowerGoodExpected = 0xFA;
constexpr int kPowerGoodTimeoutMs = 400;

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

void log_heap(const char *stage)
{
    ESP_LOGI(kTag, "%s heap: internal=%u psram=%u",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
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

    esp_err_t power_on_sequence()
    {
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_OE, true), kTag, "set EPD OE failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_EPD_MODE, true), kTag, "set EPD MODE failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_WAKEUP, true), kTag, "set TPS WAKEUP failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, true), kTag, "set TPS PWRUP failed");
        ESP_RETURN_ON_ERROR(set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, true), kTag, "set VCOM CTRL failed");
        vTaskDelay(pdMS_TO_TICKS(1));

        ESP_RETURN_ON_ERROR(wait_pca_power_good(), kTag, "PCA TPS PWR_GOOD timeout");
        ESP_RETURN_ON_ERROR(tps_write_u8(kTpsRegEnable, kTpsEnableAllRails), kTag, "enable TPS rails failed");

        const int vcom = std::clamp(kVcomMillivolts / -10, 0, 0xFFFF);
        const uint8_t vcom_data[2] = {
            static_cast<uint8_t>(vcom & 0xFF),
            static_cast<uint8_t>((vcom >> 8) & 0xFF),
        };
        ESP_RETURN_ON_ERROR(tps_write(kTpsRegVcom, vcom_data, sizeof(vcom_data)), kTag, "set TPS VCOM failed");
        ESP_RETURN_ON_ERROR(wait_tps_power_good(), kTag, "TPS PG timeout");
        return ESP_OK;
    }

    esp_err_t power_off_sequence()
    {
        esp_err_t err = ESP_OK;
        err |= set_expander(T5_BOARD_PCA_IO_EPD_OE, false);
        err |= set_expander(T5_BOARD_PCA_IO_EPD_MODE, false);
        err |= set_expander(T5_BOARD_PCA_IO_TPS_PWRUP, false);
        err |= set_expander(T5_BOARD_PCA_IO_VCOM_CTRL, false);
        vTaskDelay(pdMS_TO_TICKS(1));
        err |= set_expander(T5_BOARD_PCA_IO_TPS_WAKEUP, false);
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
        bus_cfg.pin_pwr = kDummyDc;
        bus_cfg.pin_spv = T5_BOARD_EPD_STV;
        bus_cfg.pin_ckv = T5_BOARD_EPD_CKV;
        bus_cfg.pin_sph = T5_BOARD_EPD_STH;
        bus_cfg.pin_oe = kDummyDc;
        bus_cfg.pin_le = T5_BOARD_EPD_LEH;
        bus_cfg.pin_cl = T5_BOARD_EPD_CKH;
        bus_cfg.bus_width = 8;
        bus_.config(bus_cfg);

        panel_.setBus(&bus_);

        auto panel_cfg = panel_.config();
        panel_cfg.memory_width = kPanelWidth;
        panel_cfg.memory_height = kPanelHeight;
        panel_cfg.panel_width = kPanelWidth;
        panel_cfg.panel_height = kPanelHeight;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = kPanelMirrorXRotation;
        panel_cfg.bus_shared = false;
        panel_.config(panel_cfg);

        auto detail = panel_.config_detail();
        detail.line_padding = 0;
        detail.task_priority = 3;
        panel_.config_detail(detail);

        setPanel(&panel_);
    }

private:
    T5P4EpdBus bus_;
    lgfx::Panel_EPD panel_;
};

T5P4M5GFX display;

void draw_gray_steps()
{
    const int margin = 48;
    const int swatch_width = (display.width() - margin * 2) / 16;
    const int top = 152;
    const int height = 160;

    for (int i = 0; i < 16; ++i) {
        const int x = margin + i * swatch_width;
        const uint8_t gray = static_cast<uint8_t>(i * 17);
        display.fillRect(x, top, swatch_width - 3, height, display.color888(gray, gray, gray));
        display.drawRect(x, top, swatch_width - 3, height, TFT_BLACK);
        display.setTextColor(i < 8 ? TFT_WHITE : TFT_BLACK, display.color888(gray, gray, gray));
        display.setCursor(x + 12, top + height / 2 - 8);
        display.printf("%02d", i);
    }
}

void draw_test_pattern()
{
    display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    display.setTextWrap(false);
    display.fillScreen(TFT_WHITE);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(3);
    display.setCursor(48, 48);
    display.print("T5-P4 M5GFX EPD Test");

    display.setTextSize(2);
    display.setCursor(52, 96);
    display.printf("Panel: %d x %d  Bus: 8-bit I80 @ %d MHz  Waveform: ED047TC1 style",
                   static_cast<int>(display.width()), static_cast<int>(display.height()), kEpdBusSpeedHz / 1000000);

    draw_gray_steps();

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(52, 356);
    display.print("Checkerboard, diagonal lines, rectangles, circles, and text should all be sharp.");

    for (int y = 420; y < 620; y += 24) {
        for (int x = 56; x < 456; x += 24) {
            display.fillRect(x, y, 24, 24, ((x + y) / 24) & 1 ? TFT_BLACK : TFT_WHITE);
            display.drawRect(x, y, 24, 24, TFT_BLACK);
        }
    }

    display.drawRect(512, 420, 320, 200, TFT_BLACK);
    display.fillRect(536, 444, 112, 64, display.color888(64, 64, 64));
    display.fillRect(672, 444, 112, 64, display.color888(176, 176, 176));
    display.drawCircle(608, 558, 52, TFT_BLACK);
    display.fillCircle(736, 558, 52, display.color888(96, 96, 96));

    for (int i = 0; i < 12; ++i) {
        display.drawLine(888, 616 - i * 14, 1360, 424 + i * 14, display.color888(i * 20, i * 20, i * 20));
    }
    display.drawRect(888, 420, 472, 200, TFT_BLACK);

    display.setCursor(52, 660);
    display.setTextSize(2);
    display.print("M5GFX Panel_EPD + T5-P4 PCA9535/TPS651851 power sequence");
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "starting M5GFX EPD test");
    log_heap("before init");

    if (!display.init_without_reset(false)) {
        ESP_LOGE(kTag, "display init failed");
        return;
    }

    log_heap("after init");
    draw_test_pattern();
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
    log_heap("after refresh");

    ESP_LOGI(kTag, "test pattern rendered");
}
