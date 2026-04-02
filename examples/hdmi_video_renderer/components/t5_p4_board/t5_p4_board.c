/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_lt8912b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp_err_check.h"

#ifndef CONFIG_BSP_LCD_LT8912B_MIPI_PN_SWAP
#define CONFIG_BSP_LCD_LT8912B_MIPI_PN_SWAP 0
#endif

#ifndef CONFIG_BSP_LCD_LT8912B_MIPI_LANE_SWAP
#define CONFIG_BSP_LCD_LT8912B_MIPI_LANE_SWAP 0
#endif

#ifndef CONFIG_BSP_LCD_LT8912B_TEST_PATTERN
#define CONFIG_BSP_LCD_LT8912B_TEST_PATTERN 0
#endif

#define PCA9535_INPUT_PORT_0_REG      0x00
#define PCA9535_OUTPUT_PORT_0_REG     0x02
#define PCA9535_CONFIG_PORT_0_REG     0x06

#define I2C_OP_TIMEOUT_MS             100
#define HDMI_ENABLE_DELAY_MS          10
#define HDMI_RESET_LOW_DELAY_MS       50
#define HDMI_POWER_STABLE_DELAY_MS    120
#define HDMI_READY_RETRY_COUNT        10
#define HDMI_READY_RETRY_DELAY_MS     100
#define AUDIO_AMP_STABLE_DELAY_MS     10

#if CONFIG_BSP_LCD_TYPE_HDMI && !CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#error "The current T5-P4 HDMI path requires CONFIG_BSP_LCD_COLOR_FORMAT_RGB888=y"
#endif

static const char *TAG = "t5_p4_board";

static bool s_i2c_initialized;
static bool s_pca9535_initialized;
static bool s_sdspi_initialized;
static bool s_hdmi_powered;
static bool s_hdmi_int_gpio_configured;

static i2c_master_bus_handle_t s_i2c_handle;
static i2c_master_dev_handle_t s_pca9535_dev;
static sdmmc_card_t *s_sdcard;
static i2s_chan_handle_t s_i2s_tx_chan;
static i2s_chan_handle_t s_i2s_rx_chan;
static const audio_codec_data_if_t *s_i2s_data_if;
static bsp_lcd_handles_t s_display_handles;
static esp_ldo_channel_handle_t s_dsi_phy_ldo_chan;
static uint8_t s_pca_output_state[2];
static uint8_t s_pca_config_state[2] = {0xFF, 0xFF};

static void log_sdspi_pin_levels(const char *stage)
{
    ESP_LOGI(TAG,
             "SD SPI %s: host=SPI%d miso=%d(%d) clk=%d(%d) mosi=%d(%d) cs=%d(%d)",
             stage, BSP_SDSPI_HOST + 1,
             BSP_SD_SPI_MISO, gpio_get_level(BSP_SD_SPI_MISO),
             BSP_SD_SPI_CLK, gpio_get_level(BSP_SD_SPI_CLK),
             BSP_SD_SPI_MOSI, gpio_get_level(BSP_SD_SPI_MOSI),
             BSP_SD_SPI_CS, gpio_get_level(BSP_SD_SPI_CS));
}

static esp_err_t prepare_sdspi_pins(void)
{
    const gpio_num_t pins[] = {
        BSP_SD_SPI_MISO,
        BSP_SD_SPI_CLK,
        BSP_SD_SPI_MOSI,
        BSP_SD_SPI_CS,
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_input_enable(pins[i]), TAG, "Enable SD SPI GPIO input failed");
        ESP_RETURN_ON_ERROR(gpio_pullup_en(pins[i]), TAG, "Enable SD SPI pull-up failed");
        ESP_RETURN_ON_ERROR(gpio_pulldown_dis(pins[i]), TAG, "Disable SD SPI pull-down failed");
    }

    log_sdspi_pin_levels("before-init");
    return ESP_OK;
}

static esp_err_t ensure_i2c_bus(void)
{
    return bsp_i2c_init();
}

