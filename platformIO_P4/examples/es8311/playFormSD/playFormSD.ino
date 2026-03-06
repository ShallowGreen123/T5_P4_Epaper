#include "AudioBoard.h" //https://github.com/pschatzmann/arduino-audio-driver
#include "Audio.h"      //https://github.com/schreibfaul1/ESP32-audioI2S
#include "SD.h"
#include <SPI.h>
#include "SD_MMC.h"
#include "ExtensionIOXL9555.hpp"

#define ESP32P4_FUNCTOPN_BOARD 0
#define ESP32P4_FUNCTOPN_BOARD_USE_SPI 1

#if ESP32P4_FUNCTOPN_BOARD

#define BOARD_I2C_SDA (7)
#define BOARD_I2C_SCL (8)

#define BOARD_SD_MISO (39)
#define BOARD_SD_CS (42)
#define BOARD_SD_MOSI (44)
#define BOARD_SD_SCK (43)

#define SD_D0 39
#define SD_D1 40
#define SD_D2 41
#define SD_D3 42
#define SD_CMD 44
#define SD_CLK 43

// ES8311
#define BOARD_I2C_ADDR_ES8311 (0x18)
#define BOARD_ES8311_SCL (BOARD_I2C_SCL)
#define BOARD_ES8311_SDA (BOARD_I2C_SDA)
#define BOARD_ES8311_MCLK (13)
#define BOARD_ES8311_SCLK (12)
#define BOARD_ES8311_ASDOUT (9)
#define BOARD_ES8311_LRCK (10)
#define BOARD_ES8311_DSDIN (11)
#define BOARD_ES8311_PA (53)

#else
// ES8311 I2C
#define I2C_SDA 7
#define I2C_SCL 8

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

// SD Card (SPI)
#define BOARD_SD_CS (47)
#define BOARD_SD_MISO (44)
#define BOARD_SD_SCK (45)
#define BOARD_SD_MOSI (46)

#endif

Audio audio;
DriverPins my_pins;
AudioBoard board(AudioDriverES8311, my_pins);

ExtensionIOXL9555 io;

void scan_i2c_device(TwoWire &i2c) // I2C 模块地址扫描函数
{
    Serial.println("Scanning for I2C devices ...");
    Serial.print("      ");
    for (int i = 0; i < 0x10; i++)
    {
        Serial.printf("0x%02X|", i);
    }
    uint8_t error;
    for (int j = 0; j < 0x80; j += 0x10)
    {
        Serial.println();
        Serial.printf("0x%02X |", j);
        for (int i = 0; i < 0x10; i++)
        {
            Wire.beginTransmission(i | j);
            error = Wire.endTransmission();
            if (error == 0)
                Serial.printf("0x%02X|", i | j);
            else
                Serial.print(" -- |");
        }
    }
    Serial.println();
}

bool initSDCard()
{
#if ESP32P4_FUNCTOPN_BOARD_USE_SPI
    SPI.begin(BOARD_SD_SCK, BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CS);
    if (!SD.begin(BOARD_SD_CS, SPI, 40000000))
    {
        Serial.println("SD init failed");
        return false;
    }

    if (SD.cardType() == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return false;
    }
    uint64_t cardSizeMB = SD.cardSize() / (1024ULL * 1024ULL);
    Serial.printf("SD card size: %llu MB\n", cardSizeMB);
#else
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3); // 四线SD_MMC
    if (!SD_MMC.begin())
    {
        Serial.println("Card Mount Failed");
        return false;
    }
    // 打印SD卡信息
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return false;
    }
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC)
    {
        Serial.println("MMC");
    }
    else if (cardType == CARD_SD)
    {
        Serial.println("SDSC");
    }
    else if (cardType == CARD_SDHC)
    {
        Serial.println("SDHC");
    }
    else
    {
        Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
#endif
    return true;
}

void setup()
{
    Serial.begin(115200);

#if ESP32P4_FUNCTOPN_BOARD
    // ES8311使能
    pinMode(BOARD_ES8311_PA, OUTPUT);
    digitalWrite(BOARD_ES8311_PA, HIGH);
#else
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
#endif
    // add i2c codec pins: scl, sda, port
    my_pins.addI2C(PinFunction::CODEC, BOARD_I2C_SCL, BOARD_I2C_SDA, BOARD_I2C_ADDR_ES8311);

    initSDCard();

    // configure codec
    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_ALL; // ADC_INPUT_LINE1; ADC_INPUT_ALL
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    cfg.i2s.fmt = I2S_NORMAL;

    // 初始化ES8311
    board.begin(cfg);

    scan_i2c_device(Wire); // 扫描I2C设备地址

    // 调用audio库实现MP3输出
    audio.setPinout(BOARD_ES8311_SCLK, BOARD_ES8311_LRCK, BOARD_ES8311_ASDOUT, BOARD_ES8311_MCLK);
    audio.setVolume(4); // 0...21
#if ESP32P4_FUNCTOPN_BOARD_USE_SPI
    audio.connecttoFS(SD, "Angel.mp3");
#else
    audio.connecttoFS(SD_MMC, "Angel.mp3");
#endif
    
}

void loop()
{
    audio.loop();
}

// optional
void audio_info(const char *info)
{
    Serial.print("info        ");
    Serial.println(info);
}
void audio_id3data(const char *info)
{ // id3 metadata
    Serial.print("id3data     ");
    Serial.println(info);
}
void audio_eof_mp3(const char *info)
{ // end of file
    Serial.print("eof_mp3     ");
    Serial.println(info);
}
void audio_showstation(const char *info)
{
    Serial.print("station     ");
    Serial.println(info);
}
void audio_showstreamtitle(const char *info)
{
    Serial.print("streamtitle ");
    Serial.println(info);
}
void audio_bitrate(const char *info)
{
    Serial.print("bitrate     ");
    Serial.println(info);
}
void audio_commercial(const char *info)
{ // duration in sec
    Serial.print("commercial  ");
    Serial.println(info);
}
void audio_icyurl(const char *info)
{ // homepage
    Serial.print("icyurl      ");
    Serial.println(info);
}
void audio_lasthost(const char *info)
{ // stream URL played
    Serial.print("lasthost    ");
    Serial.println(info);
}
