// clang-format off
#include <Arduino.h>
#include <Wire.h>

// I2C Pin Definition
#define I2C_SDA_PIN 7
#define I2C_SCL_PIN 8

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
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    // Perform initial scan
    scanI2CBus();
}

void loop() {
    // Rescan I2C bus every 3 seconds
    delay(3000);
    scanI2CBus();
}