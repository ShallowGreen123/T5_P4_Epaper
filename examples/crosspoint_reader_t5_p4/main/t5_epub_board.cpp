#include "t5_epub_board.h"

#include <algorithm>

#include "driver/gpio.h"
#include "esp_io_expander_pca9535.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "t5_epub_board";

constexpr gpio_num_t kI2cSdaPin = GPIO_NUM_7;
constexpr gpio_num_t kI2cSclPin = GPIO_NUM_8;
constexpr gpio_num_t kTouchIntPin = GPIO_NUM_3;

constexpr gpio_num_t kSdMisoPin = GPIO_NUM_44;
constexpr gpio_num_t kSdClkPin = GPIO_NUM_45;
constexpr gpio_num_t kSdMosiPin = GPIO_NUM_46;
constexpr gpio_num_t kSdCsPin = GPIO_NUM_47;

constexpr uint8_t kPinModeInput = 1;
constexpr uint8_t kPinModeInputPullup = 2;
constexpr uint8_t kPinModeOutput = 3;
constexpr uint8_t kPinModeInputPulldown = 4;

constexpr uint32_t kExtIoPinBase = 0x1000;
constexpr uint32_t pin_mask(uint8_t pin) { return (1UL << pin); }

constexpr uint8_t kBoardPca00TouchReset = 0;
constexpr uint8_t kBoardPca01CcSw0 = 1;
constexpr uint8_t kBoardPca02CcSw1 = 2;
constexpr uint8_t kBoardPca03LrReset = 3;
constexpr uint8_t kBoardPca04NrfCe = 4;
constexpr uint8_t kBoardPca05Shutdown = 5;
constexpr uint8_t kBoardPca06HdmiReset = 6;
constexpr uint8_t kBoardPca07HdmiEnable = 7;
constexpr uint8_t kBoardPca10EpOe = 8;
constexpr uint8_t kBoardPca11EpMode = 9;
constexpr uint8_t kBoardPca12V18Enable = 10;
constexpr uint8_t kBoardPca13TpsPowerUp = 11;
constexpr uint8_t kBoardPca14VcomCtrl = 12;
constexpr uint8_t kBoardPca15TpsWakeup = 13;
constexpr uint8_t kBoardPca16TpsPowerGood = 14;
constexpr uint8_t kBoardPca17TpsInt = 15;

constexpr uint32_t kPca9535Address = ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000;

void configure_native_gpio(uint32_t pin, uint8_t mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_down_en = (mode == kPinModeInputPulldown) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = (mode == kPinModeInputPullup) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.mode = (mode == kPinModeOutput) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

esp_err_t configure_touch_int_gpio(gpio_mode_t mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << kTouchIntPin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.mode = mode;
    return gpio_config(&io_conf);
}

}  // namespace

T5P4Board *T5P4Board::s_active_board_ = nullptr;

bool T5P4Board::init()
{
    s_active_board_ = this;
    return init_i2c_bus() && init_io_expander() && init_touch() && init_display();
}

bool T5P4Board::init_i2c_bus()
{
    if (i2c_bus_ != nullptr) {
        return true;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = kI2cSdaPin;
    bus_config.scl_io_num = kI2cSclPin;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    const esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        i2c_bus_ = nullptr;
        return false;
    }

    bbepSetI2CMasterBus(i2c_bus_);
    ESP_LOGI(kTag, "I2C bus initialized on port %d", I2C_NUM_0);
    return true;
}

bool T5P4Board::init_io_expander()
{
    if (io_ != nullptr) {
        return true;
    }

    esp_err_t err = esp_io_expander_new_i2c_pca9535(i2c_bus_, kPca9535Address, &io_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize PCA9535: %s", esp_err_to_name(err));
        return false;
    }

    const uint32_t output_mask =
        pin_mask(kBoardPca00TouchReset) |
        pin_mask(kBoardPca01CcSw0) |
        pin_mask(kBoardPca02CcSw1) |
        pin_mask(kBoardPca03LrReset) |
        pin_mask(kBoardPca04NrfCe) |
        pin_mask(kBoardPca05Shutdown) |
        pin_mask(kBoardPca06HdmiReset) |
        pin_mask(kBoardPca07HdmiEnable) |
        pin_mask(kBoardPca10EpOe) |
        pin_mask(kBoardPca11EpMode) |
        pin_mask(kBoardPca12V18Enable) |
        pin_mask(kBoardPca13TpsPowerUp) |
        pin_mask(kBoardPca14VcomCtrl) |
        pin_mask(kBoardPca15TpsWakeup);
    const uint32_t input_mask =
        pin_mask(kBoardPca16TpsPowerGood) |
        pin_mask(kBoardPca17TpsInt);

    err = esp_io_expander_set_dir(io_, output_mask, IO_EXPANDER_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to configure PCA9535 outputs: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_io_expander_set_level(io_, output_mask, 1);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to set PCA9535 outputs: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_io_expander_set_dir(io_, input_mask, IO_EXPANDER_INPUT);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to configure PCA9535 inputs: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "PCA9535 initialized");
    return true;
}

