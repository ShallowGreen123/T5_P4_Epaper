/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/bsp_board_extra.h"
#include "bq25896_reg.h"
#include "driver/i2c_master.h"
#include "usb_device_uac.h"

static const char *TAG = "usb_uac_main";

static esp_err_t disable_otg_vbus_output(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C bus is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ25896_I2C_ADDR_7BIT_DEFAULT,
        .scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    i2c_master_dev_handle_t charger = NULL;
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_config, &charger);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to access BQ25896: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t reg_addr = BQ25896_REG_03;
    uint8_t reg03 = 0;
    ret = i2c_master_transmit_receive(charger, &reg_addr, sizeof(reg_addr), &reg03, sizeof(reg03), 100);
    if (ret == ESP_OK && (reg03 & BQ25896_REG03_OTG_CONFIG_MASK) != 0) {
        const uint8_t write_buf[] = {
            BQ25896_REG_03,
            (uint8_t)(reg03 & (uint8_t)~BQ25896_REG03_OTG_CONFIG_MASK),
        };
        ret = i2c_master_transmit(charger, write_buf, sizeof(write_buf), 100);
    }

    const esp_err_t remove_ret = i2c_master_bus_rm_device(charger);
    if (ret == ESP_OK) {
        ret = remove_ret;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable BQ25896 OTG boost: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BQ25896 OTG boost disabled; USB VBUS is supplied by the host");
    return ESP_OK;
}

static esp_err_t uac_device_output_cb(uint8_t *buf, size_t len, void *arg)
{
    size_t bytes_written = 0;
    esp_err_t ret = bsp_extra_i2s_write(buf, len, &bytes_written, 0);
    return ret == ESP_OK && bytes_written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t uac_device_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *arg)
{
    esp_err_t ret = bsp_extra_i2s_read(buf, len, bytes_read, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s read failed");
    }
    return ret;
}

static void uac_device_set_mute_cb(uint32_t mute, void *arg)
{
    ESP_LOGI(TAG, "uac_device_set_mute_cb: %"PRIu32"", mute);
    bsp_extra_codec_mute_set(mute);
}

static void uac_device_set_volume_cb(uint32_t volume, void *arg)
{
    ESP_LOGI(TAG, "uac_device_set_volume_cb: %"PRIu32"", volume);
    bsp_extra_codec_volume_set(volume, NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(disable_otg_vbus_output());

    i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_UAC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };
    // GPIO43 MCLK prevents the ESP32-P4 HS PHY from completing enumeration on this board.
    // ES8311 can derive its internal master clock from BCLK in this mode.
    i2s_config.gpio_cfg.mclk = I2S_GPIO_UNUSED;

    ESP_ERROR_CHECK(bsp_audio_init(&i2s_config));
    ESP_ERROR_CHECK(bsp_extra_codec_init());
    ESP_ERROR_CHECK(bsp_extra_codec_set_fs(CONFIG_UAC_SAMPLE_RATE, 16, CONFIG_UAC_SPEAKER_CHANNEL_NUM));

    uac_device_config_t config = {
        .output_cb = uac_device_output_cb,
        .input_cb = uac_device_input_cb,
        .set_mute_cb = uac_device_set_mute_cb,
        .set_volume_cb = uac_device_set_volume_cb,
        .cb_ctx = NULL,
    };

    ESP_ERROR_CHECK(uac_device_init(&config));
}
