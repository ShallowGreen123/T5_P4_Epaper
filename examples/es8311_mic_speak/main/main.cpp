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

#include <Arduino.h>
#include "config.h"

#include "ExtensionIOXL9555.hpp"
#include "board_es8311.h"

ExtensionIOXL9555 io;



void setup(void)
{
    Serial.begin(115200);

    if(BOARD_PCA_INT > 0) {
        pinMode(BOARD_PCA_INT, INPUT_PULLUP);
    }

    if (!io.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, XL9555_SLAVE_ADDRESS0)) {
        while (1) {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }
    uint8_t port0_mask = 0xff;

    // Set port 0_5 to output
    port0_mask = port0_mask & (~BOARD_PCA_05_SHUTDOWN);

    Serial.printf("port0_mask = 0x%x", port0_mask);

    // Set PORT0 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT0, port0_mask);
    io.digitalWrite(ExtensionIOXL9555::IO5, HIGH);


    es8311_start();
}

void loop(void)
{
    delay(1000);
}
