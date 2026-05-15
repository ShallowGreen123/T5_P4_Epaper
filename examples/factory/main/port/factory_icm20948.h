#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct factory_icm20948_vector3 {
    float x;
    float y;
    float z;
} factory_icm20948_vector3_t;

typedef struct factory_icm20948_sample {
    uint8_t device_address;
    uint8_t who_am_i;
    uint16_t magnetometer_id;
    uint32_t sample_count;
    uint64_t sample_time_us;
    factory_icm20948_vector3_t accel;
    factory_icm20948_vector3_t gyro;
    factory_icm20948_vector3_t mag;
    float temperature_c;
} factory_icm20948_sample_t;

bool factory_icm20948_read(factory_icm20948_sample_t *out, char *status, size_t status_len);
void factory_icm20948_deinit(void);

#ifdef __cplusplus
}  // extern "C"
#endif
