#include "factory_icm20948.h"

#include <array>
#include <cstdio>
#include <memory>
#include <system_error>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "board_config.h"
#include "icm20948.hpp"

namespace {

using Imu = espp::Icm20948<espp::icm20948::Interface::I2C>;

constexpr char TAG[] = "factory_icm20948";
constexpr uint8_t kBankSelectRegister = 0x7F;
constexpr uint8_t kBank0Value = 0x00;
constexpr uint8_t kPwrMgmt1Register = 0x06;
constexpr uint8_t kDeviceResetBit = 0x80;
constexpr uint8_t kWhoAmIRegister = 0x00;
constexpr uint8_t kExpectedWhoAmI = 0xEA;
constexpr int kI2cTimeoutMs = 100;
constexpr std::array<uint8_t, 2> kCandidateAddresses = {0x69, 0x29};
constexpr TickType_t kBankSwitchDelayTicks = pdMS_TO_TICKS(2);
constexpr TickType_t kDeviceResetDelayTicks = pdMS_TO_TICKS(50);

static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static i2c_master_dev_handle_t s_icm_dev = nullptr;
static std::unique_ptr<Imu> s_imu;
static uint8_t s_icm_address = 0;
static uint8_t s_who_am_i = 0;
static uint16_t s_magnetometer_id = 0;
static uint32_t s_sample_count = 0;

static void set_status_text(char *status, size_t status_len, const char *text)
{
    if (status == nullptr || status_len == 0 || text == nullptr) {
        return;
    }
    std::snprintf(status, status_len, "%s", text);
}

static void set_status_text_fmt(char *status, size_t status_len, const char *format, ...)
{
    if (status == nullptr || status_len == 0 || format == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    std::vsnprintf(status, status_len, format, args);
    va_end(args);
}

static bool ensure_i2c_ready(char *status, size_t status_len)
{
    const esp_err_t init_err = bsp_i2c_init();
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "shared I2C init failed: %s", esp_err_to_name(init_err));
        set_status_text(status, status_len, "Shared I2C init failed.");
        return false;
    }

    s_i2c_bus = bsp_i2c_get_handle();
    if (s_i2c_bus == nullptr) {
        ESP_LOGE(TAG, "shared I2C handle is null");
        set_status_text(status, status_len, "Shared I2C bus unavailable.");
        return false;
    }
    return true;
}

static void clear_cached_state()
{
    s_imu.reset();

    if (s_icm_dev != nullptr) {
        uint8_t bank_reset[2] = {kBankSelectRegister, kBank0Value};
        const esp_err_t bank_err = i2c_master_transmit(s_icm_dev, bank_reset, sizeof(bank_reset), kI2cTimeoutMs);
        if (bank_err != ESP_OK) {
            ESP_LOGW(TAG, "restore bank0 before deinit failed: %s", esp_err_to_name(bank_err));
        }

        const esp_err_t err = i2c_master_bus_rm_device(s_icm_dev);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "remove ICM20948 device failed: %s", esp_err_to_name(err));
        }
        s_icm_dev = nullptr;
    }

    s_icm_address = 0;
    s_who_am_i = 0;
    s_magnetometer_id = 0;
    s_sample_count = 0;
}

