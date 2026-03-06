#include "AudioBoard.h"  //https://github.com/pschatzmann/arduino-audio-driver
#include "Audio.h"       //https://github.com/schreibfaul1/ESP32-audioI2S
#include "SD_MMC.h"

Audio audio;
DriverPins my_pins;
AudioBoard board(AudioDriverES8311, my_pins);  

//SD_MMC 
#define SD_D0    39
#define SD_D1    40
#define SD_D2    41
#define SD_D3    42
#define SD_CMD   44
#define SD_CLK   43

//ES8311 I2S
#define I2S_MCK_IO 13
#define I2S_BCK_IO 12
#define I2S_DI_IO 11
#define I2S_WS_IO 10  
#define I2S_DO_IO 9
#define ES8311_PA 53  //ES8311使能

// ES8311 I2C 
#define I2C_SDA 7  
#define I2C_SCL 8 
#define ES8311_ADDRESS 0x18

void scan_i2c_device(TwoWire &i2c)  //I2C 模块地址扫描函数
{
  Serial.println("Scanning for I2C devices ...");
  Serial.print("      ");
  for (int i = 0; i < 0x10; i++) {
    Serial.printf("0x%02X|", i);
  }
  uint8_t error;
  for (int j = 0; j < 0x80; j += 0x10) {
    Serial.println();
    Serial.printf("0x%02X |", j);
    for (int i = 0; i < 0x10; i++) {
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

void setup() {
  Serial.begin(115200);

  //ES8311使能
  pinMode(ES8311_PA, OUTPUT);
  digitalWrite(ES8311_PA, HIGH);
 
  // add i2c codec pins: scl, sda, port
  my_pins.addI2C(PinFunction::CODEC, I2C_SCL, I2C_SDA, ES8311_ADDRESS);

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0,SD_D1,SD_D2,SD_D3); //四线SD_MMC
   if (!SD_MMC.begin()) {    
    Serial.println("Card Mount Failed");
    return;
  }
  // 打印SD卡信息
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
  
  // configure codec 
  CodecConfig cfg;
  cfg.input_device = ADC_INPUT_ALL;//ADC_INPUT_LINE1; ADC_INPUT_ALL
  cfg.output_device = DAC_OUTPUT_ALL; 
  cfg.i2s.bits = BIT_LENGTH_16BITS;
  cfg.i2s.rate = RATE_44K;
  cfg.i2s.fmt = I2S_NORMAL;  
    
  //初始化ES8311
  board.begin(cfg);

  scan_i2c_device(Wire); //扫描I2C设备地址

  //调用audio库实现MP3输出
  audio.setPinout(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO,I2S_MCK_IO);
  audio.setVolume(4); // 0...21
  audio.connecttoFS(SD_MMC, "Angel.mp3");
  
}

void loop() {
audio.loop();
}

// optional
void audio_info(const char *info){
    Serial.print("info        "); Serial.println(info);
}
void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info){  //end of file
    Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info){
    Serial.print("station     ");Serial.println(info);
}
void audio_showstreamtitle(const char *info){
    Serial.print("streamtitle ");Serial.println(info);
}
void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}
