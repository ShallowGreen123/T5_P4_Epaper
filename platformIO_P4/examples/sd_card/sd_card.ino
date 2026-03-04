
// clang-format off
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// SPI 
#define BOARD_SPI_MISO (44)
#define BOARD_SPI_SCK  (45)
#define BOARD_SPI_MOSI (46)

// SD Card
#define BOARD_SD_CS    (47)
#define BOARD_SD_MISO  BOARD_SPI_MISO
#define BOARD_SD_SCK   BOARD_SPI_SCK 
#define BOARD_SD_MOSI  BOARD_SPI_MOSI

static void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
    File root = fs.open(dirname);
    if (!root) {
        Serial.println("open dir fail");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("not dir");
        root.close();
        return;
    }
    File file = root.openNextFile();
    while (file) {
        Serial.print(file.name());
        if (file.isDirectory()) {
            Serial.println("/");
            if (levels) {
                listDir(fs, file.path(), levels - 1);
            }
        } else {
            Serial.print("\t");
            Serial.println((uint32_t)file.size());
        }
        file = root.openNextFile();
    }
    root.close();
}

static void writeTestFile() {
    File f = SD.open("/sd_test.txt", FILE_WRITE);
    if (!f) {
        Serial.println("open write fail");
        return;
    }
    f.println("SD card test on ESP32-P4");
    f.println("Hello SD");
    f.close();
    Serial.println("write ok");
}

static void readTestFile() {
    File f = SD.open("/sd_test.txt");
    if (!f) {
        Serial.println("open read fail");
        return;
    }
    Serial.println("read begin:");
    while (f.available()) {
        Serial.write(f.read());
    }
    Serial.println();
    f.close();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("SD card test");

    SPI.begin(BOARD_SD_SCK, BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CS);
    if (!SD.begin(BOARD_SD_CS, SPI, 40000000)) {
        Serial.println("SD init fail");
        return;
    }
    Serial.println("SD init ok");

    uint8_t type = SD.cardType();
    Serial.print("type: ");
    Serial.println(type);
    uint64_t size = SD.cardSize();
    if (size) {
        Serial.print("size: ");
        Serial.print((uint32_t)(size / (1024ULL * 1024ULL)));
        Serial.println(" MB");
    }

    listDir(SD, "/", 1);
    writeTestFile();
    readTestFile();
}

void loop() {
    delay(2000);
}


// clang-format on

