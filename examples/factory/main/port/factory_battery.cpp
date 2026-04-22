#include "factory_battery.h"

#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "esp_log.h"

#include "bq25896.h"
#include "bq25896_hal_esp_idf.h"
#include "bq27220.h"

#include "board_config.h"
#include "factory_display.h"

namespace {

constexpr char TAG[] = "factory_battery";

static bq25896_hal_esp_idf_ctx_t s_bq25896_hal_ctx = {};
static bq25896_t s_bq25896 = {};
static BQ27220 s_bq27220;

static bool s_init_attempted = false;
static bool s_bq25896_ready = false;
static bool s_bq27220_ready = false;
static factory_battery_state_t s_state = {};
static char s_status_text[96] = "Battery init pending.";

static const char *bq25896_err_name(bq25896_err_t err)
{
    switch (err) {
        case BQ25896_OK:
            return "OK";
        case BQ25896_WARN_CLAMPED:
            return "WARN_CLAMPED";
        case BQ25896_ERR_NULL:
            return "ERR_NULL";
        case BQ25896_ERR_INVALID_ARG:
            return "ERR_INVALID_ARG";
        case BQ25896_ERR_I2C:
            return "ERR_I2C";
        case BQ25896_ERR_TIMEOUT:
            return "ERR_TIMEOUT";
        case BQ25896_ERR_DEVICE_ID:
            return "ERR_DEVICE_ID";
        case BQ25896_ERR_UNSUPPORTED:
            return "ERR_UNSUPPORTED";
        case BQ25896_ERR_NOT_INITIALIZED:
            return "ERR_NOT_INITIALIZED";
        default:
            return "ERR_UNKNOWN";
    }
}

static bool bq25896_apply_step(const char *step_name, bq25896_err_t err)
{
    if (BQ25896_FAILED(err)) {
        ESP_LOGE(TAG, "BQ25896 %s failed: %s (%d)", step_name, bq25896_err_name(err), (int)err);
        return false;
    }

    if (err == BQ25896_WARN_CLAMPED) {
        ESP_LOGW(TAG, "BQ25896 %s clamped to register step", step_name);
    }

    return true;
}

static void set_status_text(const char *text)
{
    std::snprintf(s_status_text, sizeof(s_status_text), "%s", (text != nullptr) ? text : "");
}

static bool init_bq25896_device(i2c_master_bus_handle_t bus_handle)
{
    bq25896_config_t config = {};
    bq25896_err_t rc = bq25896_get_default_config(&config);
    if (BQ25896_FAILED(rc)) {
        ESP_LOGE(TAG, "bq25896_get_default_config failed: %s", bq25896_err_name(rc));
        return false;
    }

    rc = bq25896_hal_esp_idf_get_default_ctx(&s_bq25896_hal_ctx);
    if (BQ25896_FAILED(rc)) {
        ESP_LOGE(TAG, "bq25896_hal_esp_idf_get_default_ctx failed: %s", bq25896_err_name(rc));
        return false;
    }

    s_bq25896_hal_ctx.scl_speed_hz = FACTORY_I2C_FREQ_HZ;
    s_bq25896_hal_ctx.timeout_ms = 100;

    rc = bq25896_hal_esp_idf_ctx_init(
        &s_bq25896_hal_ctx,
        bus_handle,
        (uint8_t)CONFIG_FACTORY_BATTERY_BQ25896_ADDR);
    if (BQ25896_FAILED(rc)) {
        ESP_LOGW(TAG,
                 "BQ25896 not found at 0x%02X: %s",
                 CONFIG_FACTORY_BATTERY_BQ25896_ADDR,
                 bq25896_err_name(rc));
        return false;
    }

    rc = bq25896_hal_esp_idf_make_hal(&s_bq25896_hal_ctx, &config.hal);
    if (BQ25896_FAILED(rc)) {
        ESP_LOGE(TAG, "bq25896_hal_esp_idf_make_hal failed: %s", bq25896_err_name(rc));
        (void)bq25896_hal_esp_idf_ctx_deinit(&s_bq25896_hal_ctx);
        return false;
    }

    config.i2c_addr_7bit = (uint8_t)CONFIG_FACTORY_BATTERY_BQ25896_ADDR;
    config.reset_registers_on_init = true;
    config.exit_hiz_on_init = true;
    config.adc_mode = BQ25896_ADC_MODE_CONTINUOUS;
    config.watchdog = BQ25896_WATCHDOG_DISABLED;

    rc = bq25896_init(&s_bq25896, &config);
    if (BQ25896_FAILED(rc)) {
        ESP_LOGW(TAG, "BQ25896 init failed: %s", bq25896_err_name(rc));
        (void)bq25896_hal_esp_idf_ctx_deinit(&s_bq25896_hal_ctx);
        return false;
    }

    if (!bq25896_apply_step("disable_otg", bq25896_disable_otg(&s_bq25896)) ||
        !bq25896_apply_step("enable_battery_power_path", bq25896_enable_battery_power_path(&s_bq25896)) ||
        !bq25896_apply_step("set_input_limit_ma", bq25896_set_input_limit_ma(&s_bq25896, 1000u)) ||
        !bq25896_apply_step("set_charge_current_ma", bq25896_set_charge_current_ma(&s_bq25896, CONFIG_FACTORY_BATTERY_CHARGE_CURRENT_MA)) ||
        !bq25896_apply_step("set_precharge_current_ma", bq25896_set_precharge_current_ma(&s_bq25896, CONFIG_FACTORY_BATTERY_PRECHARGE_CURRENT_MA)) ||
        !bq25896_apply_step("set_termination_current_ma", bq25896_set_termination_current_ma(&s_bq25896, CONFIG_FACTORY_BATTERY_TERM_CURRENT_MA)) ||
        !bq25896_apply_step("set_charge_voltage_mv", bq25896_set_charge_voltage_mv(&s_bq25896, CONFIG_FACTORY_BATTERY_CHARGE_VOLTAGE_MV)) ||
        !bq25896_apply_step("set_system_min_voltage_mv", bq25896_set_system_min_voltage_mv(&s_bq25896, CONFIG_FACTORY_BATTERY_SYSTEM_MIN_MV)) ||
        !bq25896_apply_step("enable_charge", bq25896_enable_charge(&s_bq25896))) {
        (void)bq25896_hal_esp_idf_ctx_deinit(&s_bq25896_hal_ctx);
        std::memset(&s_bq25896, 0, sizeof(s_bq25896));
        return false;
    }

    ESP_LOGI(TAG, "BQ25896 ready at 0x%02X", CONFIG_FACTORY_BATTERY_BQ25896_ADDR);
    return true;
}

static bool init_bq27220_device(i2c_master_bus_handle_t bus_handle)
{
    if (!s_bq27220.begin(
            bus_handle,
            (uint8_t)CONFIG_FACTORY_BATTERY_BQ27220_ADDR,
            FACTORY_I2C_FREQ_HZ)) {
        ESP_LOGW(TAG, "BQ27220 begin failed at 0x%02X", CONFIG_FACTORY_BATTERY_BQ27220_ADDR);
        return false;
    }

    if (!s_bq27220.setDefaultCapacity(CONFIG_FACTORY_BATTERY_CAPACITY_MAH)) {
        ESP_LOGE(TAG, "BQ27220 setDefaultCapacity failed");
        s_bq27220.end();
        return false;
    }

    if (!s_bq27220.setChargeParameters(CONFIG_FACTORY_BATTERY_CHARGE_CURRENT_MA,
                                       CONFIG_FACTORY_BATTERY_CHARGE_VOLTAGE_MV,
                                       CONFIG_FACTORY_BATTERY_TERM_CURRENT_MA,
                                       CONFIG_FACTORY_BATTERY_TERM_VOLTAGE_DELTA_MV)) {
        ESP_LOGE(TAG, "BQ27220 setChargeParameters failed");
        s_bq27220.end();
        return false;
    }

    if (!s_bq27220.init()) {
        ESP_LOGW(TAG, "BQ27220 init failed");
        s_bq27220.end();
        return false;
    }

    ESP_LOGI(TAG, "BQ27220 ready at 0x%02X", CONFIG_FACTORY_BATTERY_BQ27220_ADDR);
    return true;
}

static void ensure_initialized()
{
    if (s_init_attempted) {
        return;
    }

    i2c_master_bus_handle_t bus_handle = factory_display_get_i2c_bus();
    if (bus_handle == nullptr) {
        set_status_text("Shared I2C bus unavailable.");
        return;
    }

    s_init_attempted = true;
    s_bq25896_ready = init_bq25896_device(bus_handle);
    s_bq27220_ready = init_bq27220_device(bus_handle);

    if (!s_bq25896_ready && !s_bq27220_ready) {
        set_status_text("BQ25896 / BQ27220 not detected.");
    } else if (s_bq25896_ready && s_bq27220_ready) {
        set_status_text("Charger and gauge online.");
    } else if (s_bq25896_ready) {
        set_status_text("BQ25896 online, BQ27220 not detected.");
    } else {
        set_status_text("BQ27220 online, BQ25896 not detected.");
    }
}

static void update_status_text_from_state()
{
    if (!s_state.bus_ready) {
        set_status_text("Shared I2C bus unavailable.");
        return;
    }

    if (!s_state.charger_ready && !s_state.gauge_ready) {
        set_status_text("BQ25896 / BQ27220 not detected.");
        return;
    }

    const char *power_path = s_state.vbus_connected ? "USB in" : "Battery only";

    if (s_state.charger_ready && s_state.gauge_ready) {
        if (s_state.charger_read_ok && s_state.gauge_read_ok) {
            std::snprintf(s_status_text, sizeof(s_status_text), "%s | charger and gauge online.", power_path);
        } else {
            std::snprintf(s_status_text, sizeof(s_status_text), "%s | latest sample had read errors.", power_path);
        }
        return;
    }

    if (s_state.charger_ready) {
        std::snprintf(s_status_text,
                      sizeof(s_status_text),
                      "%s | charger %s.",
                      power_path,
                      s_state.charger_read_ok ? "online" : "read error");
        return;
    }

    std::snprintf(s_status_text,
                  sizeof(s_status_text),
                  "%s | gauge %s.",
                  power_path,
                  s_state.gauge_read_ok ? "online" : "read error");
}

}  // namespace

