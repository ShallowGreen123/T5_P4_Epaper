/**
 * @file bq25896_polling_sm_example.c
 * @brief Polling state-machine example for BQ25896 event handling.
 *
 * This file is reference code and is not compiled into the component by
 * default. Copy the parts you need into your own application.
 */

#include "bq25896.h"

#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BQ25896_SM_TAG                    "bq25896_sm"
#define BQ25896_SM_POLL_PERIOD_MS         1000u
#define BQ25896_SM_WATCHDOG_KICK_MS       30000u

typedef enum
{
    BQ25896_SM_STATE_NO_VBUS = 0u,
    BQ25896_SM_STATE_READY,
    BQ25896_SM_STATE_CHARGING,
    BQ25896_SM_STATE_CHARGE_DONE,
    BQ25896_SM_STATE_TEMP_FAULT
} bq25896_sm_state_t;

typedef struct
{
    bq25896_t *charger;
    bq25896_sm_state_t state;
    bool last_vbus_present;
    bool charge_done_latched;
    uint32_t last_watchdog_kick_ms;
} bq25896_polling_sm_t;

static void app_on_vbus_changed(bool inserted)
{
    ESP_LOGI(BQ25896_SM_TAG, "VBUS %s", inserted ? "inserted" : "removed");
}

static void app_on_input_dpm(bool vindpm_active, bool iindpm_active, uint16_t limit_ma)
{
    ESP_LOGW(BQ25896_SM_TAG,
             "DPM active: VINDPM=%d IINDPM=%d effective_limit=%u mA",
             (int)vindpm_active,
             (int)iindpm_active,
             (unsigned int)limit_ma);
}

static void app_on_temperature_fault(const bq25896_fault_t *fault)
{
    ESP_LOGE(BQ25896_SM_TAG,
             "Temperature abnormal: charge_fault=%u ntc_fault=%u",
             (unsigned int)fault->charge_fault,
             (unsigned int)fault->ntc_fault);
}

static void app_on_charge_done(uint16_t battery_voltage_mv)
{
    ESP_LOGI(BQ25896_SM_TAG, "Charge complete, battery=%u mV", (unsigned int)battery_voltage_mv);
}

static bq25896_sm_state_t bq25896_sm_next_state(const bq25896_status_t *status,
                                                const bq25896_fault_t *fault)
{
    bool temperature_fault = false;

    if ((!status->vbus_good) && (!status->power_good))
    {
        return BQ25896_SM_STATE_NO_VBUS;
    }

    temperature_fault = (fault->charge_fault == BQ25896_CHARGE_FAULT_THERMAL_SHUTDOWN) ||
                        (fault->ntc_fault == BQ25896_NTC_FAULT_HOT) ||
                        (fault->ntc_fault == BQ25896_NTC_FAULT_COLD);

    if (temperature_fault)
    {
        return BQ25896_SM_STATE_TEMP_FAULT;
    }

    if (status->charge_status == BQ25896_CHARGE_STATUS_TERMINATION_DONE)
    {
        return BQ25896_SM_STATE_CHARGE_DONE;
    }

    if ((status->charge_status == BQ25896_CHARGE_STATUS_PRECHARGE) ||
        (status->charge_status == BQ25896_CHARGE_STATUS_FAST_CHARGE))
    {
        return BQ25896_SM_STATE_CHARGING;
    }

    return BQ25896_SM_STATE_READY;
}

static bq25896_err_t bq25896_polling_sm_init(bq25896_polling_sm_t *sm, bq25896_t *charger)
{
    if ((sm == NULL) || (charger == NULL))
    {
        return BQ25896_ERR_NULL;
    }

    sm->charger = charger;
    sm->state = BQ25896_SM_STATE_NO_VBUS;
    sm->last_vbus_present = false;
    sm->charge_done_latched = false;
    sm->last_watchdog_kick_ms = 0u;

    return BQ25896_OK;
}

