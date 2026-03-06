#include <Arduino.h>
#include "RemoteWiFi.h"

// Define WL_NO_MODULE if not available in standard headers for this platform
#ifndef WL_NO_MODULE
#define WL_NO_MODULE 255
#endif

void setup() {
    // 1. Initialize Serial
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    Serial.println("ESP32-P4 WiFi Scan Demo via SDIO (C6 Slave)");

    // 2. Initialize RemoteWiFi (Handshake with C6)
    Serial.println("Initializing RemoteWiFi...");
    if (!RemoteWiFi.begin()) {
        Serial.println("RemoteWiFi initialization failed!");
        // Automatic reset retry logic is handled inside begin() -> initSDIO() retry loop
        // If still fails, halt
        while (1) {
            delay(1000);
            Serial.println("Error: Check C6 connection or firmware.");
        }
    }
    Serial.println("RemoteWiFi initialized successfully.");
}

void loop() {
    Serial.println("Scanning start...");

    // 3. Scan Networks (Async = false, blocking for simplicity in this demo)
    int n = RemoteWiFi.scanNetworks();
    
    Serial.println("Scan done");
    if (n == 0) {
        Serial.println("no networks found");
    } else if (n < 0) {
        Serial.println("Scan failed");
    } else {
        Serial.print(n);
        Serial.println(" networks found");
        Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
        for (int i = 0; i < n; ++i) {
            // Print SSID and RSSI for each network found
            Serial.printf("%2d | %-32s | %4d | %2d | %s\n", 
                i + 1, 
                RemoteWiFi.SSID(i).c_str(),
                RemoteWiFi.RSSI(i),
                RemoteWiFi.channel(i),
                RemoteWiFi.encryptionTypeStr(RemoteWiFi.encryptionType(i)).c_str()
            );
            delay(10);
        }
    }
    Serial.println("");

    // 4. Wait 5 seconds before next scan
    delay(5000);
}
