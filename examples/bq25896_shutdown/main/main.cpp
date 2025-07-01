/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "Arduino.h"

#define XPOWERS_CHIP_BQ25896

#include <XPowersLib.h>

static const char *TAG = "main";
XPowersPPM PPM;

const uint8_t i2c_sda = BOARD_I2C_SDA;
const uint8_t i2c_scl = BOARD_I2C_SCL;
const uint8_t pmu_irq_pin = -1;
uint32_t cycleInterval=0;
uint32_t countdown = 10;

void setup(void)
{
    Serial.begin(115200);
    // while (!Serial);

    bool result =  PPM.init(Wire, i2c_sda, i2c_scl, BQ25896_SLAVE_ADDRESS);
    if (result == false) {
        while (1) {
            Serial.println("PPM is not online...");
            delay(1000);
        }
    }
}

void loop()
{
    if (millis() > cycleInterval) {
        Serial.printf("%ld\n", countdown);
        if (!(countdown--)) {
            Serial.println("Shutdown .....");
            // The shutdown function can only be used when the battery is connected alone,
            // and cannot be shut down when connected to USB.
            // It can only be powered on in the following two ways:
            // 1. Press the PPM/QON button
            // 2. Connect to USB
            PPM.shutdown();
            countdown = 10000;
        }
        cycleInterval = millis() + 1000;
    }
    delay(1);
}
