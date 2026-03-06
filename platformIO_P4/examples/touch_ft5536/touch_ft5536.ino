#include "Wire.h"
#include "Arduino.h"
#include "ft5536.h"
#include "ExtensionIOXL9555.hpp"

#define SENSOR_SDA 7
#define SENSOR_SCL 8
#define SENSOR_IRQ 5
// #define SENSOR_RST 9

#define BOARD_PCA_00_T_RST        (0)
#define BOARD_PCA_01_CC_SW0       (1)
#define BOARD_PCA_02_CC_SW1       (2)
#define BOARD_PCA_03_LR_RST       (3)
#define BOARD_PCA_04_NRF_CE       (4)
#define BOARD_PCA_05_SHUTDOWN     (5)
#define BOARD_PCA_06_HDMI_RST     (6)
#define BOARD_PCA_07_HDMI_EN      (7)
#define BOARD_PCA_10_EP_OE        (8)
#define BOARD_PCA_11_EP_MODE      (9)
#define BOARD_PCA_12_1V8_EN       (10)
#define BOARD_PCA_13_TPS_PWRUP    (11)
#define BOARD_PCA_14_VCOM_CTRL    (12)
#define BOARD_PCA_15_TPS_WAKEUP   (13)
#define BOARD_PCA_16_TPS_PWR_GOOD (14)
#define BOARD_PCA_17_TPS_INT      (15)

ExtensionIOXL9555 io;

void setup()
{
    Serial.begin(115200);
    Wire.begin(SENSOR_SDA, SENSOR_SCL);

    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;
    if (io.init(Wire, SENSOR_SDA, SENSOR_SCL, chip_address)) {
        const uint8_t expands[] = {
            BOARD_PCA_00_T_RST,
            BOARD_PCA_01_CC_SW0,
            BOARD_PCA_02_CC_SW1,
            BOARD_PCA_03_LR_RST,
            BOARD_PCA_04_NRF_CE,
            BOARD_PCA_05_SHUTDOWN,
            BOARD_PCA_06_HDMI_RST,
            BOARD_PCA_07_HDMI_EN,
            BOARD_PCA_10_EP_OE,
            BOARD_PCA_11_EP_MODE,
            BOARD_PCA_12_1V8_EN,
            BOARD_PCA_13_TPS_PWRUP,
            BOARD_PCA_14_VCOM_CTRL,
            BOARD_PCA_15_TPS_WAKEUP,
            BOARD_PCA_16_TPS_PWR_GOOD,
            BOARD_PCA_17_TPS_INT
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
    } else {
        while (1) {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }

    io.digitalWrite(BOARD_PCA_00_T_RST, LOW);

    ft5536_check_id();
}

void loop()
{
    delay(50);

    //   ft5536_check_id();

    fts_touch_process();

}
