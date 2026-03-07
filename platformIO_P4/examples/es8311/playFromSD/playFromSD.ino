#include "AudioBoard.h" //https://github.com/pschatzmann/arduino-audio-driver
#include "Audio.h"      //https://github.com/schreibfaul1/ESP32-audioI2S
#include "SD.h"
#include <SPI.h>
#include "SD_MMC.h"
#include "ExtensionIOXL9555.hpp"

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

// debug helper
#define DEBUG_LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

// maximum number of tracks scanned from root
#define MUSIC_LIST_MAX 20

////////////////////////////////////////////////////////////////////////////////
// global objects
////////////////////////////////////////////////////////////////////////////////
Audio audio;
DriverPins my_pins;
AudioBoard board(AudioDriverES8311, my_pins);

ExtensionIOXL9555 io;
// storage for filenames (with leading slash)
String musicList[MUSIC_LIST_MAX];
int musicCount = 0;
int currentTrack = 0;

bool isAsciiName(const String &name)
{
    for (size_t i = 0; i < name.length(); ++i)
    {
        uint8_t c = name[i];
        if (c > 127) // non-ASCII
            return false;
    }
    return true;
}

// recursively scan the given fs directory depth 1, collecting mp3s
int scanMusicList(fs::FS &fs)
{
    musicCount = 0;
    File root = fs.open("/");
    if (!root || !root.isDirectory())
        return 0;

    File file = root.openNextFile();
    while (file && musicCount < MUSIC_LIST_MAX)
    {
        if (!file.isDirectory())
        {
            String name = String(file.name());
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".mp3") && isAsciiName(name))
            {
                String path = name;
                if (!path.startsWith("/"))
                    path = "/" + path;
                musicList[musicCount++] = path;
                DEBUG_LOG("found track %d: %s\n", musicCount - 1, path.c_str());
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return musicCount;
}

void playTrack(int idx)
{
    if (idx < 0 || idx >= musicCount)
        return;
    String &path = musicList[idx];
    DEBUG_LOG("playing track %d -> %s\n", idx, path.c_str());
    bool ok;
    ok = audio.connecttoFS(SD, path.c_str());
    if (!ok)
        Serial.println("audio.connecttoFS failed");
}


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
    DEBUG_LOG("initSDCard\n");
    SPI.begin(BOARD_SD_SCK, BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CS);
    // use a slightly lower clock speed; some cards lose data at 40MHz
    uint32_t spiFreq = 20000000;
    DEBUG_LOG("starting SD.begin with %u Hz\n", spiFreq);
    if (!SD.begin(BOARD_SD_CS, SPI, spiFreq))
    {
        Serial.println("SD init failed (SPI)");
        return false;
    }

    if (SD.cardType() == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return false;
    }
    uint64_t cardSizeMB = SD.cardSize() / (1024ULL * 1024ULL);
    Serial.printf("SD card size: %llu MB\n", cardSizeMB);
    DEBUG_LOG("SPI SD init succeeded\n");
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(100); // give serial time

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

    if (!initSDCard())
    {
        Serial.println("SD card init failed, aborting");
    }

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

    // configure audio library and enlarge buffer to avoid overflow
    audio.setPinout(BOARD_ES8311_SCLK, BOARD_ES8311_LRCK, BOARD_ES8311_ASDOUT, BOARD_ES8311_MCLK);
    audio.setVolume(4); // 0...21
    // increase input buffer size (use PSRAM) to 64KB
    audio.setInBufferSize(64 * 1024);
    DEBUG_LOG("in buffer size now %u\n", audio.getInBufferSize());

    // build playlist by scanning root
    musicCount = scanMusicList(SD);

    if (musicCount > 0)
    {
        currentTrack = 0;
        playTrack(currentTrack);
    }
    else
    {
        DEBUG_LOG("no mp3 files found\n");
    }
}

void loop()
{
    static bool wasRunning = false;
    audio.loop();

    // debug buffer levels occasionally
    static uint32_t lastReport = 0;
    uint32_t now = millis();
    if (now - lastReport > 2000) {
        lastReport = now;
        if(audio.inBufferFree() != 0) {
            DEBUG_LOG("in buffer free=%u filled=%u\n", audio.inBufferFree(), audio.inBufferFilled());
        }
    }

    bool running = audio.isRunning();
    if (running && !wasRunning)
        Serial.println("=== playback started ===");
    if (!running && wasRunning)
    {
        Serial.println("=== playback stopped ===");
        if (musicCount > 0)
        {
            // advance to next track
            currentTrack = (currentTrack + 1) % musicCount;
            Serial.printf("next track index %d\n", currentTrack);
            playTrack(currentTrack);
    
        }
        else
        {
            DEBUG_LOG("no mp3 files found\n");
        }
    }
    wasRunning = running;
}
