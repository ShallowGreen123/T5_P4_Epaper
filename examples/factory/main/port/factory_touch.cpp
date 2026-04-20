#include "factory_touch.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_io_expander_pca9535.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board_config.h"
#include "factory_display.h"

namespace {

static const char *TAG = "factory_touch";

static esp_io_expander_handle_t s_io = nullptr;
static esp_lcd_panel_io_handle_t s_touch_io = nullptr;
static esp_lcd_touch_handle_t s_touch = nullptr;
static bool s_touch_ready = false;
static factory_touch_diag_state_t s_diag = {
    .ready = false,
    .pressed = false,
    .x = 0,
    .y = 0,
    .raw_x = 0,
    .raw_y = 0,
    .sample_count = 0,
    .status_text = "Touch unavailable",
};

static constexpr uint32_t pin_mask(uint8_t pin)
{
    return (1UL << pin);
}

static esp_err_t expander_set_pin(uint8_t pin, bool high)
{
    return esp_io_expander_set_level(s_io, pin_mask(pin), high ? 1 : 0);
}

static bool map_touch_coordinates(uint16_t raw_x, uint16_t raw_y, int16_t *mapped_x, int16_t *mapped_y)
{
    if (mapped_x == nullptr || mapped_y == nullptr) {
        return false;
    }

    const factory_display_mode_info_t *info = factory_display_get_mode_info();
    int32_t phys_x = FACTORY_BOARD_WIDTH - 1 - (int32_t)raw_y;
    int32_t phys_y = (int32_t)raw_x;
    int32_t lx = phys_x;
    int32_t ly = phys_y;

    switch (info->rotation_deg) {
        case 90:
            lx = phys_y;
            ly = FACTORY_BOARD_WIDTH - 1 - phys_x;
            break;
        case 180:
            lx = FACTORY_BOARD_WIDTH - 1 - phys_x;
            ly = FACTORY_BOARD_HEIGHT - 1 - phys_y;
            break;
        case 270:
            lx = FACTORY_BOARD_HEIGHT - 1 - phys_y;
            ly = phys_x;
            break;
        case 0:
        default:
            break;
    }

    if ((info->mirror_mode & 0x1U) != 0U) {
        lx = (int32_t)info->width - 1 - lx;
    }
    if ((info->mirror_mode & 0x2U) != 0U) {
        ly = (int32_t)info->height - 1 - ly;
    }

    if (lx < 0) {
        lx = 0;
    }
    if (ly < 0) {
        ly = 0;
    }
    if (lx >= info->width) {
        lx = info->width - 1;
    }
    if (ly >= info->height) {
        ly = info->height - 1;
    }

    *mapped_x = (int16_t)lx;
    *mapped_y = (int16_t)ly;
    return true;
}

static bool init_expander()
{
    if (s_io != nullptr) {
        return true;
    }

    i2c_master_bus_handle_t bus_handle = factory_display_get_i2c_bus();
    if (bus_handle == nullptr) {
        ESP_LOGW(TAG, "display i2c bus unavailable");
        return false;
    }

    esp_err_t err = esp_io_expander_new_i2c_pca9535(
        bus_handle,
        ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000,
        &s_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PCA9535 not found: %s", esp_err_to_name(err));
        return false;
    }

    const uint32_t output_mask =
        pin_mask(FACTORY_PCA_T_RST) |
        pin_mask(FACTORY_PCA_CC_SW0) |
        pin_mask(FACTORY_PCA_CC_SW1) |
        pin_mask(FACTORY_PCA_LR_RST) |
        pin_mask(FACTORY_PCA_NRF_CE) |
        pin_mask(FACTORY_PCA_SHUTDOWN) |
        pin_mask(FACTORY_PCA_HDMI_RST) |
        pin_mask(FACTORY_PCA_HDMI_EN) |
        pin_mask(FACTORY_PCA_EP_OE) |
        pin_mask(FACTORY_PCA_EP_MODE) |
        pin_mask(FACTORY_PCA_1V8_EN) |
        pin_mask(FACTORY_PCA_TPS_PWRUP) |
        pin_mask(FACTORY_PCA_VCOM_CTRL) |
        pin_mask(FACTORY_PCA_TPS_WAKEUP);
    const uint32_t input_mask =
        pin_mask(FACTORY_PCA_TPS_PWR_GOOD) |
        pin_mask(FACTORY_PCA_TPS_INT);

    err = esp_io_expander_set_dir(s_io, output_mask, IO_EXPANDER_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "configure PCA9535 outputs failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_io_expander_set_level(s_io, output_mask, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set PCA9535 output levels failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_io_expander_set_dir(s_io, input_mask, IO_EXPANDER_INPUT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "configure PCA9535 inputs failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "PCA9535 ready");
    return true;
}

static bool reset_gt911_for_selected_address()
{
    const int addr = CONFIG_FACTORY_TOUCH_I2C_ADDR;
    if (addr != ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS &&
        addr != ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP) {
        ESP_LOGW(TAG, "unsupported GT911 address: 0x%02X", addr);
        return false;
    }

    gpio_config_t int_gpio_config = {};
    int_gpio_config.intr_type = GPIO_INTR_DISABLE;
    int_gpio_config.pin_bit_mask = (1ULL << FACTORY_TOUCH_INT_GPIO);
    int_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    int_gpio_config.pull_up_en = GPIO_PULLUP_ENABLE;
    int_gpio_config.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&int_gpio_config) != ESP_OK) {
        ESP_LOGW(TAG, "configure GT911 INT gpio failed");
        return false;
    }

    if (expander_set_pin(FACTORY_PCA_T_RST, false) != ESP_OK) {
        ESP_LOGW(TAG, "assert GT911 reset failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    const int int_level = (addr == ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP) ? 1 : 0;
    if (gpio_set_level((gpio_num_t)FACTORY_TOUCH_INT_GPIO, int_level) != ESP_OK) {
        ESP_LOGW(TAG, "drive GT911 INT level failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    if (expander_set_pin(FACTORY_PCA_T_RST, true) != ESP_OK) {
        ESP_LOGW(TAG, "release GT911 reset failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    int_gpio_config.mode = GPIO_MODE_INPUT;
    if (gpio_config(&int_gpio_config) != ESP_OK) {
        ESP_LOGW(TAG, "restore GT911 INT gpio failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return true;
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;

    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    uint8_t touched = 0;

    if (s_touch == nullptr) {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    esp_lcd_touch_read_data(s_touch);

    esp_lcd_touch_point_data_t touch_points[1] = {};
    if (esp_lcd_touch_get_data(s_touch, touch_points, &touched, 1) == ESP_OK && touched > 0) {
        int16_t mapped_x = 0;
        int16_t mapped_y = 0;
        map_touch_coordinates(touch_points[0].x, touch_points[0].y, &mapped_x, &mapped_y);

        last_x = (lv_coord_t)mapped_x;
        last_y = (lv_coord_t)mapped_y;
        s_diag.ready = true;
        s_diag.pressed = true;
        s_diag.x = mapped_x;
        s_diag.y = mapped_y;
        s_diag.raw_x = (int16_t)touch_points[0].x;
        s_diag.raw_y = (int16_t)touch_points[0].y;
        s_diag.sample_count++;
        s_diag.status_text = "Touch ready";

        data->state = LV_INDEV_STATE_PR;
    } else {
        s_diag.pressed = false;
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

static void register_lvgl_touch()
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

}  // namespace

extern "C" bool factory_touch_init(void)
{
    s_touch_ready = false;
    s_diag.ready = false;
    s_diag.pressed = false;
    s_diag.x = 0;
    s_diag.y = 0;
    s_diag.raw_x = 0;
    s_diag.raw_y = 0;
    s_diag.sample_count = 0;
    s_diag.status_text = "Touch unavailable";

    if (!init_expander()) {
        return false;
    }

    if (!reset_gt911_for_selected_address()) {
        return false;
    }

    i2c_master_bus_handle_t bus_handle = factory_display_get_i2c_bus();
    if (bus_handle == nullptr) {
        ESP_LOGW(TAG, "display i2c bus unavailable");
        return false;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = (uint32_t)CONFIG_FACTORY_TOUCH_I2C_ADDR,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 0,
        .flags = {
            .dc_low_on_data = 0,
            .disable_control_phase = 1,
        },
        .scl_speed_hz = FACTORY_I2C_FREQ_HZ,
    };

    esp_err_t err = esp_lcd_new_panel_io_i2c(bus_handle, &io_config, &s_touch_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "create GT911 panel io failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_touch_config_t touch_config = {
        .x_max = FACTORY_BOARD_WIDTH,
        .y_max = FACTORY_BOARD_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = (gpio_num_t)FACTORY_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr,
    };

    err = esp_lcd_touch_new_i2c_gt911(s_touch_io, &touch_config, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 init failed at 0x%02X: %s", CONFIG_FACTORY_TOUCH_I2C_ADDR, esp_err_to_name(err));
        esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = nullptr;
        s_touch = nullptr;
        return false;
    }

    s_touch_ready = true;
    s_diag.ready = true;
    s_diag.status_text = "Touch ready";
    register_lvgl_touch();

    ESP_LOGI(TAG, "touch ready");
    return true;
}

extern "C" bool factory_touch_is_ready(void)
{
    return s_touch_ready;
}

extern "C" void factory_touch_get_diag_state(factory_touch_diag_state_t *state)
{
    if (state == nullptr) {
        return;
    }
    *state = s_diag;
}