extern "C" void factory_battery_init(void)
{
    ensure_initialized();
    factory_battery_refresh();
}

extern "C" void factory_battery_refresh(void)
{
    ensure_initialized();

    std::memset(&s_state, 0, sizeof(s_state));
    s_state.bus_ready = factory_display_get_i2c_bus() != nullptr;
    s_state.charger_ready = s_bq25896_ready;
    s_state.gauge_ready = s_bq27220_ready;

    bq25896_status_t charger_status = {};
    bq25896_adc_t charger_adc = {};
    bq25896_charge_config_t charger_cfg = {};
    BQ27220Snapshot gauge = {};

    if (s_bq25896_ready) {
        const bq25896_err_t status_rc = bq25896_read_status(&s_bq25896, &charger_status);
        const bq25896_err_t adc_rc = bq25896_read_adc(&s_bq25896, &charger_adc);
        const bq25896_err_t cfg_rc = bq25896_read_charge_config(&s_bq25896, &charger_cfg);

        s_state.charger_read_ok =
            BQ25896_SUCCEEDED(status_rc) && BQ25896_SUCCEEDED(adc_rc) && BQ25896_SUCCEEDED(cfg_rc);

        if (s_state.charger_read_ok) {
            s_state.vbus_connected = charger_status.vbus_good || charger_status.power_good;
            s_state.charge_enabled = charger_cfg.charge_enabled;
            s_state.charge_done = charger_status.charge_status == BQ25896_CHARGE_STATUS_TERMINATION_DONE;
            s_state.charger_status = (uint8_t)charger_status.charge_status;
            s_state.input_limit_ma = charger_status.input_limit_ma;
            s_state.charge_current_ma = charger_cfg.charge_current_ma;
            s_state.precharge_current_ma = charger_cfg.precharge_current_ma;
            s_state.termination_current_ma = charger_cfg.termination_current_ma;
            s_state.charge_voltage_mv = charger_cfg.charge_voltage_mv;
            s_state.system_voltage_mv = charger_adc.system_voltage_mv;
            s_state.battery_voltage_mv = charger_adc.battery_voltage_mv;
            s_state.vbus_voltage_mv = charger_adc.vbus_voltage_mv;
        }
    }

    if (s_bq27220_ready) {
        s_state.gauge_read_ok = s_bq27220.readSnapshot(&gauge);
        if (s_state.gauge_read_ok) {
            const bool inferred_vbus = s_state.vbus_connected || gauge.charging;
            s_state.gauge_state =
                (uint8_t)BQ27220::classifyState(&gauge,
                                                inferred_vbus,
                                                (int16_t)CONFIG_FACTORY_BATTERY_CURRENT_THRESHOLD_MA);
            s_state.charging = gauge.charging;
            s_state.charge_done = s_state.charge_done || gauge.full;
            s_state.gauge_voltage_mv = gauge.voltage_mv;
            s_state.current_ma = gauge.current_ma;
            s_state.average_current_ma = gauge.average_current_ma;
            s_state.soc_percent = gauge.soc;
            s_state.soh_percent = gauge.soh_percent;
            s_state.full_capacity_mah = gauge.fcc_mah;
            s_state.remaining_capacity_mah = gauge.remaining_capacity_mah;
            s_state.temperature_dk = gauge.temperature_dk;
            s_state.battery_status_raw = gauge.battery_status.full;
            if (!s_state.charger_ready) {
                s_state.vbus_connected = inferred_vbus;
            }
        }
    }

    if (!s_state.gauge_ready && s_state.charger_read_ok) {
        s_state.charging =
            s_state.charge_enabled &&
            (s_state.charger_status == (uint8_t)BQ25896_CHARGE_STATUS_PRECHARGE ||
             s_state.charger_status == (uint8_t)BQ25896_CHARGE_STATUS_FAST_CHARGE);
    }

    update_status_text_from_state();
}

