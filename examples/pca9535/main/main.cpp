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
#include "ExtensionIOXL9555.hpp"


#define SENSOR_IRQ  -1

ExtensionIOXL9555 io;

void setup(void)
{
    Serial.begin(115200);

    // Set the interrupt input to input pull-up
    if (SENSOR_IRQ > 0) {
        pinMode(SENSOR_IRQ, INPUT_PULLUP);
    }
    
    /*
    *
    *    If the device address is not known, the 0xFF parameter can be passed in.
    *
    *    XL9555_UNKOWN_ADDRESS  = 0xFF
    *
    *    If the device address is known, the device address is given
    *
    *    XL9555_SLAVE_ADDRESS0  = 0x20
    *    XL9555_SLAVE_ADDRESS1  = 0x21
    *    XL9555_SLAVE_ADDRESS2  = 0x22
    *    XL9555_SLAVE_ADDRESS3  = 0x23
    *    XL9555_SLAVE_ADDRESS4  = 0x24
    *    XL9555_SLAVE_ADDRESS5  = 0x25
    *    XL9555_SLAVE_ADDRESS6  = 0x26
    *    XL9555_SLAVE_ADDRESS7  = 0x27
    */
    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;

    if (!io.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, chip_address)) {
        while (1) {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }

    // Set PORT0 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT0, 0xFF);
    // Set PORT1 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT1, 0xFF);
}

void loop(void)
{
    Serial.print("PORT0:0b");
    Serial.print(io.readPort(ExtensionIOXL9555::PORT0), BIN);
    Serial.print("\tPORT1:0b");
    Serial.println(io.readPort(ExtensionIOXL9555::PORT1), BIN);

    //Allowable range of parameter input: IO0 ~ IO15
    Serial.println("IO0:\tIO1:\tIO2:\tIO3:\tIO4:\tIO5:\tIO6:\tIO7:\tIO8:\tIO9:\tIO10:\tIO11:\tIO12:\tIO13:\tIO14:\tIO15:\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO0));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO1));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO2));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO3));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO4));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO5));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO6));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO7));
    Serial.print("\t");

    Serial.print(io.digitalRead(ExtensionIOXL9555::IO8));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO9));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO10));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO11));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO12));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO13));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO14));
    Serial.print("\t");
    Serial.print(io.digitalRead(ExtensionIOXL9555::IO15));
    Serial.println();
    delay(500);
}
