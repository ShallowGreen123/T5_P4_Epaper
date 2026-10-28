#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SGM38121_I2C_ADDR          0x28
#define SGM38121_REG_CHIP_REV      0x00
#define SGM38121_REG_DISCH         0x02
#define SGM38121_REG_DVDD1_VOUT    0x03
#define SGM38121_REG_DVDD2_VOUT    0x04
#define SGM38121_REG_AVDD1_VOUT    0x05
#define SGM38121_REG_AVDD2_VOUT    0x06
#define SGM38121_REG_FUNCTION      0x07
#define SGM38121_REG_SEQ_DVDD      0x0A
#define SGM38121_REG_SEQ_AVDD      0x0B
#define SGM38121_REG_ENABLE        0x0E
#define SGM38121_REG_SEQ_CTRL      0x0F
#define SGM38121_REG_COUNT         16

#define SGM38121_ENABLE_DVDD1_BIT  BIT0
#define SGM38121_ENABLE_DVDD2_BIT  BIT1
#define SGM38121_ENABLE_AVDD1_BIT  BIT2
#define SGM38121_ENABLE_AVDD2_BIT  BIT3

#define I2C_TIMEOUT_MS             100

static const char *TAG = "sgm38121";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_sgm = NULL;

static esp_err_t sgm_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_sgm, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

static esp_err_t sgm_read_regs(uint8_t start_reg, uint8_t *buffer, size_t len)
{
    return i2c_master_transmit_receive(s_sgm, &start_reg, 1, buffer, len, I2C_TIMEOUT_MS);
}

static esp_err_t sgm_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(s_sgm, payload, sizeof(payload), I2C_TIMEOUT_MS);
}

static int sgm_decode_dvdd_mv(uint8_t reg_value)
{
    if (reg_value < 0x03 || reg_value > 0x7D) {
        return -1;
    }
    return 504 + (reg_value * 8);
}

static int sgm_decode_avdd_mv(uint8_t reg_value)
{
    if (reg_value < 0x0F) {
        return -1;
    }
    return 1384 + (reg_value * 8);
}

static uint8_t sgm_encode_avdd_mv_rounded(int target_mv, int *actual_mv)
{
    int clamped_mv = target_mv;
    if (clamped_mv < 1504) {
        clamped_mv = 1504;
    }
    if (clamped_mv > 3424) {
        clamped_mv = 3424;
    }

    int reg_value = (clamped_mv - 1384 + 4) / 8;
    if (reg_value < 0x0F) {
        reg_value = 0x0F;
    }
    if (reg_value > 0xFF) {
        reg_value = 0xFF;
    }

    if (actual_mv) {
        *actual_mv = sgm_decode_avdd_mv((uint8_t)reg_value);
    }
    return (uint8_t)reg_value;
}

static void sgm_scan_bus(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus on GPIO%d/GPIO%d", CONFIG_SGM38121_I2C_SDA, CONFIG_SGM38121_I2C_SCL);
    for (uint8_t address = 1; address < 0x7F; ++address) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, address, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at 0x%02X", address);
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Timeout while probing 0x%02X, check bus pull-ups or wiring", address);
            break;
        }
    }
}

static esp_err_t sgm_init_i2c(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_SGM38121_I2C_SDA,
        .scl_io_num = CONFIG_SGM38121_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "create I2C master bus failed");

    esp_err_t err = i2c_master_probe(s_i2c_bus, SGM38121_I2C_ADDR, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SGM38121 probe failed at 0x%02X: %s", SGM38121_I2C_ADDR, esp_err_to_name(err));
        sgm_scan_bus();
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SGM38121_I2C_ADDR,
        .scl_speed_hz = CONFIG_SGM38121_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_sgm), TAG, "add SGM38121 device failed");
    return ESP_OK;
}

static void sgm_dump_regs(void)
{
    uint8_t regs[SGM38121_REG_COUNT] = {0};
    esp_err_t err = sgm_read_regs(0x00, regs, sizeof(regs));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read register dump failed: %s", esp_err_to_name(err));
        return;
    }

    for (size_t offset = 0; offset < sizeof(regs); offset += 8) {
        ESP_LOGI(TAG,
                 "REG[%02X..%02X]: %02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned)offset,
                 (unsigned)(offset + 7),
                 regs[offset + 0],
                 regs[offset + 1],
                 regs[offset + 2],
                 regs[offset + 3],
                 regs[offset + 4],
                 regs[offset + 5],
                 regs[offset + 6],
                 regs[offset + 7]);
    }
}