extern "C" const factory_battery_state_t *factory_battery_get_state(void)
{
    return &s_state;
}

extern "C" const char *factory_battery_get_status_text(void)
{
    return s_status_text;
}

extern "C" const char *factory_battery_charge_status_name(uint8_t status)
{
    switch ((bq25896_charge_status_t)status) {
        case BQ25896_CHARGE_STATUS_NOT_CHARGING:
            return "Idle";
        case BQ25896_CHARGE_STATUS_PRECHARGE:
            return "Precharge";
        case BQ25896_CHARGE_STATUS_FAST_CHARGE:
            return "Fast";
        case BQ25896_CHARGE_STATUS_TERMINATION_DONE:
            return "Done";
        default:
            return "Unknown";
    }
}

extern "C" const char *factory_battery_gauge_state_name(uint8_t state)
{
    switch ((BQ27220State)state) {
        case BQ27220StateSleep:
            return "Sleep";
        case BQ27220StateFull:
            return "Full";
        case BQ27220StateCharge:
            return "Charge";
        case BQ27220StateDischarge:
            return "Discharge";
        case BQ27220StateRelax:
        default:
            return "Relax";
    }
}

extern "C" void factory_battery_format_temperature(char *buffer, size_t buffer_size, uint16_t temperature_dk)
{
    if (buffer == nullptr || buffer_size == 0U) {
        return;
    }

    const int32_t temp_deci_c = (int32_t)temperature_dk - 2731;
    const int32_t abs_deci_c = (temp_deci_c < 0) ? -temp_deci_c : temp_deci_c;

    std::snprintf(buffer,
                  buffer_size,
                  "%s%" PRId32 ".%" PRId32 " C",
                  (temp_deci_c < 0) ? "-" : "",
                  abs_deci_c / 10,
                  abs_deci_c % 10);
}
