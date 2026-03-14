#include <Arduino.h>
#include <RemoteWiFiHosted.h>
#include <time.h>

#define WIFI_SSID "YOUR_WIFI"
#define WIFI_PASS "YOUR_PASSWORD"

#define NTP_SERVER "ntp.aliyun.com"
#define GMT_OFFSET 28800
#define DAY_LIGHT_OFFSET 0

void print_time()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        Serial.println("RTC read failed");
        return;
    }

    char buffer[64];

    strftime(buffer,
             sizeof(buffer),
             "%Y-%m-%d %H:%M:%S",
             &timeinfo);

    Serial.println(buffer);
}

void rtc_ntp_sync()
{
    Serial.println("Start NTP sync");

    configTime(
        GMT_OFFSET,
        DAY_LIGHT_OFFSET,
        NTP_SERVER);

    struct tm timeinfo;

    int retry = 0;

    while (!getLocalTime(&timeinfo) && retry < 20)
    {
        Serial.print(".");
        delay(500);
        retry++;
    }

    Serial.println("");

    if (retry < 20)
        Serial.println("NTP sync success");
    else
        Serial.println("NTP sync failed");
}

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

    Serial.println("ESP32P4 RTC demo");

    wifi_connect();

    if (RemoteWiFi.isConnected())
    {
        rtc_ntp_sync();
    }
    else
    {
        Serial.println("Skip NTP sync (WiFi not connected)");
    }
}

void loop()
{
    print_time();

    delay(1000);
}
