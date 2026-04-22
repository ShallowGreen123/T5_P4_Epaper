#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct factory_battery_state {
    bool bus_ready;
    bool charger_ready;
    bool gauge_ready;
    bool charger_read_ok;
    bool gauge_read_ok;
    bool vbus_connected;
    bool charge_enabled;
    bool charging;
    bool charge_done;
    bool gauge_battery_full_flag;
    bool gauge_gauging_full_flag;
    bool gauge_taper_flag;
    bool gauge_charge_inhibit;
    uint8_t charger_vbus_status;
    uint8_t charger_status;
    uint8_t gauge_state;
    uint16_t input_limit_ma;
    uint16_t charge_current_ma;
    uint16_t precharge_current_ma;
    uint16_t termination_current_ma;
    uint16_t charger_adc_current_ma;
    uint16_t charge_voltage_mv;
    uint16_t system_voltage_mv;
    uint16_t battery_voltage_mv;
    uint16_t vbus_voltage_mv;
    uint16_t gauge_voltage_mv;
    uint16_t gauge_charge_voltage_mv;
    uint16_t gauge_taper_current_ma;
    int16_t current_ma;
    int16_t average_current_ma;
    uint16_t soc_percent;
    uint16_t soh_percent;
    uint16_t full_capacity_mah;
    uint16_t remaining_capacity_mah;
    uint16_t temperature_dk;
    uint16_t battery_status_raw;
    uint16_t gauging_status_raw;
} factory_battery_state_t;

void factory_battery_init(void);
void factory_battery_refresh(void);
const factory_battery_state_t *factory_battery_get_state(void);
const char *factory_battery_get_status_text(void);
const char *factory_battery_charge_status_name(uint8_t status);
const char *factory_battery_gauge_state_name(uint8_t state);
void factory_battery_format_temperature(char *buffer, size_t buffer_size, uint16_t temperature_dk);

#ifdef __cplusplus
}  // extern "C"
#endif
