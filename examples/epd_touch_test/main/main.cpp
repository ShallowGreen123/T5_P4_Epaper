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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"
#include "t5_p4_board.h"

namespace {

constexpr char kTag[] = "m5gfx_epd_touch_test";

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

constexpr int kInfoX = 48;
constexpr int kInfoY = 132;
constexpr int kInfoWidth = 356;
constexpr int kInfoHeight = 252;
constexpr int kPreviewX = 456;
constexpr int kPreviewY = 132;
constexpr int kPreviewWidth = 936;
constexpr int kPreviewHeight = 468;
constexpr int kPollPeriodMs = 40;
constexpr int kRefreshMinIntervalMs = 120;
constexpr int kMoveThresholdPx = 6;

struct TouchViewState {
    bool ready = false;
    bool touched = false;
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    int16_t screen_x = 0;
    int16_t screen_y = 0;
    uint32_t sample_count = 0;
    uint32_t controller_addr = 0;
    int64_t last_event_ms = 0;
};

i2c_master_dev_handle_t s_tps = nullptr;
esp_lcd_touch_handle_t s_touch = nullptr;
esp_lcd_panel_io_handle_t s_touch_io = nullptr;
TouchViewState s_touch_state = {};

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

int abs_diff(int a, int b)
{
    return (a > b) ? (a - b) : (b - a);
}

void fill_panel_box(int x, int y, int w, int h)
{
    display.fillRect(x, y, w, h, TFT_WHITE);
    display.drawRect(x, y, w, h, TFT_BLACK);
}

void map_touch_to_screen(uint16_t raw_x, uint16_t raw_y, int16_t *screen_x, int16_t *screen_y)
{
    const int32_t phys_x = (kPanelWidth - 1) - static_cast<int32_t>(raw_y);
    const int32_t phys_y = static_cast<int32_t>(raw_x);

    // M5GFX panel config already handles the display-side orientation.
    int32_t lx = phys_x;
    int32_t ly = phys_y;

    lx = std::clamp<int32_t>(lx, 0, display.width() - 1);
    ly = std::clamp<int32_t>(ly, 0, display.height() - 1);
    *screen_x = static_cast<int16_t>(lx);
    *screen_y = static_cast<int16_t>(ly);
}

bool init_touch()
{
    uint32_t address = 0;
    const esp_err_t err = t5_board_touch_new(kPanelWidth, kPanelHeight, &s_touch, &s_touch_io, &address);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "touch init failed: %s", esp_err_to_name(err));
        s_touch_state.ready = false;
        s_touch_state.touched = false;
        return false;
    }

    s_touch_state.ready = true;
    s_touch_state.controller_addr = address;
    ESP_LOGI(kTag, "GT911 ready at 0x%02X", static_cast<unsigned>(address));
    return true;
}

bool poll_touch(TouchViewState *state)
{
    if (state == nullptr) {
        return false;
    }

    TouchViewState next = *state;
    next.ready = (s_touch != nullptr);

    if (s_touch == nullptr) {
        next.touched = false;
        const bool changed = std::memcmp(&next, state, sizeof(next)) != 0;
        *state = next;
        return changed;
    }

    esp_lcd_touch_read_data(s_touch);

    esp_lcd_touch_point_data_t points[1] = {};
    uint8_t touched = 0;
    if (esp_lcd_touch_get_data(s_touch, points, &touched, 1) == ESP_OK && touched > 0) {
        next.touched = true;
        next.raw_x = points[0].x;
        next.raw_y = points[0].y;
        map_touch_to_screen(next.raw_x, next.raw_y, &next.screen_x, &next.screen_y);
        next.sample_count = state->sample_count + 1;
        next.last_event_ms = esp_timer_get_time() / 1000;
    } else {
        next.touched = false;
    }

    const bool changed = std::memcmp(&next, state, sizeof(next)) != 0;
    *state = next;
    return changed;
}

void draw_info_panel(const TouchViewState &state)
{
    fill_panel_box(kInfoX, kInfoY, kInfoWidth, kInfoHeight);

    int cursor_y = kInfoY + 20;
    const int text_x = kInfoX + 18;
    const int line_height = 30;

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(text_x, cursor_y);
    display.print("Touch Diagnostics");

    cursor_y += 42;
    display.setTextSize(2);
    display.setCursor(text_x, cursor_y);
    if (!state.ready) {
        display.print("Controller: unavailable");
    } else {
        display.printf("Controller: GT911 @ 0x%02X", static_cast<unsigned>(state.controller_addr));
    }

    cursor_y += line_height;
    display.setCursor(text_x, cursor_y);
    if (!state.ready) {
        display.print("State: NOT READY");
    } else if (state.touched) {
        display.print("State: PRESSED");
    } else {
        display.print("State: RELEASED");
    }

    cursor_y += line_height;
    display.setCursor(text_x, cursor_y);
    display.printf("Raw X : %4u", static_cast<unsigned>(state.raw_x));

    cursor_y += line_height;
    display.setCursor(text_x, cursor_y);
    display.printf("Raw Y : %4u", static_cast<unsigned>(state.raw_y));

    cursor_y += line_height;
    display.setCursor(text_x, cursor_y);
    display.printf("LCD X : %4d", static_cast<int>(state.screen_x));

    cursor_y += line_height;
    display.setCursor(text_x, cursor_y);
    display.printf("LCD Y : %4d", static_cast<int>(state.screen_y));

    cursor_y += line_height;
    display.setCursor(text_x, cursor_y);
    display.printf("Samples: %lu", static_cast<unsigned long>(state.sample_count));
}