static bool write_probe_register(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    if (dev == nullptr) {
        return false;
    }

    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(dev, payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
}

static bool read_probe_register(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    if (dev == nullptr || value == nullptr) {
        return false;
    }

    return i2c_master_transmit_receive(dev, &reg, sizeof(reg), value, 1, kI2cTimeoutMs) == ESP_OK;
}

static bool select_probe_bank0(i2c_master_dev_handle_t dev)
{
    if (!write_probe_register(dev, kBankSelectRegister, kBank0Value)) {
        return false;
    }

    vTaskDelay(kBankSwitchDelayTicks);
    return true;
}

static bool reset_probe_device(i2c_master_dev_handle_t dev)
{
    if (!select_probe_bank0(dev)) {
        return false;
    }
    if (!write_probe_register(dev, kPwrMgmt1Register, kDeviceResetBit)) {
        return false;
    }

    vTaskDelay(kDeviceResetDelayTicks);
    return select_probe_bank0(dev);
}

static bool read_candidate_who_am_i(uint8_t address, uint8_t *who_am_i)
{
    if (who_am_i == nullptr || s_i2c_bus == nullptr) {
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = FACTORY_I2C_FREQ_HZ;

    i2c_master_dev_handle_t dev = nullptr;
    const esp_err_t add_err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev);
    if (add_err != ESP_OK) {
        ESP_LOGW(TAG, "add probe device 0x%02X failed: %s", address, esp_err_to_name(add_err));
        return false;
    }

    esp_err_t read_err = ESP_OK;
    if (!select_probe_bank0(dev)) {
        read_err = ESP_FAIL;
        ESP_LOGW(TAG, "select bank0 on 0x%02X failed", address);
    } else if (!read_probe_register(dev, kWhoAmIRegister, who_am_i)) {
        read_err = ESP_FAIL;
        ESP_LOGW(TAG, "read WHO_AM_I from 0x%02X failed after bank reset", address);
    } else if (*who_am_i != kExpectedWhoAmI) {
        ESP_LOGW(TAG,
                 "WHO_AM_I on 0x%02X returned 0x%02X after bank reset, retrying after soft reset",
                 address,
                 *who_am_i);
        if (!reset_probe_device(dev) || !read_probe_register(dev, kWhoAmIRegister, who_am_i)) {
            read_err = ESP_FAIL;
            ESP_LOGW(TAG, "WHO_AM_I retry on 0x%02X failed after soft reset", address);
        }
    }

    const esp_err_t remove_err = i2c_master_bus_rm_device(dev);
    if (remove_err != ESP_OK) {
        ESP_LOGW(TAG, "remove probe device 0x%02X failed: %s", address, esp_err_to_name(remove_err));
    }

    if (read_err != ESP_OK) {
        return false;
    }

    return true;
}

static bool detect_icm20948_address(uint8_t *detected_address,
                                    uint8_t *detected_who_am_i,
                                    char *status,
                                    size_t status_len)
{
    if (detected_address == nullptr || detected_who_am_i == nullptr) {
        set_status_text(status, status_len, "Invalid ICM20948 output buffer.");
        return false;
    }
    if (!ensure_i2c_ready(status, status_len)) {
        return false;
    }

    bool found_mismatch = false;
    uint8_t mismatch_address = 0;
    uint8_t mismatch_who_am_i = 0;

    for (uint8_t address : kCandidateAddresses) {
        const esp_err_t probe_err = i2c_master_probe(s_i2c_bus, address, kI2cTimeoutMs);
        if (probe_err != ESP_OK) {
            ESP_LOGI(TAG, "probe 0x%02X failed: %s", address, esp_err_to_name(probe_err));
            continue;
        }

        uint8_t who_am_i = 0;
        if (!read_candidate_who_am_i(address, &who_am_i)) {
            continue;
        }

        ESP_LOGI(TAG, "candidate 0x%02X responded with WHO_AM_I=0x%02X", address, who_am_i);
        if (who_am_i == kExpectedWhoAmI) {
            *detected_address = address;
            *detected_who_am_i = who_am_i;
            return true;
        }

        found_mismatch = true;
        mismatch_address = address;
        mismatch_who_am_i = who_am_i;
    }

    if (found_mismatch) {
        set_status_text_fmt(status,
                            status_len,
                            "ICM20948 at 0x%02X returned WHO_AM_I=0x%02X.",
                            mismatch_address,
                            mismatch_who_am_i);
        return false;
    }

    set_status_text(status, status_len, "No ICM20948 detected on 0x69/0x29.");
    return false;
}

static bool create_icm_device(uint8_t address, char *status, size_t status_len)
{
    if (!ensure_i2c_ready(status, status_len)) {
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = FACTORY_I2C_FREQ_HZ;

    const esp_err_t add_err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_icm_dev);
    if (add_err != ESP_OK) {
        ESP_LOGE(TAG, "add ICM20948 device failed: %s", esp_err_to_name(add_err));
        set_status_text(status, status_len, "Failed to add ICM20948 on I2C.");
        return false;
    }

    s_icm_address = address;
    return true;
}

static bool icm_write(uint8_t address, const uint8_t *data, size_t length)
{
    if (s_icm_dev == nullptr || address != s_icm_address) {
        return false;
    }
    return i2c_master_transmit(s_icm_dev, data, length, kI2cTimeoutMs) == ESP_OK;
}

static bool icm_read(uint8_t address, uint8_t *data, size_t length)
{
    if (s_icm_dev == nullptr || address != s_icm_address) {
        return false;
    }
    return i2c_master_receive(s_icm_dev, data, length, kI2cTimeoutMs) == ESP_OK;
}

static bool icm_write_then_read(uint8_t address,
                                const uint8_t *write_data,
                                size_t write_length,
                                uint8_t *read_data,
                                size_t read_length)
{
    if (s_icm_dev == nullptr || address != s_icm_address) {
        return false;
    }
    return i2c_master_transmit_receive(
               s_icm_dev,
               write_data,
               write_length,
               read_data,
               read_length,
               kI2cTimeoutMs) == ESP_OK;
}

static bool select_active_bank0(char *status, size_t status_len)
{
    if (s_icm_dev == nullptr) {
        set_status_text(status, status_len, "ICM20948 device handle unavailable.");
        return false;
    }

    uint8_t bank_reset[2] = {kBankSelectRegister, kBank0Value};
    const esp_err_t err = i2c_master_transmit(s_icm_dev, bank_reset, sizeof(bank_reset), kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "select active bank0 failed: %s", esp_err_to_name(err));
        set_status_text_fmt(status, status_len, "ICM20948 bank reset failed at 0x%02X.", s_icm_address);
        return false;
    }

    vTaskDelay(kBankSwitchDelayTicks);
    return true;
}

