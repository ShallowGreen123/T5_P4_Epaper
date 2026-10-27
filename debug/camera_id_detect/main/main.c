/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/i2c_master.h"
#include "esp_cam_sensor.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sccb_i2c.h"
#include "esp_sccb_intf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_CAMERA_SC2336
#include "sc2336.h"
#endif

#if CONFIG_CAMERA_OV2710
#include "ov2710.h"
#endif

#if CONFIG_CAMERA_OV5645
#include "ov5645.h"
#endif

#if !CONFIG_CAMERA_SC2336 && !CONFIG_CAMERA_OV2710 && !CONFIG_CAMERA_OV5645
#error "Enable at least one supported camera sensor in sdkconfig.defaults or menuconfig"
#endif

#define SGM38121_I2C_ADDR                    0x28
#define SGM38121_REG_CHIP_REV                0x00
#define SGM38121_REG_DISCH                   0x02
#define SGM38121_REG_AVDD1_VOUT              0x05
#define SGM38121_REG_AVDD2_VOUT              0x06
#define SGM38121_REG_FUNCTION                0x07
#define SGM38121_REG_SEQ_DVDD                0x0A
#define SGM38121_REG_SEQ_AVDD                0x0B
#define SGM38121_REG_ENABLE                  0x0E
#define SGM38121_ENABLE_AVDD1_BIT            BIT2
#define SGM38121_ENABLE_AVDD2_BIT            BIT3

#define CAMERA_SCCB_ADDR_SC2336              0x30
#define CAMERA_SCCB_ADDR_OV2710              0x36
#define CAMERA_SCCB_ADDR_OV5645              0x3C

#define I2C_TIMEOUT_MS                       100
#define CAMERA_POWER_SETTLE_MS               20
#define CAMERA_SENSOR_PORT                   ESP_CAM_SENSOR_MIPI_CSI

typedef esp_cam_sensor_device_t *(*camera_detect_fn_t)(esp_cam_sensor_config_t *config);

typedef struct {
    const char *name;
    uint8_t sccb_addr;
    uint16_t expected_pid;
    camera_detect_fn_t detect;
} camera_candidate_t;

typedef struct {
    const char *name;
    uint8_t sccb_addr;
    uint16_t pid;
} camera_match_t;

static const char *TAG = "camera_id";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sgm;

static esp_err_t sgm_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_sgm, &reg, sizeof(reg), value, 1, I2C_TIMEOUT_MS);
}

static esp_err_t sgm_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(s_sgm, payload, sizeof(payload), I2C_TIMEOUT_MS);
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

    if (actual_mv != NULL) {
        *actual_mv = sgm_decode_avdd_mv((uint8_t)reg_value);
    }

    return (uint8_t)reg_value;
}

static const char *probe_status_to_str(esp_err_t err)
{
    if (err == ESP_OK) {
        return "ACK";
    }
    if (err == ESP_ERR_TIMEOUT) {
        return "TIMEOUT";
    }
    return "NO-ACK";
}

static void camera_probe_known_addresses(const char *label)
{
    static const uint8_t addresses[] = {
        SGM38121_I2C_ADDR,
        CAMERA_SCCB_ADDR_SC2336,
        CAMERA_SCCB_ADDR_OV2710,
        CAMERA_SCCB_ADDR_OV5645,
    };

    if (s_i2c_bus == NULL) {
        ESP_LOGW(TAG, "Skip I2C diagnostics (%s): bus is not initialized", label);
        return;
    }

    ESP_LOGI(TAG, "I2C diagnostics (%s)", label);
    for (size_t i = 0; i < ARRAY_SIZE(addresses); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, addresses[i], I2C_TIMEOUT_MS);
        ESP_LOGI(TAG, "  0x%02X -> %s (%s)", addresses[i], probe_status_to_str(err), esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2C timeout while probing 0x%02X, bus wiring or pull-ups may be wrong", addresses[i]);
            break;
        }
    }
}

static esp_err_t camera_init_i2c(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_CAMERA_ID_DETECT_I2C_SDA,
        .scl_io_num = CONFIG_CAMERA_ID_DETECT_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "create I2C master bus failed");

    ESP_LOGI(TAG,
             "I2C ready on SDA=%d SCL=%d freq=%d Hz",
             CONFIG_CAMERA_ID_DETECT_I2C_SDA,
             CONFIG_CAMERA_ID_DETECT_I2C_SCL,
             CONFIG_CAMERA_ID_DETECT_I2C_FREQ_HZ);
    return ESP_OK;
}

