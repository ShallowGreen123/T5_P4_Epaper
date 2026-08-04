#include "lgfx/v1/platforms/esp32/Bus_EPD.h"

#if SOC_LCD_I80_SUPPORTED

#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "lgfx/v1/platforms/common.hpp"

namespace lgfx {
inline namespace v1 {

namespace {

constexpr char kDiagTag[] = "EPD_DIAG";
constexpr uint32_t kExpectedLines = 684;
constexpr uint32_t kExpectedClocksPerLine = 162;

uint32_t s_frame = 0;
uint32_t s_lines = 0;
uint32_t s_clocks_per_line = 0;
uint32_t s_tx_errors = 0;

}  // namespace

bool IRAM_ATTR Bus_EPD::notify_line_done(esp_lcd_panel_io_handle_t panel_io,
                                         esp_lcd_panel_io_event_data_t *edata,
                                         void *user_ctx)
{
    auto *me = static_cast<Bus_EPD *>(user_ctx);
    gpio_lo(me->_config.pin_ckv);
    gpio_hi(me->_config.pin_le);
    me->_bus_busy = false;
    return false;
}

void Bus_EPD::wait(void)
{
    while (_bus_busy) {
        taskYIELD();
    }
}

void Bus_EPD::beginTransaction(void)
{
    const auto ckv = _config.pin_ckv;
    const auto spv = _config.pin_spv;
    wait();

    ++s_frame;
    s_lines = 0;
    s_clocks_per_line = 0;

    gpio_lo(spv);
    delayMicroseconds(1);
    gpio_lo(ckv);
    delayMicroseconds(3);
    gpio_hi(ckv);
    delayMicroseconds(1);
    gpio_hi(spv);
    for (int i = 0; i < 3; ++i) {
        delayMicroseconds(3);
        gpio_lo(ckv);
        delayMicroseconds(3);
        gpio_hi(ckv);
    }
}

void Bus_EPD::endTransaction(void)
{
    wait();
    gpio_lo(_config.pin_le);
    gpio_hi(_config.pin_ckv);

    const bool geometry_ok = s_lines == kExpectedLines
                          && s_clocks_per_line == kExpectedClocksPerLine;
    if (geometry_ok) {
        ESP_LOGI(kDiagTag,
                 "frame=%lu lines=%lu clocks/line=%lu XSTL=1-clock tx_errors=%lu",
                 static_cast<unsigned long>(s_frame),
                 static_cast<unsigned long>(s_lines),
                 static_cast<unsigned long>(s_clocks_per_line),
                 static_cast<unsigned long>(s_tx_errors));
    } else {
        ESP_LOGW(kDiagTag,
                 "frame=%lu geometry mismatch: lines=%lu (expected %lu), clocks/line=%lu (expected %lu)",
                 static_cast<unsigned long>(s_frame),
                 static_cast<unsigned long>(s_lines),
                 static_cast<unsigned long>(kExpectedLines),
                 static_cast<unsigned long>(s_clocks_per_line),
                 static_cast<unsigned long>(kExpectedClocksPerLine));
    }
}

void Bus_EPD::scanlineDone(void)
{
}

bool Bus_EPD::powerControl(bool power_on)
{
    if (_pwr_on != power_on) {
        _pwr_on = power_on;
        wait();
        if (power_on) {
            gpio_hi(_config.pin_oe);
            delayMicroseconds(100);
            gpio_hi(_config.pin_pwr);
            delayMicroseconds(100);
            gpio_hi(_config.pin_spv);
            delay(1);
        } else {
            delay(1);
            gpio_lo(_config.pin_pwr);
            delayMicroseconds(10);
            gpio_lo(_config.pin_oe);
            delayMicroseconds(100);
            gpio_lo(_config.pin_spv);
        }
    }
    return true;
}

void Bus_EPD::writeScanLine(const uint8_t *data, uint32_t length)
{
    wait();
    if (data == nullptr || length < 4 || (length & 1U) != 0) {
        ++s_tx_errors;
        ESP_LOGE(kDiagTag, "invalid scan line: data=%p length=%lu",
                 data, static_cast<unsigned long>(length));
        return;
    }

    // The I80 D/C signal is wired to XSTL. The 16-bit command phase sends the
    // first source word with XSTL low; the data phase sends the rest with XSTL
    // high. This creates the required one-clock start pulse without a CS pin.
    const int first_word = static_cast<int>(data[0])
                         | (static_cast<int>(data[1]) << 8);

    _bus_busy = true;
    gpio_lo(_config.pin_le);
    gpio_hi(_config.pin_ckv);
    ++s_lines;
    s_clocks_per_line = length / 2;

    const esp_err_t err = esp_lcd_panel_io_tx_color(_io_handle, first_word,
                                                     data + 2, length - 2);
    if (err != ESP_OK) {
        ++s_tx_errors;
        gpio_lo(_config.pin_ckv);
        gpio_hi(_config.pin_le);
        _bus_busy = false;
        ESP_LOGE(kDiagTag, "I80 line transmit failed: %s", esp_err_to_name(err));
    }
}

bool Bus_EPD::init(void)
{
    _bus_busy = false;
    _pwr_on = false;

    if (_config.bus_width != 16) {
        ESP_LOGE(kDiagTag, "this compatibility bus requires a 16-bit I80 configuration");
        return false;
    }

    pinMode(_config.pin_spv, pin_mode_t::output);
    pinMode(_config.pin_ckv, pin_mode_t::output);
    pinMode(_config.pin_sph, pin_mode_t::output);
    pinMode(_config.pin_oe, pin_mode_t::output);
    pinMode(_config.pin_le, pin_mode_t::output);
    pinMode(_config.pin_cl, pin_mode_t::output);
    gpio_hi(_config.pin_spv);
    gpio_hi(_config.pin_ckv);
    gpio_hi(_config.pin_sph);
    gpio_lo(_config.pin_le);

    esp_lcd_i80_bus_config_t bus_config = {};
    bus_config.max_transfer_bytes = 1024;
    bus_config.clk_src = LCD_CLK_SRC_PLL160M;
    bus_config.dc_gpio_num = static_cast<gpio_num_t>(_config.pin_sph);
    bus_config.wr_gpio_num = static_cast<gpio_num_t>(_config.pin_cl);
    bus_config.bus_width = _config.bus_width;
    for (int i = 0; i < _config.bus_width; ++i) {
        bus_config.data_gpio_nums[i] = static_cast<gpio_num_t>(_config.pin_data[i]);
        pinMode(_config.pin_data[i], pin_mode_t::output);
    }
    if (esp_lcd_new_i80_bus(&bus_config, &_i80_bus_handle) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_io_i80_config_t io_config = {};
    io_config.trans_queue_depth = 4;
    io_config.on_color_trans_done = notify_line_done;
    io_config.user_ctx = this;
    io_config.lcd_cmd_bits = 16;
    io_config.lcd_param_bits = 16;
    io_config.pclk_hz = _config.bus_speed;
    io_config.dc_levels.dc_idle_level = 1;
    io_config.dc_levels.dc_cmd_level = 0;
    io_config.dc_levels.dc_dummy_level = 1;
    io_config.dc_levels.dc_data_level = 1;
    io_config.flags.pclk_idle_low = 1;
    io_config.cs_gpio_num = -1;

    if (esp_lcd_new_panel_io_i80(_i80_bus_handle, &io_config, &_io_handle) != ESP_OK) {
        return false;
    }

    ESP_LOGI(kDiagTag,
             "16-bit I80 ready: CKH=%ld Hz, XSTL uses D/C command phase (1 clock), CS disabled",
             static_cast<long>(_config.bus_speed));
    return true;
}

}  // namespace v1
}  // namespace lgfx

#endif