static esp_err_t validate_pca9535_io(uint8_t io_num)
{
    return (io_num < 16) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t pca9535_write_registers(uint8_t start_reg, const uint8_t *data, size_t size)
{
    uint8_t buffer[3];

    if (s_pca9535_dev == NULL || data == NULL || size == 0 || size > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = start_reg;
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(s_pca9535_dev, buffer, size + 1, I2C_OP_TIMEOUT_MS);
}

static esp_err_t pca9535_read_registers(uint8_t start_reg, uint8_t *data, size_t size)
{
    if (s_pca9535_dev == NULL || data == NULL || size == 0 || size > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(s_pca9535_dev, &start_reg, 1, data, size, I2C_OP_TIMEOUT_MS);
}

static void log_i2c_probe(const char *name, uint8_t address)
{
    if (s_i2c_handle == NULL) {
        return;
    }

    const esp_err_t ret = i2c_master_probe(s_i2c_handle, address, I2C_OP_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s detected at 0x%02X", name, address);
    } else {
        ESP_LOGW(TAG, "%s probe at 0x%02X failed: %s", name, address, esp_err_to_name(ret));
    }
}

static void display_handles_reset(bsp_lcd_handles_t *handles)
{
    if (handles == NULL) {
        return;
    }

    if (handles->panel) {
        esp_lcd_panel_del(handles->panel);
    }
    if (handles->io) {
        esp_lcd_panel_io_del(handles->io);
    }
    if (handles->io_cec) {
        esp_lcd_panel_io_del(handles->io_cec);
    }
    if (handles->io_avi) {
        esp_lcd_panel_io_del(handles->io_avi);
    }
    if (handles->mipi_dsi_bus) {
        esp_lcd_del_dsi_bus(handles->mipi_dsi_bus);
    }

    memset(handles, 0, sizeof(*handles));
}

static esp_err_t ensure_hdmi_int_gpio(void)
{
    if (s_hdmi_int_gpio_configured) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << T5_BOARD_HDMI_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Configure HDMI INT GPIO failed");
    s_hdmi_int_gpio_configured = true;
    return ESP_OK;
}

static int get_pca_input_level(const uint8_t inputs[2], uint8_t io_num)
{
    const uint8_t port = io_num / 8;
    const uint8_t bit = 1U << (io_num % 8);
    return (inputs[port] & bit) ? 1 : 0;
}

static void log_hdmi_power_debug_state(const char *stage)
{
    uint8_t inputs[2] = {0};
    int hdmi_int_level = -1;
    esp_err_t pca_ret = ESP_ERR_INVALID_STATE;

    if (s_pca9535_initialized && (s_pca9535_dev != NULL)) {
        pca_ret = pca9535_read_registers(PCA9535_INPUT_PORT_0_REG, inputs, sizeof(inputs));
        if (pca_ret != ESP_OK) {
            ESP_LOGW(TAG, "HDMI %s: read PCA9535 input state failed: %s", stage, esp_err_to_name(pca_ret));
        }
    }

    esp_err_t gpio_ret = ensure_hdmi_int_gpio();
    if (gpio_ret == ESP_OK) {
        hdmi_int_level = gpio_get_level(T5_BOARD_HDMI_INT_GPIO);
    } else {
        ESP_LOGW(TAG, "HDMI %s: configure HDMI INT GPIO failed: %s", stage, esp_err_to_name(gpio_ret));
    }

    ESP_LOGI(TAG,
             "HDMI %s: powered=%s out=0x%02X/0x%02X dir=0x%02X/0x%02X in=0x%02X/0x%02X io10_1v8=%d io7_en=%d io6_rst=%d int_gpio=%d",
             stage, s_hdmi_powered ? "yes" : "no",
             s_pca_output_state[0], s_pca_output_state[1],
             s_pca_config_state[0], s_pca_config_state[1],
             inputs[0], inputs[1],
             (pca_ret == ESP_OK) ? get_pca_input_level(inputs, T5_BOARD_PCA_IO_HDMI_1V8_ENABLE) : -1,
             (pca_ret == ESP_OK) ? get_pca_input_level(inputs, T5_BOARD_PCA_IO_HDMI_ENABLE) : -1,
             (pca_ret == ESP_OK) ? get_pca_input_level(inputs, T5_BOARD_PCA_IO_HDMI_RESET) : -1,
             hdmi_int_level);
}

static esp_err_t bsp_enable_dsi_phy_power(void)
{
    if (s_dsi_phy_ldo_chan != NULL) {
        return ESP_OK;
    }

#if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_phy_ldo_chan), TAG,
                        "Acquire LDO channel for DSI PHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY powered on");
#endif

    return ESP_OK;
}

static void bsp_disable_dsi_phy_power(void)
{
    if (s_dsi_phy_ldo_chan != NULL) {
        esp_ldo_release_channel(s_dsi_phy_ldo_chan);
        s_dsi_phy_ldo_chan = NULL;
    }
}

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &s_i2c_handle), TAG, "Create I2C master bus failed");

    s_i2c_initialized = true;
    ESP_LOGI(TAG, "I2C ready on SDA=%d SCL=%d, port=%d, clk=%d Hz",
             BSP_I2C_SDA, BSP_I2C_SCL, BSP_I2C_NUM, CONFIG_BSP_I2C_CLK_SPEED_HZ);
    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (s_pca9535_dev != NULL) {
        BSP_ERROR_CHECK_RETURN_ERR(i2c_master_bus_rm_device(s_pca9535_dev));
        s_pca9535_dev = NULL;
        s_pca9535_initialized = false;
    }

    if (s_i2c_initialized && s_i2c_handle != NULL) {
        BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(s_i2c_handle));
        s_i2c_handle = NULL;
        s_i2c_initialized = false;
    }

    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    return s_i2c_handle;
}

