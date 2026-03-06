#include <Arduino.h>
#include <Wire.h>

// I2C Pin Definitions for T5-Paper-P4
// Based on FastEPD configuration: SDA=7, SCL=8
#define I2C_SDA 7
#define I2C_SCL 8

// Device Addresses
#define BQ27220_ADDR 0x55
#define BQ25896_ADDR 0x6B

// BQ27220 Registers (Fuel Gauge)
#define BQ27220_REG_CNTL       0x00
#define BQ27220_REG_TEMP       0x06
#define BQ27220_REG_VOLTAGE    0x08
#define BQ27220_REG_CURRENT    0x0C
#define BQ27220_REG_FULL_CAP   0x12
#define BQ27220_REG_REM_CAP    0x10
#define BQ27220_REG_SOC        0x2C

// BQ25896 Registers (Charger)
#define BQ25896_REG_VBUS_STAT  0x0B
#define BQ25896_REG_FAULT      0x0C
#define BQ25896_REG_BAT_V      0x0E
#define BQ25896_REG_SYS_V      0x0F
#define BQ25896_REG_TS_PCT     0x10
#define BQ25896_REG_VBUS_V     0x11
#define BQ25896_REG_ICHG       0x12

// Function Prototypes
void scanI2C();
void testBQ27220();
void testBQ25896();
uint16_t readWord(uint8_t addr, uint8_t reg);
uint8_t readByte(uint8_t addr, uint8_t reg);
void writeByte(uint8_t addr, uint8_t reg, uint8_t data);

void setup() {
    Serial.begin(115200);
    // Wait for Serial to be ready
    delay(2000);
    Serial.println("T5-Paper-P4 Hardware Test");

    // Initialize I2C
    // The ESP32-P4 allows mapping I2C to any GPIO pins
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.printf("I2C Initialized on SDA=%d, SCL=%d\n", I2C_SDA, I2C_SCL);

    // Scan for devices
    scanI2C();
}

void loop() {
    Serial.println("\n--- Starting Hardware Check ---");
    
    // Test Fuel Gauge
    testBQ27220();

    // Test Charger
    testBQ25896();

    Serial.println("--- Check Complete ---\n");
    delay(5000);
}

// I2C Scanner to detect connected devices
void scanI2C() {
    byte error, address;
    int nDevices = 0;

    Serial.println("Scanning I2C bus...");

    for(address = 1; address < 127; address++ ) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            
            if (address == BQ27220_ADDR) Serial.print(" (BQ27220 Fuel Gauge)");
            if (address == BQ25896_ADDR) Serial.print(" (BQ25896 Charger)");
            
            Serial.println("  !");
            nDevices++;
        }
        else if (error == 4) {
            Serial.print("Unknown error at address 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
    else
        Serial.println("done\n");
}

// Read 16-bit word from I2C device (Little Endian)
uint16_t readWord(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false); // Restart
    Wire.requestFrom(addr, (uint8_t)2);
    
    if (Wire.available() >= 2) {
        uint8_t lsb = Wire.read();
        uint8_t msb = Wire.read();
        return (msb << 8) | lsb;
    }
    return 0;
}

// Read 8-bit byte from I2C device
uint8_t readByte(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false); // Restart
    Wire.requestFrom(addr, (uint8_t)1);
    
    if (Wire.available()) {
        return Wire.read();
    }
    return 0;
}

// Write 8-bit byte to I2C device
void writeByte(uint8_t addr, uint8_t reg, uint8_t data) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data);
    Wire.endTransmission();
}