bool T5P4Board::reset_gt911(uint32_t address)
{
    if (address != ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS &&
        address != ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP) {
        ESP_LOGE(kTag, "Unsupported GT911 address: 0x%02X", (unsigned int)address);
        return false;
    }

    if (configure_touch_int_gpio(GPIO_MODE_OUTPUT) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to configure GT911 INT as output");
        return false;
    }
    if (set_expander_pin(kBoardPca00TouchReset, false) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to assert GT911 reset");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    const int int_level = (address == ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP) ? 1 : 0;
    if (gpio_set_level(kTouchIntPin, int_level) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to drive GT911 INT level");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    if (set_expander_pin(kBoardPca00TouchReset, true) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to release GT911 reset");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    if (configure_touch_int_gpio(GPIO_MODE_INPUT) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to restore GT911 INT as input");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return true;
}

void T5P4Board::cleanup_touch_handles()
{
    if (touch_ != nullptr) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (touch_io_ != nullptr) {
        esp_lcd_panel_io_del(touch_io_);
        touch_io_ = nullptr;
    }
}

bool T5P4Board::init_gt911_touch(uint32_t address)
{
    cleanup_touch_handles();
    if (!reset_gt911(address)) {
        return false;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = address;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 16;
    io_config.lcd_param_bits = 0;
    io_config.flags.dc_low_on_data = 0;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = 400000;

    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &touch_io_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to create GT911 panel IO at 0x%02X: %s",
                 (unsigned int)address, esp_err_to_name(err));
        cleanup_touch_handles();
        return false;
    }

    esp_lcd_touch_config_t touch_config = {};
    touch_config.x_max = kPanelWidth;
    touch_config.y_max = kPanelHeight;
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = kTouchIntPin;
    touch_config.levels.reset = 0;
    touch_config.levels.interrupt = 0;
    touch_config.flags.swap_xy = 0;
    touch_config.flags.mirror_x = 0;
    touch_config.flags.mirror_y = 0;

    err = esp_lcd_touch_new_i2c_gt911(touch_io_, &touch_config, &touch_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to initialize GT911 at 0x%02X: %s",
                 (unsigned int)address, esp_err_to_name(err));
        cleanup_touch_handles();
        return false;
    }

    return true;
}

bool T5P4Board::init_touch()
{
    if (touch_ != nullptr) {
        return true;
    }

    if (init_gt911_touch(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)) {
        ESP_LOGI(kTag, "GT911 initialized at 0x%02X", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
        return true;
    }

    ESP_LOGW(kTag, "GT911 init at 0x%02X failed, retry backup address 0x%02X",
             ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
             ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);

    if (init_gt911_touch(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)) {
        ESP_LOGI(kTag, "GT911 initialized at 0x%02X", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
        return true;
    }

    ESP_LOGE(kTag, "Failed to initialize GT911");
    return false;
}

esp_err_t T5P4Board::set_expander_pin(uint8_t pin, bool high)
{
    if (io_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_set_level(io_, pin_mask(pin), high ? 1 : 0);
}

int T5P4Board::read_expander_pin(uint8_t pin)
{
    if (io_ == nullptr) {
        return 0;
    }

    uint32_t levels = 0;
    if (esp_io_expander_get_level(io_, pin_mask(pin), &levels) != ESP_OK) {
        return 0;
    }
    return (levels & pin_mask(pin)) ? 1 : 0;
}

bool T5P4Board::init_display()
{
    const int rc = epaper_.initPanel(BB_PANEL_LILYGO_T5P4, 40000000);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGE(kTag, "Failed to initialize FastEPD panel: %d", rc);
        return false;
    }

    if (epaper_.width() != kPanelWidth || epaper_.height() != kPanelHeight) {
        ESP_LOGW(kTag, "Unexpected panel dimensions after init: %dx%d", epaper_.width(), epaper_.height());
    } else {
        ESP_LOGI(kTag, "Panel dimensions: %dx%d", epaper_.width(), epaper_.height());
    }

    if (epaper_.setMode(BB_MODE_4BPP) != BBEP_SUCCESS) {
        ESP_LOGE(kTag, "Failed to switch FastEPD to 4bpp mode");
        return false;
    }

    epaper_.setRotation(CONFIG_EPUB_READER_ROTATION);
    epaper_.fillScreen(15);
    ESP_LOGI(kTag, "Display ready: %dx%d", epaper_.width(), epaper_.height());
    return true;
}

bool T5P4Board::mount_sd_card(const char *mount_point)
{
    if (card_ != nullptr) {
        return true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = kSdMosiPin;
    bus_cfg.miso_io_num = kSdMisoPin;
    bus_cfg.sclk_io_num = kSdClkPin;
    bus_cfg.quadwp_io_num = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = 4096;

    const spi_host_device_t spi_host = static_cast<spi_host_device_t>(host.slot);

    esp_err_t ret = spi_bus_initialize(spi_host, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize SDSPI bus: %s", esp_err_to_name(ret));
        return false;
    }
    spi_bus_ready_ = true;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = spi_host;
    slot_config.gpio_cs = kSdCsPin;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card_);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "Failed to mount SD card: %s", esp_err_to_name(ret));
        card_ = nullptr;
        if (spi_bus_ready_) {
            spi_bus_free(spi_host);
            spi_bus_ready_ = false;
        }
        return false;
    }

    sdmmc_card_print_info(stdout, card_);
    return true;
}