esp_err_t t5_board_pca9535_init(void)
{
    if (s_pca9535_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), TAG, "I2C init failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = T5_BOARD_I2C_ADDR_PCA9535,
        .scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_handle, &dev_cfg, &s_pca9535_dev), TAG,
                        "Add PCA9535 to I2C bus failed");

    esp_err_t ret = i2c_master_probe(s_i2c_handle, T5_BOARD_I2C_ADDR_PCA9535, I2C_OP_TIMEOUT_MS);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(s_pca9535_dev);
        s_pca9535_dev = NULL;
        return ret;
    }

    memset(s_pca_output_state, 0, sizeof(s_pca_output_state));
    s_pca_config_state[0] = 0xFF;
    s_pca_config_state[1] = 0xFF;

    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_OUTPUT_PORT_0_REG, s_pca_output_state,
                                                sizeof(s_pca_output_state)),
                        TAG, "Initialize PCA9535 outputs failed");
    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_CONFIG_PORT_0_REG, s_pca_config_state,
                                                sizeof(s_pca_config_state)),
                        TAG, "Initialize PCA9535 directions failed");

    s_pca9535_initialized = true;
    log_i2c_probe("PCA9535", T5_BOARD_I2C_ADDR_PCA9535);
    return ESP_OK;
}

esp_err_t t5_board_pca9535_set_level(uint8_t io_num, bool level)
{
    uint8_t port = 0;
    uint8_t bit = 0;

    ESP_RETURN_ON_ERROR(validate_pca9535_io(io_num), TAG, "Invalid PCA9535 IO");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_init(), TAG, "PCA9535 init failed");

    port = io_num / 8;
    bit = 1U << (io_num % 8);

    if (level) {
        s_pca_output_state[port] |= bit;
    } else {
        s_pca_output_state[port] &= (uint8_t)~bit;
    }
    s_pca_config_state[port] &= (uint8_t)~bit;

    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_OUTPUT_PORT_0_REG, s_pca_output_state,
                                                sizeof(s_pca_output_state)),
                        TAG, "Write PCA9535 output state failed");
    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_CONFIG_PORT_0_REG, s_pca_config_state,
                                                sizeof(s_pca_config_state)),
                        TAG, "Write PCA9535 direction state failed");
    return ESP_OK;
}

esp_err_t t5_board_pca9535_get_level(uint8_t io_num, bool *level)
{
    uint8_t inputs[2] = {0};
    uint8_t port = 0;
    uint8_t bit = 0;

    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(validate_pca9535_io(io_num), TAG, "Invalid PCA9535 IO");
    ESP_RETURN_ON_ERROR(t5_board_pca9535_init(), TAG, "PCA9535 init failed");
    ESP_RETURN_ON_ERROR(pca9535_read_registers(PCA9535_INPUT_PORT_0_REG, inputs, sizeof(inputs)),
                        TAG, "Read PCA9535 input state failed");

    port = io_num / 8;
    bit = 1U << (io_num % 8);
    *level = (inputs[port] & bit) != 0;
    return ESP_OK;
}

