#include "AudioBoard.h" //https://github.com/pschatzmann/arduino-audio-driver
#include "Audio.h"      //https://github.com/schreibfaul1/ESP32-audioI2S
#include "SD.h"
#include "SPI.h"
#include "FS.h"
#include "Ticker.h"
#include <stdio.h>

#include "FS.h"
#include "SPIFFS.h"
#include "ExtensionIOXL9555.hpp"

// XL9555
#define BOARD_I2C_SDA (7)
#define BOARD_I2C_SCL (8)

// Connected to XL9555 IO05, enable power amplifier
#define BOARD_XL9555_05_AMPLIFIER (5)

// ES8311
#define BOARD_I2C_ADDR_ES8311 (0x18)
#define BOARD_ES8311_SCL (BOARD_I2C_SCL)
#define BOARD_ES8311_SDA (BOARD_I2C_SDA)
#define BOARD_ES8311_MCLK (43)
#define BOARD_ES8311_SCLK (42)
#define BOARD_ES8311_ASDOUT (39)
#define BOARD_ES8311_LRCK (40)
#define BOARD_ES8311_DSDIN (41)

#define ASSERT_FLAG(x) \
    if (x == false)    \
        Serial.printf("[%d] Execution error\n", __LINE__);

Audio audio;
DriverPins my_pins;
AudioBoard board(AudioDriverES8311, my_pins);
ExtensionIOXL9555 io;

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}


void setup()
{
    Serial.begin(115200);

    if (!io.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, XL9555_SLAVE_ADDRESS0))
    {
        while (1)
        {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }

    io.configPort(ExtensionIOXL9555::PORT0, 0x00);
    io.configPort(ExtensionIOXL9555::PORT1, 0x00);
    io.digitalWrite(BOARD_XL9555_05_AMPLIFIER, HIGH);

    // add i2c codec pins: scl, sda, port
    my_pins.addI2C(PinFunction::CODEC, BOARD_I2C_SCL, BOARD_I2C_SDA, BOARD_I2C_ADDR_ES8311);

    // configure codec
    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_ALL; // ADC_INPUT_LINE1; ADC_INPUT_ALL
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    cfg.i2s.fmt = I2S_NORMAL;

    // 初始化ES8311
    board.begin(cfg);

    if(!SPIFFS.begin(true)){
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    Serial.println("*************** SPIFFS ****************");
    listDir(SPIFFS, "/", 0);
    Serial.println("**************************************");

    audio.setPinout(BOARD_ES8311_SCLK, BOARD_ES8311_LRCK, BOARD_ES8311_ASDOUT, BOARD_ES8311_MCLK);
    audio.setVolume(4); // 0...21
    audio.connecttoFS(SPIFFS, "/iphone_call.mp3");
}

void loop()
{
    audio.loop();
    // 判断音频是否播放完成
    if (!audio.isRunning())
    {
        Serial.println("Audio playback finished.");
        // 可以在这里执行其他操作，例如重新播放音频或执行其他任务
        audio.connecttoFS(SPIFFS, "/iphone_call.mp3"); // 重新播放音频
    }

    // delay(1);
}