void testBQ27220() {
    Serial.println("[BQ27220] Reading Fuel Gauge Data...");
    
    // Check if device exists
    Wire.beginTransmission(BQ27220_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[BQ27220] Device not found! (Check I2C connections or address)");
        return;
    }

    // Read Voltage (mV)
    uint16_t voltage = readWord(BQ27220_ADDR, BQ27220_REG_VOLTAGE);
    Serial.printf("  Voltage: %d mV\n", voltage);

    // Read Current (mA)
    // Note: Current is signed 16-bit
    int16_t current = (int16_t)readWord(BQ27220_ADDR, BQ27220_REG_CURRENT);
    Serial.printf("  Current: %d mA\n", current);

    // Read State of Charge (%)
    uint16_t soc = readWord(BQ27220_ADDR, BQ27220_REG_SOC);
    Serial.printf("  SOC: %d %%\n", soc & 0xFF); // Lower byte is SOC

    // Read Remaining Capacity (mAh)
    uint16_t remCap = readWord(BQ27220_ADDR, BQ27220_REG_REM_CAP);
    Serial.printf("  Remaining Capacity: %d mAh\n", remCap);

    // Read Full Charge Capacity (mAh)
    uint16_t fullCap = readWord(BQ27220_ADDR, BQ27220_REG_FULL_CAP);
    Serial.printf("  Full Capacity: %d mAh\n", fullCap);

    // Read Temperature (0.1 K)
    uint16_t tempK = readWord(BQ27220_ADDR, BQ27220_REG_TEMP);
    float tempC = (tempK / 10.0) - 273.15;
    Serial.printf("  Temperature: %.1f C\n", tempC);
}

void testBQ25896() {
    Serial.println("[BQ25896] Reading Charger Data...");

    // Check if device exists
    Wire.beginTransmission(BQ25896_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[BQ25896] Device not found! (Check I2C connections or address)");
        return;
    }

    // Force ADC conversion start (One-shot)
    // Register 02, bit 7 is CONV_START.
    // First read current config
    uint8_t config = readByte(BQ25896_ADDR, 0x02);
    writeByte(BQ25896_ADDR, 0x02, config | 0x80); 
    delay(100); // Wait for conversion

    // Read VBUS Status
    uint8_t status = readByte(BQ25896_ADDR, BQ25896_REG_VBUS_STAT);
    // Bit 7-6: VBUS Status (00: No Input, 01: USB Host, 10: Adapter, 11: OTG)
    // Bit 4-3: Charge Status (00: Not Charging, 01: Pre-charge, 10: Fast-charge, 11: Charge termination)
    
    Serial.print("  VBUS Status: ");
    uint8_t vbus_stat = (status >> 6) & 0x03;
    switch (vbus_stat) {
        case 0: Serial.println("No Input"); break;
        case 1: Serial.println("USB Host (SDP)"); break;
        case 2: Serial.println("Adapter (DCP)"); break;
        case 3: Serial.println("OTG Mode"); break;
    }

    Serial.print("  Charging Status: ");
    uint8_t chg_stat = (status >> 3) & 0x03;
    switch (chg_stat) {
        case 0: Serial.println("Not Charging"); break;
        case 1: Serial.println("Pre-charge"); break;
        case 2: Serial.println("Fast-charge"); break;
        case 3: Serial.println("Charge Termination Done"); break;
    }

    // Read Battery Voltage
    // REG0E: BATV (Battery Voltage) -> Bit 0-6, 2.304V base + 20mV step
    uint8_t batVal = readByte(BQ25896_ADDR, BQ25896_REG_BAT_V);
    float batV = 2.304 + (batVal & 0x7F) * 0.020;
    Serial.printf("  Battery Voltage: %.3f V\n", batV);

    // Read System Voltage
    // REG0F: SYSV (System Voltage) -> Bit 0-6, 2.304V base + 20mV step
    uint8_t sysVal = readByte(BQ25896_ADDR, BQ25896_REG_SYS_V);
    float sysV = 2.304 + (sysVal & 0x7F) * 0.020;
    Serial.printf("  System Voltage: %.3f V\n", sysV);

    // Read VBUS Voltage
    // REG11: VBUSV -> Bit 0-6, 2.6V base + 100mV step
    uint8_t vbusVal = readByte(BQ25896_ADDR, BQ25896_REG_VBUS_V);
    float vbusV = 2.6 + (vbusVal & 0x7F) * 0.100;
    Serial.printf("  VBUS Voltage: %.3f V\n", vbusV);
    
    // Read Charge Current
    // REG12: ICHG -> Bit 0-6, 0mA base + 50mA step
    uint8_t ichgVal = readByte(BQ25896_ADDR, BQ25896_REG_ICHG);
    float ichg = (ichgVal & 0x7F) * 50.0;
    Serial.printf("  Charge Current (Setting): %.0f mA\n", ichg);
}
