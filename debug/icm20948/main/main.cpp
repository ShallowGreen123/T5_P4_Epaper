#include <array>
#include <cinttypes>
#include <cstdint>
#include <system_error>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "icm20948.hpp"

namespace {

using Imu = espp::Icm20948<espp::icm20948::Interface::I2C>;

constexpr char TAG[] = "icm20948";
constexpr uint8_t kWhoAmIRegister = 0x00;
constexpr uint8_t kExpectedWhoAmI = 0xEA;
constexpr int kI2cTimeoutMs = 100;
constexpr std::array<uint8_t, 2> kCandidateAddresses = {0x29, 0x69};

i2c_master_bus_handle_t s_i2c_bus = nullptr;
i2c_master_dev_handle_t s_icm_dev = nullptr;
uint8_t s_icm_address = 0;

esp_err_t ensure_i2c_ready() {
  ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "shared I2C init failed");
  s_i2c_bus = bsp_i2c_get_handle();
  ESP_RETURN_ON_FALSE(s_i2c_bus != nullptr, ESP_ERR_INVALID_STATE, TAG,
                      "shared I2C handle is null");
  return ESP_OK;
}

void scan_i2c_bus() {
  ESP_LOGI(TAG, "Scanning I2C bus on SDA=%d SCL=%d", T5_BOARD_I2C_SDA, T5_BOARD_I2C_SCL);
  for (uint8_t address = 1; address < 0x7F; ++address) {
    esp_err_t err = i2c_master_probe(s_i2c_bus, address, kI2cTimeoutMs);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Found I2C device at 0x%02X", address);
    } else if (err == ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "Probe timeout at 0x%02X, check pull-ups or sensor wiring", address);
      break;
    }
  }
}

esp_err_t read_candidate_who_am_i(uint8_t address, uint8_t *who_am_i) {
  ESP_RETURN_ON_FALSE(who_am_i != nullptr, ESP_ERR_INVALID_ARG, TAG, "who_am_i output is null");

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = address;
  dev_cfg.scl_speed_hz = CONFIG_ICM20948_I2C_FREQ_HZ;

  i2c_master_dev_handle_t dev = nullptr;
  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev), TAG,
                      "add temporary device 0x%02X failed", address);

  uint8_t reg = kWhoAmIRegister;
  esp_err_t ret =
      i2c_master_transmit_receive(dev, &reg, sizeof(reg), who_am_i, 1, kI2cTimeoutMs);

  esp_err_t remove_ret = i2c_master_bus_rm_device(dev);
  if (ret != ESP_OK) {
    return ret;
  }
  return remove_ret;
}

esp_err_t detect_icm20948_address(uint8_t *detected_address) {
  ESP_RETURN_ON_FALSE(detected_address != nullptr, ESP_ERR_INVALID_ARG, TAG,
                      "detected_address output is null");
  ESP_RETURN_ON_ERROR(ensure_i2c_ready(), TAG, "I2C not ready");

  for (uint8_t address : kCandidateAddresses) {
    esp_err_t probe_err = i2c_master_probe(s_i2c_bus, address, kI2cTimeoutMs);
    if (probe_err != ESP_OK) {
      ESP_LOGI(TAG, "Probe 0x%02X failed: %s", address, esp_err_to_name(probe_err));
      continue;
    }

    uint8_t who_am_i = 0;
    esp_err_t id_err = read_candidate_who_am_i(address, &who_am_i);
    if (id_err != ESP_OK) {
      ESP_LOGW(TAG, "Read WHO_AM_I from 0x%02X failed: %s", address, esp_err_to_name(id_err));
      continue;
    }

    ESP_LOGI(TAG, "Candidate 0x%02X responded with WHO_AM_I=0x%02X", address, who_am_i);
    if (who_am_i == kExpectedWhoAmI) {
      *detected_address = address;
      return ESP_OK;
    }
  }

  scan_i2c_bus();
  return ESP_ERR_NOT_FOUND;
}

esp_err_t create_icm_device(uint8_t address) {
  ESP_RETURN_ON_ERROR(ensure_i2c_ready(), TAG, "I2C not ready");

  if (s_icm_dev != nullptr) {
    ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_icm_dev), TAG,
                        "remove previous ICM20948 device failed");
    s_icm_dev = nullptr;
    s_icm_address = 0;
  }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = address;
  dev_cfg.scl_speed_hz = CONFIG_ICM20948_I2C_FREQ_HZ;

  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_icm_dev), TAG,
                      "add ICM20948 device failed");
  s_icm_address = address;
  return ESP_OK;
}

bool icm_write(uint8_t address, const uint8_t *data, size_t length) {
  if (s_icm_dev == nullptr || address != s_icm_address) {
    return false;
  }
  return i2c_master_transmit(s_icm_dev, data, length, kI2cTimeoutMs) == ESP_OK;
}

