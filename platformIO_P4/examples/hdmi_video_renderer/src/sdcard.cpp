#include "sdcard.h"

#include <Arduino.h>

#include "hdmi_config.h"

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static constexpr const char *kMountPoint = "/sdcard";
static constexpr uint32_t kSdSpiFreqsKhz[] = {20000, 10000, 4000};

static bool tryMountSdCard(uint32_t freq_khz, sdmmc_card_t **out_card)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = freq_khz;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = BOARD_SD_MOSI;
    bus_cfg.miso_io_num = BOARD_SD_MISO;
    bus_cfg.sclk_io_num = BOARD_SD_SCK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 64 * 1024;

    esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[sd] spi_bus_initialize failed at %lu kHz: %s\n", (unsigned long)freq_khz, esp_err_to_name(err));
        return false;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = (gpio_num_t)BOARD_SD_CS;
    slot_cfg.host_id = static_cast<spi_host_device_t>(host.slot);

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files = 4;
    mount_cfg.allocation_unit_size = 16 * 1024;

    err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_cfg, &mount_cfg, out_card);
    if (err == ESP_OK) {
        Serial.printf("[sd] mount OK at %lu kHz\n", (unsigned long)freq_khz);
        return true;
    }

    Serial.printf("[sd] mount failed at %lu kHz: %s\n", (unsigned long)freq_khz, esp_err_to_name(err));
    spi_bus_free(static_cast<spi_host_device_t>(host.slot));
    return false;
}

bool SdCard::mount()
{
    if (card_) {
        return true;
    }

    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);
    delay(2);

    sdmmc_card_t *card = nullptr;
    for (uint32_t freq_khz : kSdSpiFreqsKhz) {
        if (tryMountSdCard(freq_khz, &card)) {
            card_ = card;
            return true;
        }
    }
    return false;
}

void SdCard::unmount()
{
    if (!card_) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(kMountPoint, static_cast<sdmmc_card_t *>(card_));
    card_ = nullptr;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_free(static_cast<spi_host_device_t>(host.slot));
}

bool SdCard::mounted() const
{
    return card_ != nullptr;
}

