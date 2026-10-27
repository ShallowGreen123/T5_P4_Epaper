#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <FastEPD.h>
#include "driver/i2c_master.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

class T5P4Board {
public:
    static constexpr int kPanelWidth = 1440;
    static constexpr int kPanelHeight = 720;

    T5P4Board() = default;
    ~T5P4Board() = default;

    bool init();
    bool mount_sd_card(const char *mount_point);
    void unmount_sd_card(const char *mount_point);
    bool read_touch_point(int16_t *x, int16_t *y);
    static void platform_pin_mode(uint32_t pin, uint8_t mode);
    static void platform_digital_write(uint32_t pin, uint8_t value);
    static int platform_digital_read(uint32_t pin);

    FASTEPD &display() { return epaper_; }
    int screen_width() { return epaper_.width(); }
    int screen_height() { return epaper_.height(); }

private:
    bool init_i2c_bus();
    bool init_touch();
    bool init_display();

    static T5P4Board *s_active_board_;

    FASTEPD epaper_;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    sdmmc_card_t *card_ = nullptr;
    bool spi_bus_ready_ = false;
};