esp_err_t t5_board_audio_amp_enable(bool enable)
{
    ESP_RETURN_ON_ERROR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_AUDIO_SHUTDOWN, enable), TAG,
                        "Set audio amp state failed");
    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_STABLE_DELAY_MS));
    return ESP_OK;
}

esp_err_t t5_board_hdmi_power_on(void)
{
    if (s_hdmi_powered) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(t5_board_pca9535_init(), TAG, "PCA9535 init failed");

    ESP_RETURN_ON_ERROR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_1V8_ENABLE, true), TAG,
                        "Enable HDMI 1.8V failed");
    vTaskDelay(pdMS_TO_TICKS(HDMI_ENABLE_DELAY_MS));

    ESP_RETURN_ON_ERROR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_ENABLE, true), TAG,
                        "Enable HDMI bridge failed");
    vTaskDelay(pdMS_TO_TICKS(HDMI_ENABLE_DELAY_MS));

    ESP_RETURN_ON_ERROR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_RESET, false), TAG,
                        "Assert HDMI reset failed");
    vTaskDelay(pdMS_TO_TICKS(HDMI_RESET_LOW_DELAY_MS));

    ESP_RETURN_ON_ERROR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_RESET, true), TAG,
                        "Release HDMI reset failed");
    vTaskDelay(pdMS_TO_TICKS(HDMI_POWER_STABLE_DELAY_MS));

    log_i2c_probe("LT8912B main", T5_BOARD_I2C_ADDR_LT8912B_MAIN);
    log_i2c_probe("LT8912B CEC", T5_BOARD_I2C_ADDR_LT8912B_CEC);
    log_i2c_probe("LT8912B AVI", T5_BOARD_I2C_ADDR_LT8912B_AVI);

    s_hdmi_powered = true;
    log_hdmi_power_debug_state("after-power-on");
    return ESP_OK;
}

esp_err_t t5_board_hdmi_power_off(void)
{
    if (!s_pca9535_initialized || !s_hdmi_powered) {
        s_hdmi_powered = false;
        return ESP_OK;
    }

    BSP_ERROR_CHECK_RETURN_ERR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_RESET, false));
    BSP_ERROR_CHECK_RETURN_ERR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_ENABLE, false));
    BSP_ERROR_CHECK_RETURN_ERR(t5_board_pca9535_set_level(T5_BOARD_PCA_IO_HDMI_1V8_ENABLE, false));
    s_hdmi_powered = false;
    log_hdmi_power_debug_state("after-power-off");
    return ESP_OK;
}

sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    return s_sdcard;
}

void bsp_sdcard_get_sdspi_host(const int slot, sdmmc_host_t *config)
{
    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();

    assert(config != NULL);
    host_config.slot = slot;
    memcpy(config, &host_config, sizeof(host_config));
}

void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config)
{
    assert(config != NULL);
    memset(config, 0, sizeof(*config));

    config->gpio_cs = BSP_SD_SPI_CS;
    config->gpio_cd = SDSPI_SLOT_NO_CD;
    config->gpio_wp = SDSPI_SLOT_NO_WP;
    config->gpio_int = GPIO_NUM_NC;
    config->gpio_wp_polarity = SDSPI_IO_ACTIVE_LOW;
    config->host_id = spi_host;
}

esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg)
{
    sdmmc_host_t sdhost = {0};
    sdspi_device_config_t sdslot = {0};
    bool initialized_here = false;
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 64 * 1024,
    };

    assert(cfg != NULL);

    if (s_sdcard != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card over SPI at %s", BSP_SD_MOUNT_POINT);
    ESP_LOGI(TAG, "SD SPI pins: MISO=%d CLK=%d MOSI=%d CS=%d",
             BSP_SD_SPI_MISO, BSP_SD_SPI_CLK, BSP_SD_SPI_MOSI, BSP_SD_SPI_CS);
    ESP_RETURN_ON_ERROR(prepare_sdspi_pins(), TAG, "Prepare SD SPI pins failed");

    if (!s_sdspi_initialized) {
        const spi_bus_config_t buscfg = {
            .sclk_io_num = BSP_SD_SPI_CLK,
            .mosi_io_num = BSP_SD_SPI_MOSI,
            .miso_io_num = BSP_SD_SPI_MISO,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = 4096,
        };
        ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_SDSPI_HOST, &buscfg, SDSPI_DEFAULT_DMA), TAG,
                            "Initialize SPI bus for SD card failed");
        s_sdspi_initialized = true;
        initialized_here = true;
    }

    if (cfg->mount == NULL) {
        cfg->mount = &mount_config;
    }
    if (cfg->host == NULL) {
        bsp_sdcard_get_sdspi_host(BSP_SDSPI_HOST, &sdhost);
        cfg->host = &sdhost;
    }
    if (cfg->slot.sdspi == NULL) {
        bsp_sdcard_sdspi_get_slot(BSP_SDSPI_HOST, &sdslot);
        cfg->slot.sdspi = &sdslot;
    }

    esp_err_t ret = esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, cfg->host, cfg->slot.sdspi,
                                            cfg->mount, &s_sdcard);
    if (ret != ESP_OK && initialized_here) {
        spi_bus_free(BSP_SDSPI_HOST);
        s_sdspi_initialized = false;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted over SPI at %s", BSP_SD_MOUNT_POINT);
        log_sdspi_pin_levels("after-mount");
    } else {
        ESP_LOGE(TAG, "SD card mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        log_sdspi_pin_levels("after-failure");
        if (ret == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGW(TAG, "Invalid SD response during CMD8 probe. Check card insertion, SD pull-ups, pin mapping, and board-specific SD power.");
        }
    }
    return ret;
}

esp_err_t bsp_sdcard_mount(void)
{
    bsp_sdcard_cfg_t cfg = {0};
    return bsp_sdcard_sdspi_mount(&cfg);
}

esp_err_t bsp_sdcard_unmount(void)
{
    esp_err_t ret = ESP_OK;

    if (s_sdcard != NULL) {
        ret |= esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, s_sdcard);
        s_sdcard = NULL;
    }
    if (s_sdspi_initialized) {
        ret |= spi_bus_free(BSP_SDSPI_HOST);
        s_sdspi_initialized = false;
    }

    return ret;
}

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (s_i2s_tx_chan != NULL && s_i2s_rx_chan != NULL && s_i2s_data_if != NULL) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, &s_i2s_rx_chan), TAG,
                        "Create I2S channels failed");

    const i2s_std_config_t default_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(22050),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };
    const i2s_std_config_t *cfg = (i2s_config != NULL) ? i2s_config : &default_cfg;

    if (s_i2s_tx_chan != NULL) {
        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx_chan, cfg), TAG, "Init I2S TX failed");
        ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_chan), TAG, "Enable I2S TX failed");
    }
    if (s_i2s_rx_chan != NULL) {
        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx_chan, cfg), TAG, "Init I2S RX failed");
        ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx_chan), TAG, "Enable I2S RX failed");
    }

    audio_codec_i2s_cfg_t codec_i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .tx_handle = s_i2s_tx_chan,
        .rx_handle = s_i2s_rx_chan,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&codec_i2s_cfg);
    if (s_i2s_data_if == NULL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "I2S prepared: mclk=%d bclk=%d lrck=%d dout=%d din=%d",
             BSP_I2S_MCLK, BSP_I2S_BCLK, BSP_I2S_LRCK, BSP_I2S_DOUT, BSP_I2S_DIN);
    return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    if (s_i2s_data_if == NULL) {
        if (bsp_i2c_init() != ESP_OK || bsp_audio_init(NULL) != ESP_OK) {
            return NULL;
        }
    }

    if (t5_board_audio_amp_enable(true) != ESP_OK) {
        return NULL;
    }

    log_i2c_probe("ES8311", T5_BOARD_I2C_ADDR_ES8311);
    ESP_LOGI(TAG, "Configuring ES8311 via codec default addr 0x%02X (7-bit address 0x%02X)",
             ES8311_CODEC_DEFAULT_ADDR, T5_BOARD_I2C_ADDR_ES8311);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL) {
        t5_board_audio_amp_enable(false);
        return NULL;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_handle,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (i2c_ctrl_if == NULL) {
        t5_board_audio_amp_enable(false);
        return NULL;
    }

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_TYPE_OUT,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    if (es8311_dev == NULL) {
        t5_board_audio_amp_enable(false);
        return NULL;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };
    esp_codec_dev_handle_t codec_dev = esp_codec_dev_new(&codec_dev_cfg);
    if (codec_dev == NULL) {
        t5_board_audio_amp_enable(false);
    }

    return codec_dev;
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    if (s_i2s_data_if == NULL) {
        if (bsp_i2c_init() != ESP_OK || bsp_audio_init(NULL) != ESP_OK) {
            return NULL;
        }
    }

    log_i2c_probe("ES8311", T5_BOARD_I2C_ADDR_ES8311);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL) {
        return NULL;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_handle,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (i2c_ctrl_if == NULL) {
        return NULL;
    }

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    if (es8311_dev == NULL) {
        return NULL;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };
    return esp_codec_dev_new(&codec_dev_cfg);
}