static esp_err_t camera_init_sgm_device(void)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SGM38121_I2C_ADDR,
        .scl_speed_hz = CONFIG_CAMERA_ID_DETECT_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_master_probe(s_i2c_bus, SGM38121_I2C_ADDR, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SGM38121 probe failed at 0x%02X: %s", SGM38121_I2C_ADDR, esp_err_to_name(err));
        return err;
    }

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_sgm), TAG, "add SGM38121 device failed");
    return ESP_OK;
}

static esp_err_t camera_enable_power_best_effort(void)
{
#if CONFIG_CAMERA_ID_DETECT_ENABLE_CAMERA_POWER
    int avdd1_actual_mv = 0;
    int avdd2_actual_mv = 0;
    const uint8_t avdd1_reg = sgm_encode_avdd_mv_rounded(CONFIG_CAMERA_ID_DETECT_AVDD1_MV, &avdd1_actual_mv);
    const uint8_t avdd2_reg = sgm_encode_avdd_mv_rounded(CONFIG_CAMERA_ID_DETECT_AVDD2_MV, &avdd2_actual_mv);
    uint8_t chip_rev = 0;

    esp_err_t err = camera_init_sgm_device();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Continue without managed camera power. Detection may fail if the camera rails are off.");
        camera_probe_known_addresses("after_sgm_probe_failure");
        return err;
    }

    if (sgm_read_reg(SGM38121_REG_CHIP_REV, &chip_rev) == ESP_OK) {
        ESP_LOGI(TAG, "SGM38121 CHIP_REV=0x%02X", chip_rev);
    }

    ESP_LOGI(TAG, "Enabling camera rails: AVDD1=%d mV AVDD2=%d mV", avdd1_actual_mv, avdd2_actual_mv);

    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_SEQ_DVDD, 0x00), TAG, "set DVDD sequence failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_SEQ_AVDD, 0x00), TAG, "set AVDD sequence failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_FUNCTION, 0x00), TAG, "disable wake-up failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_DISCH, 0x00), TAG, "set discharge mode failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_AVDD1_VOUT, avdd1_reg), TAG, "write AVDD1 failed");
    ESP_RETURN_ON_ERROR(sgm_write_reg(SGM38121_REG_AVDD2_VOUT, avdd2_reg), TAG, "write AVDD2 failed");
    ESP_RETURN_ON_ERROR(
        sgm_write_reg(SGM38121_REG_ENABLE, SGM38121_ENABLE_AVDD1_BIT | SGM38121_ENABLE_AVDD2_BIT),
        TAG,
        "enable camera rails failed");

    vTaskDelay(pdMS_TO_TICKS(CAMERA_POWER_SETTLE_MS));
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Camera power enable on boot is disabled; detection runs best-effort only");
    return ESP_OK;
#endif
}

static esp_err_t camera_create_sccb_io(uint8_t device_address, esp_sccb_io_handle_t *sccb_handle)
{
    const sccb_i2c_config_t sccb_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = CONFIG_CAMERA_ID_DETECT_I2C_FREQ_HZ,
    };

    return sccb_new_i2c_io(s_i2c_bus, &sccb_cfg, sccb_handle);
}

static esp_err_t camera_try_detect_one(const camera_candidate_t *candidate, camera_match_t *match)
{
    esp_sccb_io_handle_t sccb_handle = NULL;
    esp_cam_sensor_device_t *cam = NULL;
    esp_err_t err = camera_create_sccb_io(candidate->sccb_addr, &sccb_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Create SCCB handle for %s at 0x%02X failed: %s",
                 candidate->name, candidate->sccb_addr, esp_err_to_name(err));
        return err;
    }

    esp_cam_sensor_config_t cam_cfg = {
        .sccb_handle = sccb_handle,
        .reset_pin = (gpio_num_t)CONFIG_CAMERA_ID_DETECT_RESET_GPIO,
        .pwdn_pin = (gpio_num_t)CONFIG_CAMERA_ID_DETECT_PWDN_GPIO,
        .xclk_pin = (gpio_num_t)CONFIG_CAMERA_ID_DETECT_XCLK_GPIO,
        .xclk_freq_hz = CONFIG_CAMERA_ID_DETECT_XCLK_FREQ_HZ,
        .sensor_port = CAMERA_SENSOR_PORT,
    };

    ESP_LOGI(TAG, "Trying %s on SCCB address 0x%02X", candidate->name, candidate->sccb_addr);
    cam = candidate->detect(&cam_cfg);
    if (cam != NULL) {
        match->name = candidate->name;
        match->sccb_addr = candidate->sccb_addr;
        match->pid = cam->id.pid;
        ESP_LOGI(TAG, "Camera match: %s addr=0x%02X pid=0x%04X", match->name, match->sccb_addr, match->pid);
        err = esp_cam_sensor_del_dev(cam);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Delete camera device for %s failed: %s", candidate->name, esp_err_to_name(err));
        }
        err = ESP_OK;
    } else {
        ESP_LOGI(TAG, "%s did not match", candidate->name);
        err = ESP_ERR_NOT_FOUND;
    }

    esp_err_t cleanup_err = esp_sccb_del_i2c_io(sccb_handle);
    if (cleanup_err != ESP_OK) {
        ESP_LOGW(TAG, "Delete SCCB handle for %s failed: %s", candidate->name, esp_err_to_name(cleanup_err));
        if (err == ESP_OK) {
            err = cleanup_err;
        }
    }

    return err;
}

