/**
 *
 * @license MIT License
 *
 * Copyright (c) 2022 lewis he
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @file      GT911_GetPoint.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @date      2023-04-12
 *
 */
#include <Wire.h>
#include <SPI.h>
#include <Arduino.h>
#include "TouchDrvGT911.hpp"
#include "ExtensionIOXL9555.hpp"

#define SENSOR_SDA 7
#define SENSOR_SCL 8
#define SENSOR_IRQ 5

#define BOARD_PCA_00_T_RST        (0)
#define BOARD_PCA_01_CC_SW0       (1)
#define BOARD_PCA_02_CC_SW1       (2)
#define BOARD_PCA_03_LR_RST       (3)
#define BOARD_PCA_04_NRF_CE       (4)
#define BOARD_PCA_05_SHUTDOWN     (5)
#define BOARD_PCA_06_HDMI_RST     (6)
#define BOARD_PCA_07_HDMI_EN      (7)
#define BOARD_PCA_10_EP_OE        (8)
#define BOARD_PCA_11_EP_MODE      (9)
#define BOARD_PCA_12_1V8_EN       (10)
#define BOARD_PCA_13_TPS_PWRUP    (11)
#define BOARD_PCA_14_VCOM_CTRL    (12)
#define BOARD_PCA_15_TPS_WAKEUP   (13)
#define BOARD_PCA_16_TPS_PWR_GOOD (14)
#define BOARD_PCA_17_TPS_INT      (15)

TouchDrvGT911 touch;
int16_t x[5], y[5];


ExtensionIOXL9555 io;

static constexpr uint32_t EXTIO_PIN_BASE = 0x1000;
static constexpr uint32_t EXTIO_PIN(uint32_t pin) { return EXTIO_PIN_BASE + pin; }

static void gpioWrite(uint32_t pin, uint8_t value)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        io.digitalWrite((uint8_t)(pin - EXTIO_PIN_BASE), value);
        return;
    }
    digitalWrite((int)pin, value);
}

static int gpioRead(uint32_t pin)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        return io.digitalRead((uint8_t)(pin - EXTIO_PIN_BASE));
    }
    return digitalRead((int)pin);
}

static void gpioMode(uint32_t pin, uint8_t mode)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        io.pinMode((uint8_t)(pin - EXTIO_PIN_BASE), mode);
        return;
    }
    pinMode((int)pin, mode);
}


// Function to scan I2C bus
void scanI2CBus() {
    Serial.println("I2C Bus Scanner Started");
    Serial.println("Scanning range: 0x03 - 0x77 (7-bit addresses)");
    Serial.println();

    // Print table header
    Serial.println("+-----------------+---------------------+");
    Serial.println("| Hex Address     | Device Status       |");
    Serial.println("+-----------------+---------------------+");

    byte error, address;
    int nDevices = 0;

    // Scan address range 0x03 to 0x77
    for (address = 0x03; address <= 0x77; address++) {
        // Try to communicate with device
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        // Determine device status based on error code
        if (error == 0) {
            Serial.printf("| 0x%02X           | Connected          |\n", address);
            nDevices++;
        } else if (error == 4) {
            Serial.printf("| 0x%02X           | Communication Error|\n", address);
        }
        // Error codes 2, 3, 5 etc. indicate no response - not shown to keep table clean

        delay(1); // Short delay to avoid bus overload
    }

    // Print table footer
    Serial.println("+-----------------+---------------------+");
    Serial.println();
    Serial.printf("Scan completed. Found %d device(s)\n", nDevices);
    Serial.println("Note: Only connected devices or communication errors are shown");
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    while (!Serial);

    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;
    if (io.init(Wire, SENSOR_SDA, SENSOR_SCL, chip_address)) {
        const uint8_t expands[] = {
            BOARD_PCA_00_T_RST,
            BOARD_PCA_01_CC_SW0,
            BOARD_PCA_02_CC_SW1,
            BOARD_PCA_03_LR_RST,
            BOARD_PCA_04_NRF_CE,
            BOARD_PCA_05_SHUTDOWN,
            BOARD_PCA_06_HDMI_RST,
            BOARD_PCA_07_HDMI_EN,
            BOARD_PCA_10_EP_OE,
            BOARD_PCA_11_EP_MODE,
            BOARD_PCA_12_1V8_EN,
            BOARD_PCA_13_TPS_PWRUP,
            BOARD_PCA_14_VCOM_CTRL,
            BOARD_PCA_15_TPS_WAKEUP,
            BOARD_PCA_16_TPS_PWR_GOOD,
            BOARD_PCA_17_TPS_INT
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
    } else {
        while (1) {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }

    io.pinMode(BOARD_PCA_14_VCOM_CTRL, INPUT);
    io.pinMode(BOARD_PCA_15_TPS_WAKEUP, INPUT);

    // Speed up and stabilize GT911 init/read on ESP32.
    Wire.setClock(400000);

    // Let the driver control GT911 reset (via XL9555) + INT (via MCU GPIO),
    // so the I2C address is deterministically latched (0x5D) at boot.
    touch.setPins((int)EXTIO_PIN(BOARD_PCA_00_T_RST), SENSOR_IRQ);
    touch.setGpioCallback(gpioMode, gpioWrite, gpioRead);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L, SENSOR_SDA, SENSOR_SCL)) {
        while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
            scanI2CBus();
            delay(1000);
            if (touch.begin(Wire, GT911_SLAVE_ADDRESS_L, SENSOR_SDA, SENSOR_SCL))
            {
                Serial.println("GT911 found on retry!");
                break;
            }
        }
    }

    Serial.println("Init GT911 Sensor success!");

    // Set the center button to trigger the callback , Only for specific devices, e.g LilyGo-EPD47 S3 GT911
    touch.setHomeButtonCallback([](void *user_data) {
        Serial.println("Home button pressed!");
    }, NULL);

    

    /*
    *   GT911 Interrupt mode
    * * */
    // Low level when idle, converts to high level when touched
    // touch.setInterruptMode(HIGH_LEVEL_QUERY);

    // Keep low level when idle, and trigger on the falling edge after touching, trigger once at a frequency of 100HZ, and keep high level for 10ms
    // touch.setInterruptMode(RISING);

    // Keep high level when idle, and switch to low level when touched
    // touch.setInterruptMode(LOW_LEVEL_QUERY);

    // Maintains high level when idle, and is triggered by the falling edge after being touched. The frequency is 100HZ and is triggered once. Maintains 10ms in the low level interval
    // touch.setInterruptMode(FALLING);


    /*
    * GT911 Max touch point ,range: 1 ~ 5
    * */
    // touch.setMaxTouchPoint(1);

}

void loop()
{
    if (touch.isPressed()) {
        uint8_t touched = touch.getPoint(x, y, touch.getSupportTouchPoint());
        if (touched > 0) {
            Serial.print(millis());
            Serial.print("ms ");
            for (int i = 0; i < touched; ++i) {
                Serial.print("X[");
                Serial.print(i);
                Serial.print("]:");
                Serial.print(x[i]);
                Serial.print(" ");
                Serial.print(" Y[");
                Serial.print(i);
                Serial.print("]:");
                Serial.print(y[i]);
                Serial.print(" ");
            }
            Serial.println();
        }
    }
    delay(100);
}