bool icm_read(uint8_t address, uint8_t *data, size_t length) {
  if (s_icm_dev == nullptr || address != s_icm_address) {
    return false;
  }
  return i2c_master_receive(s_icm_dev, data, length, kI2cTimeoutMs) == ESP_OK;
}

bool icm_write_then_read(uint8_t address, const uint8_t *write_data, size_t write_length,
                         uint8_t *read_data, size_t read_length) {
  if (s_icm_dev == nullptr || address != s_icm_address) {
    return false;
  }
  return i2c_master_transmit_receive(s_icm_dev, write_data, write_length, read_data, read_length,
                                     kI2cTimeoutMs) == ESP_OK;
}

void log_measurement(int64_t sample_index, float dt_ms, const Imu::Value &accel,
                     const Imu::Value &gyro, const Imu::Value &mag, float temperature_c) {
  ESP_LOGI(TAG,
           "#%" PRId64
           " dt=%.1fms accel[g] x=%.3f y=%.3f z=%.3f gyro[dps] x=%.3f y=%.3f z=%.3f "
           "mag[uT] x=%.3f y=%.3f z=%.3f temp[C]=%.2f",
           sample_index, dt_ms, accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z, mag.x, mag.y,
           mag.z, temperature_c);
}

} // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting ICM20948 example on T5-P4 E-Paper");
  ESP_LOGI(TAG, "Board I2C: SDA=%d SCL=%d, frequency=%d Hz", T5_BOARD_I2C_SDA, T5_BOARD_I2C_SCL,
           CONFIG_ICM20948_I2C_FREQ_HZ);
  ESP_LOGI(TAG, "Address probe order: 0x29 -> 0x69");

  uint8_t detected_address = 0;
  ESP_ERROR_CHECK(detect_icm20948_address(&detected_address));
  ESP_ERROR_CHECK(create_icm_device(detected_address));

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

  Imu imu(imu_config);
  imu.set_write_then_read(icm_write_then_read);

  std::error_code ec;
  if (!imu.init(ec)) {
    ESP_LOGE(TAG, "ICM20948 init failed at 0x%02X: %s", detected_address, ec.message().c_str());
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  ec.clear();
  uint8_t who_am_i = imu.get_device_id(ec);
  if (ec) {
    ESP_LOGE(TAG, "Read ICM20948 WHO_AM_I failed: %s", ec.message().c_str());
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  ec.clear();
  uint16_t magnetometer_id = imu.get_magnetometer_device_id(ec);
  if (ec) {
    ESP_LOGE(TAG, "Read AK09916 ID failed: %s", ec.message().c_str());
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  ESP_LOGI(TAG, "ICM20948 ready at 0x%02X, WHO_AM_I=0x%02X, AK09916 ID=0x%04X",
           detected_address, who_am_i, magnetometer_id);

  int64_t sample_index = 0;
  int64_t previous_sample_us = esp_timer_get_time();

  while (true) {
    int64_t now_us = esp_timer_get_time();
    float dt_ms = (now_us - previous_sample_us) / 1000.0f;
    previous_sample_us = now_us;

    ec.clear();
    Imu::Value accel = imu.read_accelerometer(ec);
    if (ec) {
      ESP_LOGE(TAG, "Read accelerometer failed: %s", ec.message().c_str());
      vTaskDelay(pdMS_TO_TICKS(CONFIG_ICM20948_SAMPLE_PERIOD_MS));
      continue;
    }

    ec.clear();
    Imu::Value gyro = imu.read_gyroscope(ec);
    if (ec) {
      ESP_LOGE(TAG, "Read gyroscope failed: %s", ec.message().c_str());
      vTaskDelay(pdMS_TO_TICKS(CONFIG_ICM20948_SAMPLE_PERIOD_MS));
      continue;
    }

    ec.clear();
    Imu::Value mag = imu.read_magnetometer(ec);
    if (ec) {
      ESP_LOGE(TAG, "Read magnetometer failed: %s", ec.message().c_str());
      vTaskDelay(pdMS_TO_TICKS(CONFIG_ICM20948_SAMPLE_PERIOD_MS));
      continue;
    }

    ec.clear();
    float temperature_c = imu.read_temperature(ec);
    if (ec) {
      ESP_LOGE(TAG, "Read temperature failed: %s", ec.message().c_str());
      vTaskDelay(pdMS_TO_TICKS(CONFIG_ICM20948_SAMPLE_PERIOD_MS));
      continue;
    }

    log_measurement(++sample_index, dt_ms, accel, gyro, mag, temperature_c);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_ICM20948_SAMPLE_PERIOD_MS));
  }
}