static bool init_cached_imu(char *status, size_t status_len)
{
    uint8_t detected_address = 0;
    uint8_t detected_who_am_i = 0;
    if (!detect_icm20948_address(&detected_address, &detected_who_am_i, status, status_len)) {
        return false;
    }

    clear_cached_state();
    if (!create_icm_device(detected_address, status, status_len)) {
        clear_cached_state();
        return false;
    }
    if (!select_active_bank0(status, status_len)) {
        clear_cached_state();
        return false;
    }

    Imu::Config imu_config = {
        .device_address = detected_address,
        .write = icm_write,
        .read = icm_read,
        .imu_config =
            {
                .accelerometer_range = Imu::AccelerometerRange::RANGE_2G,
                .gyroscope_range = Imu::GyroscopeRange::RANGE_250DPS,
                .accelerometer_sample_rate_divider = 9,
                .gyroscope_sample_rate_divider = 9,
                .magnetometer_mode = Imu::MagnetometerMode::CONTINUOUS_MODE_100_HZ,
            },
        .auto_init = false,
        .log_level = espp::Logger::Verbosity::WARN,
    };

    s_imu.reset(new Imu(imu_config));
    s_imu->set_write_then_read(icm_write_then_read);

    std::error_code ec;
    if (!s_imu->init(ec)) {
        ESP_LOGE(TAG, "ICM20948 init failed at 0x%02X: %s", detected_address, ec.message().c_str());
        set_status_text_fmt(status, status_len, "ICM20948 init failed at 0x%02X.", detected_address);
        clear_cached_state();
        return false;
    }

    // The ES++ driver may leave the chip in bank 3 after init; get_device_id()
    // does not force a bank switch, so the pre-init probe result is the reliable
    // WHO_AM_I value to surface here.
    s_who_am_i = detected_who_am_i;

    ec.clear();
    s_magnetometer_id = s_imu->get_magnetometer_device_id(ec);
    if (ec) {
        ESP_LOGE(TAG, "AK09916 ID read failed: %s", ec.message().c_str());
        set_status_text(status, status_len, "AK09916 ID read failed.");
        clear_cached_state();
        return false;
    }

    set_status_text_fmt(status, status_len, "ICM20948 ready at 0x%02X.", detected_address);
    ESP_LOGI(TAG,
             "ICM20948 ready at 0x%02X, WHO_AM_I=0x%02X, AK09916 ID=0x%04X",
             detected_address,
             s_who_am_i,
             s_magnetometer_id);
    return true;
}

static bool read_sensor_value(factory_icm20948_sample_t *out, char *status, size_t status_len)
{
    if (out == nullptr) {
        set_status_text(status, status_len, "Invalid ICM20948 sample buffer.");
        return false;
    }

    if (s_imu == nullptr && !init_cached_imu(status, status_len)) {
        return false;
    }

    std::error_code ec;

    ec.clear();
    const Imu::Value accel = s_imu->read_accelerometer(ec);
    if (ec) {
        ESP_LOGE(TAG, "read accelerometer failed: %s", ec.message().c_str());
        set_status_text(status, status_len, "Accelerometer read failed.");
        clear_cached_state();
        return false;
    }

    ec.clear();
    const Imu::Value gyro = s_imu->read_gyroscope(ec);
    if (ec) {
        ESP_LOGE(TAG, "read gyroscope failed: %s", ec.message().c_str());
        set_status_text(status, status_len, "Gyroscope read failed.");
        clear_cached_state();
        return false;
    }

    ec.clear();
    const Imu::Value mag = s_imu->read_magnetometer(ec);
    if (ec) {
        ESP_LOGE(TAG, "read magnetometer failed: %s", ec.message().c_str());
        set_status_text(status, status_len, "Magnetometer read failed.");
        clear_cached_state();
        return false;
    }

    ec.clear();
    const float temperature_c = s_imu->read_temperature(ec);
    if (ec) {
        ESP_LOGE(TAG, "read temperature failed: %s", ec.message().c_str());
        set_status_text(status, status_len, "Temperature read failed.");
        clear_cached_state();
        return false;
    }

    ++s_sample_count;
    out->device_address = s_icm_address;
    out->who_am_i = s_who_am_i;
    out->magnetometer_id = s_magnetometer_id;
    out->sample_count = s_sample_count;
    out->sample_time_us = (uint64_t)esp_timer_get_time();
    out->accel.x = accel.x;
    out->accel.y = accel.y;
    out->accel.z = accel.z;
    out->gyro.x = gyro.x;
    out->gyro.y = gyro.y;
    out->gyro.z = gyro.z;
    out->mag.x = mag.x;
    out->mag.y = mag.y;
    out->mag.z = mag.z;
    out->temperature_c = temperature_c;

    set_status_text_fmt(status, status_len, "ICM20948 sample #%u captured.", out->sample_count);
    return true;
}

}  // namespace

extern "C" bool factory_icm20948_read(factory_icm20948_sample_t *out, char *status, size_t status_len)
{
    return read_sensor_value(out, status, status_len);
}

extern "C" void factory_icm20948_deinit(void)
{
    clear_cached_state();
}