void T5P4Board::unmount_sd_card(const char *mount_point)
{
    if (card_ != nullptr) {
        esp_vfs_fat_sdcard_unmount(mount_point, card_);
        card_ = nullptr;
    }
    if (spi_bus_ready_) {
        spi_bus_free(SPI2_HOST);
        spi_bus_ready_ = false;
    }
}

bool T5P4Board::read_touch_point(int16_t *x, int16_t *y)
{
    if (touch_ == nullptr) {
        return false;
    }

    esp_lcd_touch_read_data(touch_);

    esp_lcd_touch_point_data_t points[1] = {};
    uint8_t touched = 0;
    if (esp_lcd_touch_get_data(touch_, points, &touched, 1) != ESP_OK || touched == 0) {
        return false;
    }

    const int32_t phys_x = (kPanelWidth - 1) - (int32_t)points[0].y;
    const int32_t phys_y = (int32_t)points[0].x;

    int32_t lx = phys_x;
    int32_t ly = phys_y;

    switch (CONFIG_EPUB_READER_ROTATION) {
        case 90:
            lx = phys_y;
            ly = (kPanelWidth - 1) - phys_x;
            break;
        case 180:
            lx = (kPanelWidth - 1) - phys_x;
            ly = (kPanelHeight - 1) - phys_y;
            break;
        case 270:
            lx = (kPanelHeight - 1) - phys_y;
            ly = phys_x;
            break;
        default:
            break;
    }

    // The GT911 orientation is 180 degrees opposite the rendered panel output
    // on this board, so mirror in logical screen space after rotation.
    lx = (epaper_.width() - 1) - lx;
    ly = (epaper_.height() - 1) - ly;

    lx = std::clamp<int32_t>(lx, 0, epaper_.width() - 1);
    ly = std::clamp<int32_t>(ly, 0, epaper_.height() - 1);
    *x = (int16_t)lx;
    *y = (int16_t)ly;
    return true;
}

void T5P4Board::platform_pin_mode(uint32_t pin, uint8_t mode)
{
    if (s_active_board_ != nullptr) {
        s_active_board_->gpio_mode_impl(pin, mode);
        return;
    }
    configure_native_gpio(pin, mode);
}

void T5P4Board::platform_digital_write(uint32_t pin, uint8_t value)
{
    if (s_active_board_ != nullptr) {
        s_active_board_->gpio_write_impl(pin, value);
        return;
    }
    gpio_set_level((gpio_num_t)pin, value);
}

int T5P4Board::platform_digital_read(uint32_t pin)
{
    if (s_active_board_ != nullptr) {
        return s_active_board_->gpio_read_impl(pin);
    }
    return gpio_get_level((gpio_num_t)pin);
}

void T5P4Board::gpio_mode_impl(uint32_t pin, uint8_t mode)
{
    if (pin >= kExtIoPinBase && pin < (kExtIoPinBase + 16)) {
        if (io_ == nullptr) {
            return;
        }
        const uint8_t expander_pin = (uint8_t)(pin - kExtIoPinBase);
        const esp_io_expander_dir_t dir = (mode == kPinModeOutput) ? IO_EXPANDER_OUTPUT : IO_EXPANDER_INPUT;
        esp_io_expander_set_dir(io_, pin_mask(expander_pin), dir);
        return;
    }
    configure_native_gpio(pin, mode);
}

void T5P4Board::gpio_write_impl(uint32_t pin, uint8_t value)
{
    if (pin >= kExtIoPinBase && pin < (kExtIoPinBase + 16)) {
        set_expander_pin((uint8_t)(pin - kExtIoPinBase), value != 0);
        return;
    }
    gpio_set_level((gpio_num_t)pin, value);
}

int T5P4Board::gpio_read_impl(uint32_t pin)
{
    if (pin >= kExtIoPinBase && pin < (kExtIoPinBase + 16)) {
        return read_expander_pin((uint8_t)(pin - kExtIoPinBase));
    }
    return gpio_get_level((gpio_num_t)pin);
}

void pinMode(uint32_t gpio, uint8_t mode)
{
    T5P4Board::platform_pin_mode(gpio, mode);
}

void digitalWrite(uint32_t gpio, uint8_t level)
{
    T5P4Board::platform_digital_write(gpio, level);
}

int digitalRead(uint32_t gpio)
{
    return T5P4Board::platform_digital_read(gpio);
}