static bq25896_err_t bq25896_polling_sm_step(bq25896_polling_sm_t *sm, uint32_t now_ms)
{
    bq25896_status_t status;
    bq25896_fault_t fault;
    bq25896_adc_t adc;
    bq25896_sm_state_t next_state;
    bq25896_err_t rc;
    bool vbus_present;

    if (sm == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    rc = bq25896_read_status(sm->charger, &status);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_fault(sm->charger, &fault);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_adc(sm->charger, &adc);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    vbus_present = status.vbus_good || status.power_good;
    if (vbus_present != sm->last_vbus_present)
    {
        app_on_vbus_changed(vbus_present);

        if (vbus_present)
        {
            rc = bq25896_enable_charge(sm->charger);
            if (BQ25896_FAILED(rc))
            {
                return rc;
            }
        }

        sm->last_vbus_present = vbus_present;
    }

    if (adc.vindpm_active || adc.iindpm_active)
    {
        app_on_input_dpm(adc.vindpm_active, adc.iindpm_active, adc.input_limit_ma);
    }

    next_state = bq25896_sm_next_state(&status, &fault);
    if (next_state != sm->state)
    {
        switch (next_state)
        {
            case BQ25896_SM_STATE_TEMP_FAULT:
                app_on_temperature_fault(&fault);
                rc = bq25896_disable_charge(sm->charger);
                if (BQ25896_FAILED(rc))
                {
                    return rc;
                }
                break;

            case BQ25896_SM_STATE_CHARGE_DONE:
                if (!sm->charge_done_latched)
                {
                    app_on_charge_done(adc.battery_voltage_mv);
                    sm->charge_done_latched = true;
                }
                break;

            case BQ25896_SM_STATE_READY:
            case BQ25896_SM_STATE_CHARGING:
                if (sm->state == BQ25896_SM_STATE_TEMP_FAULT)
                {
                    rc = bq25896_enable_charge(sm->charger);
                    if (BQ25896_FAILED(rc))
                    {
                        return rc;
                    }
                }

                sm->charge_done_latched = false;
                break;

            case BQ25896_SM_STATE_NO_VBUS:
            default:
                sm->charge_done_latched = false;
                break;
        }

        sm->state = next_state;
    }

    if (vbus_present && ((now_ms - sm->last_watchdog_kick_ms) >= BQ25896_SM_WATCHDOG_KICK_MS))
    {
        rc = bq25896_kick_watchdog(sm->charger);
        if (BQ25896_FAILED(rc))
        {
            return rc;
        }

        sm->last_watchdog_kick_ms = now_ms;
    }

    return BQ25896_OK;
}

static bq25896_err_t app_bq25896_i2c_read(void *user_ctx,
                                          uint8_t i2c_addr_7bit,
                                          uint8_t reg,
                                          uint8_t *data,
                                          size_t len)
{
    (void)user_ctx;
    (void)i2c_addr_7bit;
    (void)reg;
    (void)data;
    (void)len;
    return BQ25896_ERR_UNSUPPORTED;
}

static bq25896_err_t app_bq25896_i2c_write(void *user_ctx,
                                           uint8_t i2c_addr_7bit,
                                           uint8_t reg,
                                           const uint8_t *data,
                                           size_t len)
{
    (void)user_ctx;
    (void)i2c_addr_7bit;
    (void)reg;
    (void)data;
    (void)len;
    return BQ25896_ERR_UNSUPPORTED;
}

static void app_bq25896_delay_ms(void *user_ctx, uint32_t delay_ms)
{
    (void)user_ctx;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

void app_main(void)
{
    bq25896_t charger;
    bq25896_config_t config;
    bq25896_polling_sm_t sm;
    bq25896_err_t rc;

    rc = bq25896_get_default_config(&config);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(BQ25896_SM_TAG, "default config failed: %d", (int)rc);
        return;
    }

    config.hal.i2c_read = app_bq25896_i2c_read;
    config.hal.i2c_write = app_bq25896_i2c_write;
    config.hal.delay_ms = app_bq25896_delay_ms;
    config.hal.user_ctx = NULL;
    config.i2c_addr_7bit = BQ25896_I2C_ADDR_7BIT_DEFAULT;
    config.exit_hiz_on_init = true;
    config.enable_ilim_pin = false;
    config.enable_ico = true;
    config.adc_mode = BQ25896_ADC_MODE_CONTINUOUS;
    config.watchdog = BQ25896_WATCHDOG_40S;

    rc = bq25896_init(&charger, &config);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(BQ25896_SM_TAG, "init failed: %d", (int)rc);
        return;
    }

    rc = bq25896_enable_charge(&charger);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(BQ25896_SM_TAG, "enable_charge failed: %d", (int)rc);
        return;
    }

    rc = bq25896_polling_sm_init(&sm, &charger);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(BQ25896_SM_TAG, "state machine init failed: %d", (int)rc);
        return;
    }

    while (true)
    {
        rc = bq25896_polling_sm_step(&sm, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        if (BQ25896_FAILED(rc))
        {
            ESP_LOGE(BQ25896_SM_TAG, "poll step failed: %d", (int)rc);
        }

        vTaskDelay(pdMS_TO_TICKS(BQ25896_SM_POLL_PERIOD_MS));
    }
}
