#include "t5_epub_board.h"

#include <algorithm>

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "bsp/esp-bsp.h"

namespace {

constexpr char kTag[] = "t5_epub_board";
constexpr uint8_t kPinModeOutput = 1;

constexpr gpio_num_t kSdMisoPin = GPIO_NUM_44;
constexpr gpio_num_t kSdClkPin = GPIO_NUM_45;
constexpr gpio_num_t kSdMosiPin = GPIO_NUM_46;
constexpr gpio_num_t kSdCsPin = GPIO_NUM_47;

void configure_native_gpio(uint32_t pin, uint8_t mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.mode = (mode == kPinModeOutput) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

}  // namespace

T5P4Board *T5P4Board::s_active_board_ = nullptr;

bool T5P4Board::init()
{
    s_active_board_ = this;
    return init_i2c_bus() && init_touch() && init_display();
}

bool T5P4Board::init_i2c_bus()
{
    if (i2c_bus_ != nullptr) {
        return true;
    }

    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize shared I2C bus");
        return false;
    }

    i2c_bus_ = bsp_i2c_get_handle();
    if (i2c_bus_ == nullptr) {
        ESP_LOGE(kTag, "Shared I2C handle unavailable");
        return false;
    }

    bbepSetI2CMasterBus(i2c_bus_);
    ESP_LOGI(kTag, "Shared I2C bus ready");
    return true;
}

bool T5P4Board::init_touch()
{
    if (touch_ != nullptr) {
        return true;
    }

    esp_err_t err = t5_board_touch_new(kPanelWidth, kPanelHeight, &touch_, &touch_io_, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize GT911: %s", esp_err_to_name(err));
        touch_ = nullptr;
        touch_io_ = nullptr;
        return false;
    }

    return true;
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
    (void)s_active_board_;
    configure_native_gpio(pin, mode);
}

void T5P4Board::platform_digital_write(uint32_t pin, uint8_t value)
{
    (void)s_active_board_;
    gpio_set_level((gpio_num_t)pin, value);
}

int T5P4Board::platform_digital_read(uint32_t pin)
{
    (void)s_active_board_;
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