void draw_preview_panel(const TouchViewState &state)
{
    fill_panel_box(kPreviewX, kPreviewY, kPreviewWidth, kPreviewHeight);

    const uint32_t grid = display.color888(176, 176, 176);
    const uint32_t dark = display.color888(64, 64, 64);
    const int inner_x = kPreviewX + 1;
    const int inner_y = kPreviewY + 1;
    const int inner_w = kPreviewWidth - 2;
    const int inner_h = kPreviewHeight - 2;

    for (int i = 1; i < 4; ++i) {
        const int x = inner_x + (inner_w * i) / 4;
        display.drawLine(x, inner_y, x, inner_y + inner_h - 1, grid);
    }
    for (int i = 1; i < 4; ++i) {
        const int y = inner_y + (inner_h * i) / 4;
        display.drawLine(inner_x, y, inner_x + inner_w - 1, y, grid);
    }

    display.setTextColor(dark, TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(kPreviewX + 18, kPreviewY + 16);
    display.print("Scaled Screen Preview");

    display.setTextSize(1);
    display.setCursor(kPreviewX + 18, kPreviewY + 48);
    display.print("(0,0)");

    char label[24];
    std::snprintf(label, sizeof(label), "(%d,0)", kPanelWidth - 1);
    display.setCursor(kPreviewX + kPreviewWidth - 116, kPreviewY + 48);
    display.print(label);

    std::snprintf(label, sizeof(label), "(0,%d)", kPanelHeight - 1);
    display.setCursor(kPreviewX + 18, kPreviewY + kPreviewHeight - 30);
    display.print(label);

    std::snprintf(label, sizeof(label), "(%d,%d)", kPanelWidth - 1, kPanelHeight - 1);
    display.setCursor(kPreviewX + kPreviewWidth - 140, kPreviewY + kPreviewHeight - 30);
    display.print(label);

    if (!state.ready) {
        display.setTextSize(2);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.setCursor(kPreviewX + 24, kPreviewY + 88);
        display.print("Touch controller not detected.");
        return;
    }

    if (!state.touched && state.sample_count == 0) {
        display.setTextSize(2);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.setCursor(kPreviewX + 24, kPreviewY + 88);
        display.print("Touch and drag on the panel.");
        return;
    }

    const int point_x = inner_x + (static_cast<int64_t>(state.screen_x) * (inner_w - 1)) / (kPanelWidth - 1);
    const int point_y = inner_y + (static_cast<int64_t>(state.screen_y) * (inner_h - 1)) / (kPanelHeight - 1);
    const uint32_t marker = state.touched ? TFT_BLACK : dark;

    display.drawLine(point_x - 24, point_y, point_x + 24, point_y, marker);
    display.drawLine(point_x, point_y - 24, point_x, point_y + 24, marker);
    display.drawCircle(point_x, point_y, 16, marker);
    display.fillCircle(point_x, point_y, 5, marker);
}

void draw_static_footer()
{
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(52, 640);
    display.print("M5GFX EPD + GT911 touch test");

    display.setCursor(52, 674);
    display.print("Raw coordinates come from GT911; LCD coordinates follow the M5GFX drawing space.");
}

void render_screen(const TouchViewState &state, bool full_refresh)
{
    display.waitDisplay();
    display.setEpdMode(full_refresh ? lgfx::epd_mode_t::epd_quality : lgfx::epd_mode_t::epd_fast);

    if (full_refresh) {
        display.fillScreen(TFT_WHITE);
        display.setTextWrap(false);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.setTextSize(3);
        display.setCursor(48, 42);
        display.print("T5-P4 Touch Test");

        display.setTextSize(2);
        display.setCursor(52, 88);
        display.printf("Panel: %d x %d  Driver: m5stack/m5gfx  Touch: GT911",
                       static_cast<int>(display.width()),
                       static_cast<int>(display.height()));

        draw_static_footer();
    }

    draw_info_panel(state);
    draw_preview_panel(state);
    display.display();
}

bool should_draw_update(const TouchViewState &drawn, const TouchViewState &current, int64_t elapsed_ms)
{
    if (drawn.ready != current.ready || drawn.touched != current.touched) {
        return true;
    }
    if (!current.ready) {
        return false;
    }
    if (current.touched) {
        if (abs_diff(current.screen_x, drawn.screen_x) >= kMoveThresholdPx) {
            return true;
        }
        if (abs_diff(current.screen_y, drawn.screen_y) >= kMoveThresholdPx) {
            return true;
        }
    }
    return elapsed_ms >= kRefreshMinIntervalMs && current.sample_count != drawn.sample_count;
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "starting M5GFX EPD touch test");
    log_heap("before init");

    if (!display.init_without_reset(false)) {
        ESP_LOGE(kTag, "display init failed");
        return;
    }

    display.setAutoDisplay(false);
    log_heap("after display init");

    init_touch();
    render_screen(s_touch_state, true);
    log_heap("after initial render");

    TouchViewState last_drawn = s_touch_state;
    int64_t last_draw_ms = esp_timer_get_time() / 1000;

    while (true) {
        const bool changed = poll_touch(&s_touch_state);
        const int64_t now_ms = esp_timer_get_time() / 1000;

        if (changed && should_draw_update(last_drawn, s_touch_state, now_ms - last_draw_ms)) {
            render_screen(s_touch_state, false);
            last_drawn = s_touch_state;
            last_draw_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(kPollPeriodMs));
    }
}