static void camera_cleanup(void)
{
    if (s_sgm != NULL) {
        esp_err_t err = i2c_master_bus_rm_device(s_sgm);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Remove SGM38121 device failed: %s", esp_err_to_name(err));
        }
        s_sgm = NULL;
    }

    if (s_i2c_bus != NULL) {
        esp_err_t err = i2c_del_master_bus(s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Delete I2C bus failed: %s", esp_err_to_name(err));
        }
        s_i2c_bus = NULL;
    }
}

void app_main(void)
{
    const camera_candidate_t candidates[] = {
#if CONFIG_CAMERA_SC2336
        {
            .name = SC2336_SENSOR_NAME,
            .sccb_addr = CAMERA_SCCB_ADDR_SC2336,
            .expected_pid = SC2336_PID,
            .detect = sc2336_detect,
        },
#endif
#if CONFIG_CAMERA_OV2710
        {
            .name = OV2710_SENSOR_NAME,
            .sccb_addr = CAMERA_SCCB_ADDR_OV2710,
            .expected_pid = OV2710_PID,
            .detect = ov2710_detect,
        },
#endif
#if CONFIG_CAMERA_OV5645
        {
            .name = OV5645_SENSOR_NAME,
            .sccb_addr = CAMERA_SCCB_ADDR_OV5645,
            .expected_pid = OV5645_PID,
            .detect = ov5645_detect,
        },
#endif
    };
    camera_match_t matches[ARRAY_SIZE(candidates)];
    size_t match_count = 0;

    ESP_LOGI(TAG, "Camera ID detect example started");
    ESP_LOGI(TAG,
             "Default target=%s reset=%d pwdn=%d xclk=%d xclk_freq=%d",
             CONFIG_IDF_TARGET,
             CONFIG_CAMERA_ID_DETECT_RESET_GPIO,
             CONFIG_CAMERA_ID_DETECT_PWDN_GPIO,
             CONFIG_CAMERA_ID_DETECT_XCLK_GPIO,
             CONFIG_CAMERA_ID_DETECT_XCLK_FREQ_HZ);

    ESP_ERROR_CHECK(camera_init_i2c());

    esp_err_t power_err = camera_enable_power_best_effort();
    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "Camera power setup was not fully successful: %s", esp_err_to_name(power_err));
    }

    for (size_t i = 0; i < ARRAY_SIZE(candidates); ++i) {
        camera_match_t match = {0};
        esp_err_t err = camera_try_detect_one(&candidates[i], &match);
        if (err == ESP_OK) {
            if (match.pid != candidates[i].expected_pid) {
                ESP_LOGW(TAG,
                         "PID mismatch for %s: expected 0x%04X but got 0x%04X",
                         candidates[i].name,
                         candidates[i].expected_pid,
                         match.pid);
            }
            matches[match_count++] = match;
        }
    }

    if (match_count == 0) {
        ESP_LOGW(TAG, "No supported camera detected.");
        ESP_LOGW(TAG, "Check camera power, RESET/PWDN/XCLK, and sensor wiring.");
        camera_probe_known_addresses("no_match");
    } else if (match_count == 1) {
        ESP_LOGI(TAG, "Detected camera model: %s", matches[0].name);
    } else {
        ESP_LOGW(TAG, "Multiple supported camera matches found (%u)", (unsigned)match_count);
        for (size_t i = 0; i < match_count; ++i) {
            ESP_LOGW(TAG, "  %s addr=0x%02X pid=0x%04X", matches[i].name, matches[i].sccb_addr, matches[i].pid);
        }
        camera_probe_known_addresses("multiple_matches");
    }

    camera_cleanup();
}
