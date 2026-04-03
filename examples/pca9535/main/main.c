/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_io_expander.h"
#include "esp_io_expander_pca9535.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef BOARD_I2C_SDA
#define BOARD_I2C_SDA 7
#endif

#ifndef BOARD_I2C_SCL
#define BOARD_I2C_SCL 8
#endif

#ifndef BOARD_I2C_NUM
#define BOARD_I2C_NUM I2C_NUM_0
#endif

#define EXAMPLE_PCA9535_I2C_ADDR         0x20
#define EXAMPLE_PCA9535_ALL_PIN_MASK     0x0000FFFFUL
#define EXAMPLE_PCA9535_POLL_INTERVAL_MS 500
#define EXAMPLE_I2C_TIMEOUT_MS           100

static const char *TAG = "pca9535";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;

static void byte_to_binary_string(uint8_t value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 9) {
        return;
    }

    for (int bit = 7; bit >= 0; --bit) {
        buffer[7 - bit] = (value & (1U << bit)) ? '1' : '0';
    }
    buffer[8] = '\0';
}

static void scan_i2c_bus(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus on SDA=%d SCL=%d", BOARD_I2C_SDA, BOARD_I2C_SCL);
    for (uint8_t address = 1; address < 0x7F; ++address) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, address, EXAMPLE_I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", address);
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Probe timeout at 0x%02X, check pull-ups or bus wiring", address);
            break;
        }
    }
}

static esp_err_t pca9535_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_NUM,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "create I2C master bus failed");

    esp_err_t err = i2c_master_probe(s_i2c_bus, EXAMPLE_PCA9535_I2C_ADDR, EXAMPLE_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9535 probe failed at 0x%02X: %s",
                 EXAMPLE_PCA9535_I2C_ADDR, esp_err_to_name(err));
        scan_i2c_bus();
        return err;
    }

    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_pca9535(s_i2c_bus, ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000, &s_io_expander),
        TAG,
        "create PCA9535 expander failed");

    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_dir(s_io_expander, EXAMPLE_PCA9535_ALL_PIN_MASK, IO_EXPANDER_INPUT),
        TAG,
        "set PCA9535 pins to input failed");

    ESP_LOGI(TAG, "PCA9535 initialized on address 0x%02X", EXAMPLE_PCA9535_I2C_ADDR);
    esp_io_expander_print_state(s_io_expander);
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting PCA9535 ESP-IDF polling example");
    ESP_LOGI(TAG, "I2C config: port=%d SDA=%d SCL=%d", BOARD_I2C_NUM, BOARD_I2C_SDA, BOARD_I2C_SCL);

    ESP_ERROR_CHECK(pca9535_init());

    while (1) {
        uint32_t io_level_mask = 0;
        char port0_bits[9];
        char port1_bits[9];

        esp_err_t err = esp_io_expander_get_level(s_io_expander, EXAMPLE_PCA9535_ALL_PIN_MASK, &io_level_mask);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Read PCA9535 IO state failed: %s", esp_err_to_name(err));
        } else {
            uint8_t port0 = (uint8_t)(io_level_mask & 0xFF);
            uint8_t port1 = (uint8_t)((io_level_mask >> 8) & 0xFF);

            byte_to_binary_string(port0, port0_bits, sizeof(port0_bits));
            byte_to_binary_string(port1, port1_bits, sizeof(port1_bits));

            ESP_LOGI(TAG,
                     "IO state: all=0x%04" PRIX32 " PORT1=0b%s PORT0=0b%s",
                     io_level_mask & EXAMPLE_PCA9535_ALL_PIN_MASK,
                     port1_bits,
                     port0_bits);
        }

        vTaskDelay(pdMS_TO_TICKS(EXAMPLE_PCA9535_POLL_INTERVAL_MS));
    }
}