esp_err_t bsp_display_brightness_init(void)
{
    return ESP_OK;
}

esp_err_t bsp_display_brightness_deinit(void)
{
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    (void)brightness_percent;
    return ESP_OK;
}

esp_err_t bsp_display_backlight_on(void)
{
    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    return ESP_OK;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    bsp_lcd_handles_t handles = {0};

    ESP_RETURN_ON_FALSE(config != NULL && ret_panel != NULL && ret_io != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid display arguments");

    esp_err_t ret = bsp_display_new_with_handles(config, &handles);
    if (ret == ESP_OK) {
        *ret_panel = handles.panel;
        *ret_io = handles.io;
    }
    return ret;
}

esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles)
{
    esp_err_t ret = ESP_OK;
    bsp_lcd_handles_t handles = {0};
    bsp_hdmi_resolution_t resolution = BSP_HDMI_DEFAULT_RESOLUTION;

    ESP_RETURN_ON_FALSE(config != NULL && ret_handles != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid display arguments");
    memset(ret_handles, 0, sizeof(*ret_handles));

    if (s_display_handles.panel != NULL) {
        *ret_handles = s_display_handles;
        return ESP_OK;
    }

#if !CONFIG_BSP_LCD_TYPE_HDMI
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (config->hdmi_resolution != BSP_HDMI_RES_NONE) {
        resolution = config->hdmi_resolution;
    }

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Display brightness init failed");
    ESP_GOTO_ON_ERROR(bsp_enable_dsi_phy_power(), err, TAG, "Enable DSI PHY power failed");

    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = config->dsi_bus.phy_clk_src,
        .lane_bit_rate_mbps = config->dsi_bus.lane_bit_rate_mbps,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &handles.mipi_dsi_bus), err, TAG, "New DSI bus failed");

    ESP_GOTO_ON_ERROR(bsp_i2c_init(), err, TAG, "I2C init failed");
    ESP_GOTO_ON_ERROR(t5_board_hdmi_power_on(), err, TAG, "HDMI power sequence failed");

    esp_lcd_panel_io_i2c_config_t io_config =
        LT8912B_IO_CFG(CONFIG_BSP_I2C_CLK_SPEED_HZ, LT8912B_IO_I2C_MAIN_ADDRESS);
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_handle, &io_config, &handles.io), err, TAG,
                      "Create LT8912B main IO failed");

    esp_lcd_panel_io_i2c_config_t io_cec_config =
        LT8912B_IO_CFG(CONFIG_BSP_I2C_CLK_SPEED_HZ, LT8912B_IO_I2C_CEC_ADDRESS);
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_handle, &io_cec_config, &handles.io_cec), err, TAG,
                      "Create LT8912B CEC IO failed");

    esp_lcd_panel_io_i2c_config_t io_avi_config =
        LT8912B_IO_CFG(CONFIG_BSP_I2C_CLK_SPEED_HZ, LT8912B_IO_I2C_AVI_ADDRESS);
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_handle, &io_avi_config, &handles.io_avi), err, TAG,
                      "Create LT8912B AVI IO failed");

    esp_lcd_dpi_panel_config_t dpi_configs[] = {
        LT8912B_800x600_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
        LT8912B_1024x768_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
        LT8912B_1280x720_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
        LT8912B_1280x800_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
        LT8912B_1920x1080_PANEL_30HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
    };
    const esp_lcd_panel_lt8912b_video_timing_t video_timings[] = {
        ESP_LCD_LT8912B_VIDEO_TIMING_800x600_60Hz(),
        ESP_LCD_LT8912B_VIDEO_TIMING_1024x768_60Hz(),
        ESP_LCD_LT8912B_VIDEO_TIMING_1280x720_60Hz(),
        ESP_LCD_LT8912B_VIDEO_TIMING_1280x800_60Hz(),
        ESP_LCD_LT8912B_VIDEO_TIMING_1920x1080_30Hz(),
    };
    lt8912b_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = handles.mipi_dsi_bus,
            .lane_num = BSP_LCD_MIPI_DSI_LANE_NUM,
        },
    };

    switch (resolution) {
    case BSP_HDMI_RES_800x600:
        vendor_config.mipi_config.dpi_config = &dpi_configs[0];
        memcpy(&vendor_config.video_timing, &video_timings[0], sizeof(vendor_config.video_timing));
        break;
    case BSP_HDMI_RES_1024x768:
        vendor_config.mipi_config.dpi_config = &dpi_configs[1];
        memcpy(&vendor_config.video_timing, &video_timings[1], sizeof(vendor_config.video_timing));
        break;
    case BSP_HDMI_RES_1280x720:
        vendor_config.mipi_config.dpi_config = &dpi_configs[2];
        memcpy(&vendor_config.video_timing, &video_timings[2], sizeof(vendor_config.video_timing));
        break;
    case BSP_HDMI_RES_1280x800:
        vendor_config.mipi_config.dpi_config = &dpi_configs[3];
        memcpy(&vendor_config.video_timing, &video_timings[3], sizeof(vendor_config.video_timing));
        break;
    case BSP_HDMI_RES_1920x1080:
        vendor_config.mipi_config.dpi_config = &dpi_configs[4];
        memcpy(&vendor_config.video_timing, &video_timings[4], sizeof(vendor_config.video_timing));
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_INVALID_ARG, err, TAG, "Unsupported HDMI resolution");
    }

    const esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 24,
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .reset_gpio_num = -1,
        .vendor_config = &vendor_config,
    };
    const esp_lcd_panel_lt8912b_io_t io_all = {
        .main = handles.io,
        .cec_dsi = handles.io_cec,
        .avi = handles.io_avi,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_lt8912b(&io_all, &panel_config, &handles.panel), err, TAG,
                      "Create LT8912B panel failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(handles.panel), err, TAG, "LT8912B panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(handles.panel), err, TAG, "LT8912B panel init failed");

    bool hdmi_ready = false;
    for (int attempt = 0; attempt < HDMI_READY_RETRY_COUNT; ++attempt) {
        hdmi_ready = esp_lcd_panel_lt8912b_is_ready(handles.panel);
        ESP_LOGI(TAG, "LT8912B ready check %d/%d: %s",
                 attempt + 1, HDMI_READY_RETRY_COUNT, hdmi_ready ? "ready" : "not ready");
        if (hdmi_ready) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(HDMI_READY_RETRY_DELAY_MS));
    }

    ESP_LOGI(TAG, "LT8912B HPD/ready after init: %s", hdmi_ready ? "ready" : "not ready");
    ESP_LOGI(TAG, "LT8912B diagnostics config: test_pattern=%d pn_swap=%d lane_swap=%d",
             CONFIG_BSP_LCD_LT8912B_TEST_PATTERN,
             CONFIG_BSP_LCD_LT8912B_MIPI_PN_SWAP,
             CONFIG_BSP_LCD_LT8912B_MIPI_LANE_SWAP);
#if CONFIG_BSP_LCD_LT8912B_TEST_PATTERN
    ESP_LOGW(TAG, "LT8912B built-in test pattern is enabled for diagnosis");
#endif
    log_hdmi_power_debug_state("after-panel-init");

    s_display_handles = handles;
    *ret_handles = handles;
    return ESP_OK;

err:
    display_handles_reset(&handles);
    bsp_disable_dsi_phy_power();
    t5_board_hdmi_power_off();
    return ret;
#endif
}

void bsp_display_delete(void)
{
    display_handles_reset(&s_display_handles);
    bsp_disable_dsi_phy_power();
    t5_board_hdmi_power_off();
    bsp_display_brightness_deinit();
}
