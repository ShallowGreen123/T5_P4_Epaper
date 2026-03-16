// clang-format off
#include <Arduino.h>
#include <Wire.h>
#include "ExtensionIOXL9555.hpp"

// I2C Pin Definition
#define I2C_SDA_PIN 7
#define I2C_SCL_PIN 8


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


ExtensionIOXL9555 io;

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

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }

    // Initialize I2C bus
    // Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;
    if (io.init(Wire, I2C_SDA_PIN, I2C_SCL_PIN, chip_address)) {
        const uint8_t expands[] = {
            // BOARD_PCA_00_T_RST,
            // BOARD_PCA_01_CC_SW0,
            // BOARD_PCA_02_CC_SW1,
            // BOARD_PCA_03_LR_RST,
            // BOARD_PCA_04_NRF_CE,
            // BOARD_PCA_05_SHUTDOWN,
            // BOARD_PCA_06_HDMI_RST,
            // BOARD_PCA_07_HDMI_EN,
            // BOARD_PCA_10_EP_OE,
            // BOARD_PCA_11_EP_MODE,
            BOARD_PCA_12_1V8_EN,
            // BOARD_PCA_13_TPS_PWRUP,
            // BOARD_PCA_14_VCOM_CTRL,
            // BOARD_PCA_15_TPS_WAKEUP,
            BOARD_PCA_16_TPS_PWR_GOOD,
            // BOARD_PCA_17_TPS_INT
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

    io.pinMode(BOARD_PCA_07_HDMI_EN, OUTPUT);
    io.digitalWrite(BOARD_PCA_07_HDMI_EN, HIGH);

    io.pinMode(BOARD_PCA_06_HDMI_RST, OUTPUT);
    io.digitalWrite(BOARD_PCA_06_HDMI_RST, LOW);
    delay(50);
    io.digitalWrite(BOARD_PCA_06_HDMI_RST, HIGH);
    delay(50);
    
    // Perform initial scan
    scanI2CBus();
}

void loop() {
    // Rescan I2C bus every 3 seconds
    delay(3000);
    scanI2CBus();
}