static void sgm_log_state(const char *label)
{
    uint8_t chip_rev = 0;
    uint8_t discharge = 0;
    uint8_t dvdd1_vout = 0;
    uint8_t dvdd2_vout = 0;
    uint8_t avdd1_vout = 0;
    uint8_t avdd2_vout = 0;
    uint8_t function = 0;
    uint8_t seq_dvdd = 0;
    uint8_t seq_avdd = 0;
    uint8_t enable = 0;
    uint8_t seq_ctrl = 0;

    if (sgm_read_reg(SGM38121_REG_CHIP_REV, &chip_rev) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_DISCH, &discharge) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_DVDD1_VOUT, &dvdd1_vout) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_DVDD2_VOUT, &dvdd2_vout) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_AVDD1_VOUT, &avdd1_vout) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_AVDD2_VOUT, &avdd2_vout) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_FUNCTION, &function) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_SEQ_DVDD, &seq_dvdd) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_SEQ_AVDD, &seq_avdd) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_ENABLE, &enable) != ESP_OK ||
        sgm_read_reg(SGM38121_REG_SEQ_CTRL, &seq_ctrl) != ESP_OK) {
        ESP_LOGE(TAG, "State read failed");
        return;
    }

    ESP_LOGI(TAG,
             "[%s] CHIP_REV=0x%02X DISCH=0x%02X FUNC=0x%02X SEQ_DV=0x%02X SEQ_AV=0x%02X EN=0x%02X SEQ_CTRL=0x%02X",
             label,
             chip_rev,
             discharge,
             function,
             seq_dvdd,
             seq_avdd,
             enable,
             seq_ctrl);

    ESP_LOGI(TAG,
             "[%s] DVDD1 reg=0x%02X (%d mV), DVDD2 reg=0x%02X (%d mV)",
             label,
             dvdd1_vout,
             sgm_decode_dvdd_mv(dvdd1_vout),
             dvdd2_vout,
             sgm_decode_dvdd_mv(dvdd2_vout));

    ESP_LOGI(TAG,
             "[%s] AVDD1 reg=0x%02X (%d mV, %s), AVDD2 reg=0x%02X (%d mV, %s)",
             label,
             avdd1_vout,
             sgm_decode_avdd_mv(avdd1_vout),
             (enable & SGM38121_ENABLE_AVDD1_BIT) ? "enabled" : "disabled",
             avdd2_vout,
             sgm_decode_avdd_mv(avdd2_vout),
             (enable & SGM38121_ENABLE_AVDD2_BIT) ? "enabled" : "disabled");
}

static esp_err_t sgm_apply_config(void)
{
    int avdd1_actual_mv = 0;
    int avdd2_actual_mv = 0;
    uint8_t avdd1_reg = sgm_encode_avdd_mv_rounded(CONFIG_SGM38121_AVDD1_MV, &avdd1_actual_mv);
    uint8_t avdd2_reg = sgm_encode_avdd_mv_rounded(CONFIG_SGM38121_AVDD2_MV, &avdd2_actual_mv);
    uint8_t enable_mask = 0;

    ESP_LOGI(TAG, "Applying SGM38121 register-mode configuration");
    ESP_LOGI(TAG, "Board assumption: AVDD1 -> CAM_1V8, AVDD2 -> CAM_2V8");

    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_SEQ_DVDD, 0x00), TAG, "set DVDD sequence failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_SEQ_AVDD, 0x00), TAG, "set AVDD sequence failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_FUNCTION, 0x00), TAG, "disable wake-up failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_DISCH, 0x00), TAG, "set discharge mode failed");

