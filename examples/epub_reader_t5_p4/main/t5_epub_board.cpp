#include "t5_epub_board.h"

#include <algorithm>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "t5_epub_board";

constexpr gpio_num_t kI2cSdaPin = GPIO_NUM_7;
constexpr gpio_num_t kI2cSclPin = GPIO_NUM_8;
constexpr gpio_num_t kTouchIrqPin = GPIO_NUM_5;

constexpr gpio_num_t kSdMisoPin = GPIO_NUM_44;
constexpr gpio_num_t kSdClkPin = GPIO_NUM_45;
constexpr gpio_num_t kSdMosiPin = GPIO_NUM_46;
constexpr gpio_num_t kSdCsPin = GPIO_NUM_47;

constexpr uint32_t kExtIoPinBase = 0x1000;
constexpr uint32_t extio_pin(uint32_t pin) { return kExtIoPinBase + pin; }

constexpr uint8_t kBoardPca00TouchReset = 0;
constexpr uint8_t kBoardPca14VcomCtrl = 12;
constexpr uint8_t kBoardPca15TpsWakeup = 13;

void configure_native_gpio(uint32_t pin, uint8_t mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

}  // namespace

T5P4Board *T5P4Board::s_active_board_ = nullptr;

bool T5P4Board::init()
{
    s_active_board_ = this;
    return init_io_expander() && init_touch() && init_display();
}

bool T5P4Board::init_io_expander()
{
    if (!io_.begin((i2c_port_t)I2C_NUM_0, XL9555_SLAVE_ADDRESS0, kI2cSdaPin, kI2cSclPin)) {
        ESP_LOGE(kTag, "Failed to initialize XL9555");
        return false;
    }

    for (int pin = 0; pin < 16; ++pin) {
        io_.pinMode(pin, OUTPUT);
        io_.digitalWrite(pin, HIGH);
    }

    io_.pinMode(kBoardPca14VcomCtrl, INPUT);
    io_.pinMode(kBoardPca15TpsWakeup, INPUT);
    ESP_LOGI(kTag, "XL9555 initialized");
    return true;
}

bool T5P4Board::init_touch()
{
    touch_.setPins((int)extio_pin(kBoardPca00TouchReset), (int)kTouchIrqPin);
    touch_.setGpioCallback(gpio_mode_thunk, gpio_write_thunk, gpio_read_thunk);

    // XL9555 and GT911 share I2C0. SensorLib's ESP-IDF path reinstalls the driver
    // during begin(), so delete the expander-installed instance first to avoid the
    // noisy "i2c driver install error" path while still reusing the same bus/pins.
    esp_err_t ret = i2c_driver_delete(I2C_NUM_0);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Failed to reset I2C driver before GT911 init: %s", esp_err_to_name(ret));
    }

    if (!touch_.begin((i2c_port_t)I2C_NUM_0, GT911_SLAVE_ADDRESS_L, kI2cSdaPin, kI2cSclPin)) {
        ESP_LOGE(kTag, "Failed to initialize GT911");
        return false;
    }

    touch_.setInterruptMode(LOW_LEVEL_QUERY);
    ESP_LOGI(kTag, "GT911 initialized");
    return true;
}

bool T5P4Board::init_display()
{
    const int rc = epaper_.initPanel(BB_PANEL_LILYGO_T5P4, 40000000);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGE(kTag, "Failed to initialize FastEPD panel: %d", rc);
        return false;
    }

    // BB_PANEL_LILYGO_T5P4 already has a built-in panel definition, and initPanel()
    // allocates the frame buffers with the correct size. Calling setPanelSize() again
    // returns BBEP_ERROR_BAD_PARAMETER because the panel is already configured.
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
    int16_t raw_x[1] = {0};
    int16_t raw_y[1] = {0};
    const uint8_t touched = touch_.getPoint(raw_x, raw_y, 1);
    if (touched == 0) {
        return false;
    }

    const int32_t phys_x = (kPanelWidth - 1) - (int32_t)raw_y[0];
    const int32_t phys_y = (int32_t)raw_x[0];

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

    // The GT911 coordinates end up 180 degrees opposite to the rendered FastEPD
    // output on this board. Keep the display rotation unchanged and rotate the
    // touch coordinates in logical screen space so touches match the current UI.
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

void T5P4Board::gpio_mode_thunk(uint32_t pin, uint8_t mode)
{
    if (s_active_board_ != nullptr) {
        s_active_board_->gpio_mode_impl(pin, mode);
    }
}

void T5P4Board::gpio_write_thunk(uint32_t pin, uint8_t value)
{
    if (s_active_board_ != nullptr) {
        s_active_board_->gpio_write_impl(pin, value);
    }
}

int T5P4Board::gpio_read_thunk(uint32_t pin)
{
    if (s_active_board_ != nullptr) {
        return s_active_board_->gpio_read_impl(pin);
    }
    return 0;
}

void T5P4Board::gpio_mode_impl(uint32_t pin, uint8_t mode)
{
    if (pin >= kExtIoPinBase && pin < (kExtIoPinBase + 16)) {
        io_.pinMode((uint8_t)(pin - kExtIoPinBase), mode);
        return;
    }
    configure_native_gpio(pin, mode);
}

void T5P4Board::gpio_write_impl(uint32_t pin, uint8_t value)
{
    if (pin >= kExtIoPinBase && pin < (kExtIoPinBase + 16)) {
        io_.digitalWrite((uint8_t)(pin - kExtIoPinBase), value);
        return;
    }
    gpio_set_level((gpio_num_t)pin, value);
}

int T5P4Board::gpio_read_impl(uint32_t pin)
{
    if (pin >= kExtIoPinBase && pin < (kExtIoPinBase + 16)) {
        return io_.digitalRead((uint8_t)(pin - kExtIoPinBase));
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
