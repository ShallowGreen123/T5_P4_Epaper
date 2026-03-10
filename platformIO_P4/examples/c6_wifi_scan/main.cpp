#include <Arduino.h>

#include "RemoteWiFi.h"

namespace {
constexpr uint32_t kScanIntervalMs = 5000;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("ESP32-P4 <-> ESP32-C6 SDIO Wi-Fi scan");
    Serial.println("Pins: D0=14 D1=15 D2=16 D3=17 CLK=18 CMD=19 RST=54 WAKEUP=6");

    if (!RemoteWiFi.begin()) {
        Serial.println("RemoteWiFi init failed.");
        while (true) {
            delay(1000);
            Serial.println("Check C6 esp-hosted slave firmware, SDIO wiring, and power.");
        }
    }

    Serial.println("RemoteWiFi init OK.");
    RemoteWiFi.printHostedInfo(Serial);
}

void loop() {
    Serial.println();
    Serial.println("Starting scan...");

    const int16_t found = RemoteWiFi.scanNetworks(false, true, false, 300);
    if (found == WIFI_SCAN_FAILED) {
        Serial.println("Scan failed.");
    } else if (found == 0) {
        Serial.println("No AP found.");
    } else {
        Serial.printf("Found %d AP(s).\r\n", found);
        RemoteWiFi.printScanTable(Serial, found);
    }

    RemoteWiFi.scanDelete();
    delay(kScanIntervalMs);
}
