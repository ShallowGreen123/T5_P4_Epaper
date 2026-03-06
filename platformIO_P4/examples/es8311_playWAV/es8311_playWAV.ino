#include "AudioBoard.h" // https://github.com/pschatzmann/arduino-audio-driver
#include "Audio.h"      // https://github.com/schreibfaul1/ESP32-audioI2S
#include <SD.h>
#include <SPI.h>
#include "ExtensionIOXL9555.hpp"

Audio audio;
DriverPins my_pins;
AudioBoard board(AudioDriverES8311, my_pins);
ExtensionIOXL9555 io;

// XL9555
#define BOARD_I2C_SDA (7)
#define BOARD_I2C_SCL (8)

// Connected to XL9555 IO05, enable power amplifier
#define BOARD_XL9555_05_AMPLIFIER (5)

// ES8311
#define BOARD_I2C_ADDR_ES8311   (0x18)
#define BOARD_ES8311_SCL        (BOARD_I2C_SCL)
#define BOARD_ES8311_SDA        (BOARD_I2C_SDA)
#define BOARD_ES8311_MCLK       (43)
#define BOARD_ES8311_SCLK       (42)
#define BOARD_ES8311_ASDOUT     (39)
#define BOARD_ES8311_LRCK       (40)
#define BOARD_ES8311_DSDIN      (41)

// SD Card (SPI)
#define BOARD_SD_CS     (47)
#define BOARD_SD_MISO   (44)
#define BOARD_SD_SCK    (45)
#define BOARD_SD_MOSI   (46)

static const char *DEFAULT_MP3_PATH = "/1.mp3";

enum PlaybackStage
{
    PLAYBACK_IDLE = 0,
    PLAYBACK_MP3,
    PLAYBACK_DONE
};

PlaybackStage playbackStage = PLAYBACK_IDLE;
String mp3Path;

void scan_i2c_device(TwoWire &i2c)
{
    Serial.println("Scanning for I2C devices ...");
    Serial.print("      ");
    for (int i = 0; i < 0x10; ++i)
    {
        Serial.printf("0x%02X|", i);
    }
    uint8_t error;
    for (int j = 0; j < 0x80; j += 0x10)
    {
        Serial.println();
        Serial.printf("0x%02X |", j);
        for (int i = 0; i < 0x10; ++i)
        {
            i2c.beginTransmission(i | j);
            error = i2c.endTransmission();
            if (error == 0)
            {
                Serial.printf("0x%02X|", i | j);
            }
            else
            {
                Serial.print(" -- |");
            }
        }
    }
    Serial.println();
}

bool initCodecAndAudio()
{
    my_pins.addI2C(PinFunction::CODEC, BOARD_ES8311_SCL, BOARD_ES8311_SDA, BOARD_I2C_ADDR_ES8311);
    my_pins.addI2S(PinFunction::CODEC, BOARD_ES8311_MCLK, BOARD_ES8311_SCLK, BOARD_ES8311_LRCK,
                   BOARD_ES8311_DSDIN, BOARD_ES8311_ASDOUT);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_NONE;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    cfg.i2s.fmt = I2S_NORMAL;

    if (!board.begin(cfg))
    {
        Serial.println("ES8311 init failed");
        return false;
    }

    board.setVolume(70);
    audio.setPinout(BOARD_ES8311_SCLK, BOARD_ES8311_LRCK, BOARD_ES8311_ASDOUT, BOARD_ES8311_MCLK);
    audio.setVolume(3); // 0...21
    return true;
}

bool initSDCard()
{
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
    return true;
}

bool findFirstMp3(fs::FS &fs, const char *dirname, uint8_t levels, String &outPath)
{
    File root = fs.open(dirname);
    if (!root || !root.isDirectory())
    {
        return false;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            if (levels > 0 && findFirstMp3(fs, file.path(), levels - 1, outPath))
            {
                file.close();
                root.close();
                return true;
            }
        }
        else
        {
            String name = String(file.name());
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".mp3"))
            {
                outPath = String(file.path());
                if (!outPath.startsWith("/"))
                {
                    outPath = "/" + outPath;
                }
                file.close();
                root.close();
                return true;
            }
        }
        file = root.openNextFile();
    }

    root.close();
    return false;
}

bool startPlayFromSD(const char *path)
{
    if (!path || !SD.exists(path))
    {
        Serial.printf("File not found: %s\n", path ? path : "(null)");
        return false;
    }

    audio.stopSong();
    if (!audio.connecttoFS(SD, path))
    {
        Serial.printf("Playback start failed: %s\n", path);
        return false;
    }

    Serial.printf("Now playing: %s\n", path);
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(200);

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

    if (!initCodecAndAudio())
    {
        return;
    }

    scan_i2c_device(Wire);

    if (!initSDCard())
    {
        return;
    }

    if (!findFirstMp3(SD, "/", 3, mp3Path))
    {
        if (SD.exists(DEFAULT_MP3_PATH))
        {
            mp3Path = DEFAULT_MP3_PATH;
        }
    }

    if (mp3Path.length() > 0 && startPlayFromSD(mp3Path.c_str()))
    {
        playbackStage = PLAYBACK_MP3;
        return;
    }

    Serial.println("No MP3 file found on SD.");
    playbackStage = PLAYBACK_DONE;
}

void loop()
{
    audio.loop();

    if (playbackStage == PLAYBACK_MP3 && !audio.isRunning())
    {
        playbackStage = PLAYBACK_DONE;
        Serial.println("MP3 playback finished.");
    }
}

void audio_info(const char *info)
{
    Serial.print("info        ");
    Serial.println(info);
}

void audio_id3data(const char *info)
{
    Serial.print("id3data     ");
    Serial.println(info);
}

void audio_eof_mp3(const char *info)
{
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
{
    Serial.print("commercial  ");
    Serial.println(info);
}

void audio_icyurl(const char *info)
{
    Serial.print("icyurl      ");
    Serial.println(info);
}

void audio_lasthost(const char *info)
{
    Serial.print("lasthost    ");
    Serial.println(info);
}