#if CONFIG_SGM38121_ENABLE_AVDD1
    if (avdd1_actual_mv != CONFIG_SGM38121_AVDD1_MV) {
        ESP_LOGW(TAG,
                 "AVDD1 requested %d mV, rounded to %d mV (reg=0x%02X)",
                 CONFIG_SGM38121_AVDD1_MV,
                 avdd1_actual_mv,
                 avdd1_reg);
    } else {
        ESP_LOGI(TAG, "AVDD1 target %d mV (reg=0x%02X)", avdd1_actual_mv, avdd1_reg);
    }
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_AVDD1_VOUT, avdd1_reg), TAG, "write AVDD1 VOUT failed");
    enable_mask |= SGM38121_ENABLE_AVDD1_BIT;
#endif

#if CONFIG_SGM38121_ENABLE_AVDD2
    if (avdd2_actual_mv != CONFIG_SGM38121_AVDD2_MV) {
        ESP_LOGW(TAG,
                 "AVDD2 requested %d mV, rounded to %d mV (reg=0x%02X)",
                 CONFIG_SGM38121_AVDD2_MV,
                 avdd2_actual_mv,
                 avdd2_reg);
    } else {
        ESP_LOGI(TAG, "AVDD2 target %d mV (reg=0x%02X)", avdd2_actual_mv, avdd2_reg);
    }
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_AVDD2_VOUT, avdd2_reg), TAG, "write AVDD2 VOUT failed");
    enable_mask |= SGM38121_ENABLE_AVDD2_BIT;
#endif

    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_ENABLE, enable_mask), TAG, "write enable register failed");
    return ESP_OK;
}

void app_main(void)
{
    uint8_t chip_rev = 0;

    ESP_LOGI(TAG, "SGM38121 ESP-IDF test started");
    ESP_LOGI(TAG,
             "Default target: %s, I2C SDA=%d, SCL=%d, FREQ=%d Hz",
             CONFIG_IDF_TARGET,
             CONFIG_SGM38121_I2C_SDA,
             CONFIG_SGM38121_I2C_SCL,
             CONFIG_SGM38121_I2C_FREQ_HZ);

    ESP_ERROR_CHECK(sgm_init_i2c());
    ESP_ERROR_CHECK(sgm_read_reg(SGM38121_REG_CHIP_REV, &chip_rev));

    if (chip_rev != 0x80) {
        ESP_LOGW(TAG, "Unexpected CHIP_REV 0x%02X, datasheet reset value is 0x80", chip_rev);
    } else {
        ESP_LOGI(TAG, "CHIP_REV = 0x%02X", chip_rev);
    }

    sgm_log_state("boot");

#if CONFIG_SGM38121_DUMP_ALL_REGS_AT_BOOT
    sgm_dump_regs();
#endif

#if CONFIG_SGM38121_APPLY_CONFIG_ON_BOOT
    ESP_ERROR_CHECK(sgm_apply_config());
    vTaskDelay(pdMS_TO_TICKS(20));
    sgm_log_state("after_config");
#if CONFIG_SGM38121_DUMP_ALL_REGS_AT_BOOT
    sgm_dump_regs();
#endif
#else
    ESP_LOGW(TAG, "Boot apply is disabled, project is currently in read-only mode");
#endif

#if CONFIG_SGM38121_TOGGLE_TEST
    const uint8_t on_mask =
#if CONFIG_SGM38121_ENABLE_AVDD1
        SGM38121_ENABLE_AVDD1_BIT |
#endif
#if CONFIG_SGM38121_ENABLE_AVDD2
        SGM38121_ENABLE_AVDD2_BIT |
#endif
        0;
    bool outputs_on = true;
#endif

    while (1) {
#if CONFIG_SGM38121_TOGGLE_TEST
        outputs_on = !outputs_on;
        ESP_LOGI(TAG, "Toggle test -> outputs %s", outputs_on ? "ON" : "OFF");
        ESP_ERROR_CHECK(sgm_write_reg(SGM38121_REG_ENABLE, outputs_on ? on_mask : 0x00));
        vTaskDelay(pdMS_TO_TICKS(20));
        sgm_log_state(outputs_on ? "toggle_on" : "toggle_off");
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SGM38121_TOGGLE_PERIOD_MS));
#else
        sgm_log_state("monitor");
#if CONFIG_SGM38121_DUMP_ALL_REGS_PERIODICALLY
        sgm_dump_regs();
#endif
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SGM38121_MONITOR_PERIOD_MS));
#endif
    }
}
