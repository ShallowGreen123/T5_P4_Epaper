#include <Arduino.h>
#include <RemoteWiFiHosted.h>
#include <time.h>

#define WIFI_SSID "YOUR_WIFI"
#define WIFI_PASS "YOUR_PASSWORD"

void wifi_connect()
{
    if (!RemoteWiFi.begin())
    {
        Serial.println("Remote WiFi init failed");
        return;
    }

    Serial.print("Connecting WiFi");
    const wl_status_t result = RemoteWiFi.connect(WIFI_SSID, WIFI_PASS, 20000, true);

    Serial.println("");
    if (result == WL_CONNECTED)
    {
        Serial.printf("WiFi connected, IP: %s\r\n", RemoteWiFi.localIP().toString().c_str());
    }
    else
    {
        Serial.printf("WiFi connect failed, status=%d\r\n", static_cast<int>(result));
    }
}

void setup()
{
    Serial.begin(115200);

    delay(2000);

    Serial.println("ESP32P4 C6 WiFi demo");

    wifi_connect();

    if (RemoteWiFi.isConnected())
    {
        Serial.println("WiFi connected, IP: " + RemoteWiFi.localIP().toString());
    }
    else
    {
        Serial.println("Skip NTP sync (WiFi not connected)");
    }
}

void loop()
{
    delay(1000);
}
