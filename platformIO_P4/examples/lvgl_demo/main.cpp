/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "lvgl.h"
#include <FastEPD.h>
#include "Wire.h"
#include <Arduino.h>
#include <SPI.h>
#include "ui.h"
#include "scr_mrg.h"
#include "TouchDrvGT911.hpp"
#include "ExtensionIOXL9555.hpp" // expander used for pin control on some boards


// I2C Pin Definition
#define I2C_SDA_PIN 7
#define I2C_SCL_PIN 8

// translate to the names used by the GT911 example
#define SENSOR_SDA       I2C_SDA_PIN
#define SENSOR_SCL       I2C_SCL_PIN
#define SENSOR_IRQ       5  // interrupt pin from touch controller


#define DISP_WIDTH 1440
#define DISP_HEIGHT 720
#define DISP_BUF_SIZE (DISP_WIDTH * DISP_HEIGHT)

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


LV_IMG_DECLARE(img_test)

// 1 = 4bpp grayscale (2 pixels per byte)
// 0 = 1bpp (1 bit per pixel)
#define EPD_USE_4BPP_GRAY 1

// 1 = enable ordered dithering (better gradients in 1bpp)
// 0 = simple threshold
#define EPD_ENABLE_DITHER 1

// Mirror controls for LVGL flush
// EPD_MIRROR_MODE: 0 = normal, 1 = mirror X, 2 = mirror Y, 3 = mirror X+Y (180 deg)
#define EPD_MIRROR_MODE 0
// EPD_ROTATION: 0, 90, 180, 270 (rotation applied when writing to panel buffer)
#define EPD_ROTATION 0

// 4bpp update strategy:
// 0 = always flashing full refresh (legacy behavior)
// 1 = first refresh uses CLEAR_SLOW, then CLEAR_NONE to reduce flashing
#define EPD_4BPP_LOW_FLASH 1

// --- GT911 touch globals --------------------------------------------------
TouchDrvGT911 touch;
int16_t gt_x[5], gt_y[5];

ExtensionIOXL9555 io;

static constexpr uint32_t EXTIO_PIN_BASE = 0x1000;
static constexpr uint32_t EXTIO_PIN(uint32_t pin) { return EXTIO_PIN_BASE + pin; }

static void gpioWrite(uint32_t pin, uint8_t value)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        io.digitalWrite((uint8_t)(pin - EXTIO_PIN_BASE), value);
        return;
    }
    digitalWrite((int)pin, value);
}

static int gpioRead(uint32_t pin)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        return io.digitalRead((uint8_t)(pin - EXTIO_PIN_BASE));
    }
    return digitalRead((int)pin);
}

static void gpioMode(uint32_t pin, uint8_t mode)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        io.pinMode((uint8_t)(pin - EXTIO_PIN_BASE), mode);
        return;
    }
    pinMode((int)pin, mode);
}

// --------------------------------------------------------------------------



FASTEPD epaper;
uint8_t *decodebuffer = NULL;
uint8_t *pFramebuffer;

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t x1 = area->x1;
    const int32_t y1 = area->y1;
    const int32_t x2 = area->x2;
    const int32_t y2 = area->y2;

    const int32_t w = x2 - x1 + 1;
    const int32_t h = y2 - y1 + 1;

#if EPD_USE_4BPP_GRAY
    const int32_t pitch = DISP_WIDTH / 2; // 4bpp: 2 pixels per byte
#else
    const int32_t pitch = (DISP_WIDTH + 7) / 8; // 1bpp: 8 pixels per byte
    static const uint8_t bayer4[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
#endif

    for (int32_t y = 0; y < h; y++)
    {
        int32_t src_y = y;
#if (EPD_MIRROR_MODE & 0x2)
        src_y = (h - 1) - y;
#endif
        for (int32_t x = 0; x < w; x++)
        {
            int32_t src_x = x;
#if (EPD_MIRROR_MODE & 0x1)
            src_x = (w - 1) - x;
#endif
            lv_color_t c = color_p[src_y * w + src_x];

            // RGB565 -> 8-bit gray
            uint8_t r = LV_COLOR_GET_R(c);
            uint8_t g = LV_COLOR_GET_G(c);
            uint8_t b = LV_COLOR_GET_B(c);
            uint8_t r8 = (r << 3) | (r >> 2);
            uint8_t g8 = (g << 2) | (g >> 4);
            uint8_t b8 = (b << 3) | (b >> 2);
            uint8_t gray = (r8 * 76 + g8 * 150 + b8 * 30) >> 8; // 0..255

            int32_t logical_x = x1 + x;
            int32_t logical_y = y1 + y;
            int32_t dst_x = logical_x;
            int32_t dst_y = logical_y;

#if (EPD_ROTATION == 90)
            dst_x = (DISP_WIDTH - 1) - logical_y;
            dst_y = logical_x;
#elif (EPD_ROTATION == 180)
            dst_x = (DISP_WIDTH - 1) - logical_x;
            dst_y = (DISP_HEIGHT - 1) - logical_y;
#elif (EPD_ROTATION == 270)
            dst_x = logical_y;
            dst_y = (DISP_HEIGHT - 1) - logical_x;
#endif
#if EPD_USE_4BPP_GRAY
            uint8_t g4 = gray >> 4; // 0..15
            int32_t idx = dst_y * pitch + (dst_x >> 1);

            if ((dst_x & 1) == 0)
            {
                // even x -> high nibble
                pFramebuffer[idx] = (pFramebuffer[idx] & 0x0F) | (g4 << 4);
            }
            else
            {
                // odd x -> low nibble
                pFramebuffer[idx] = (pFramebuffer[idx] & 0xF0) | g4;
            }
#else
            int32_t idx = dst_y * pitch + (dst_x >> 3);
            uint8_t mask = 0x80 >> (dst_x & 7);

#if EPD_ENABLE_DITHER
            uint8_t threshold = (uint8_t)(bayer4[dst_y & 3][dst_x & 3] * 16);
            bool is_white = (gray >= threshold);
#else
            bool is_white = (gray >= 128);
#endif

            if (is_white)
            {
                pFramebuffer[idx] |= mask; // white
            }
            else
            {
                pFramebuffer[idx] &= (uint8_t)~mask; // black
            }
#endif
        }
    }

    // full or partial update
#if EPD_USE_4BPP_GRAY
    if (lv_disp_flush_is_last(disp))
    {
#if EPD_4BPP_LOW_FLASH
        static bool s_first_4bpp_refresh = true;
        if (s_first_4bpp_refresh)
        {
            // Do one strong clear pass after boot, then use no-clear updates.
            epaper.fullUpdate(CLEAR_SLOW, true);
            s_first_4bpp_refresh = false;
        }
        else
        {
            epaper.fullUpdate(CLEAR_NONE, true);
        }
#else
        epaper.fullUpdate(CLEAR_SLOW, true);
#endif
    }
#else
    epaper.partialUpdate(true, y1, y2);
    // epaper.fullUpdate(true, true);
#endif
    // epaper.einkPower(true);
    // or partial update: epaper.partialUpdate(true, y1, y2);

    lv_disp_flush_ready(disp);
}


static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    // Do not gate on IRQ level. GT911 may be configured for edge-triggered IRQ
    // (short pulses), which LVGL's default indev read period can miss. Polling
    // the point register is deterministic and works regardless of IRQ mode.
    uint8_t touched = touch.getPoint(gt_x, gt_y, 1);
    if (touched > 0) {
        uint16_t touch_x = gt_x[0];
        uint16_t touch_y = gt_y[0];

// #if EPD_ROTATION == 0
//         // Landscape inverted (1440x720)
//         last_x = touch_y;
//         last_y = DISP_HEIGHT - touch_x;
// #elif EPD_ROTATION == 90
//         // Portrait inverted (720x1440)
//         last_x = DISP_HEIGHT - touch_x;
//         last_y = DISP_WIDTH - touch_y;
// #elif EPD_ROTATION == 180
//         // Landscape (1440x720)
//         last_x = DISP_WIDTH - touch_y;
//         last_y = touch_x;
// #elif EPD_ROTATION == 270
//         // Portrait (720x1440)
//         last_x = touch_x;
//         last_y = touch_y;
// #endif

#if EPD_ROTATION == 0
        // Landscape (1440x720)
        last_x = DISP_WIDTH - touch_y;
        last_y = touch_x;
#elif EPD_ROTATION == 90
        // Portrait (720x1440)
        last_x = touch_x;
        last_y = touch_y;
#elif EPD_ROTATION == 180
        // Landscape inverted (1440x720)
        last_x = touch_y;
        last_y = DISP_HEIGHT - touch_x;
#elif EPD_ROTATION == 270
        // Portrait inverted (720x1440)
        last_x = DISP_HEIGHT - touch_x;
        last_y = DISP_WIDTH - touch_y;
#endif
        if (last_x < 0) last_x = 0;
        if (last_y < 0) last_y = 0;

        data->state = LV_INDEV_STATE_PR;
        Serial.printf("touch pressed: %d, %d (raw: %d, %d)\n", last_x, last_y, touch_x, touch_y);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

void lv_port_disp_init(void)
{
    lv_init();

    static lv_disp_draw_buf_t draw_buf;

    lv_color_t *lv_disp_buf_1 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    lv_color_t *lv_disp_buf_2 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    Serial.printf("epaper w = %d, h = %d\n", epaper.width(), epaper.height());

    lv_disp_draw_buf_init(&draw_buf, lv_disp_buf_1, lv_disp_buf_2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
#if (EPD_ROTATION == 90) || (EPD_ROTATION == 270)
    disp_drv.hor_res = DISP_HEIGHT;
    disp_drv.ver_res = DISP_WIDTH;
#else
    disp_drv.hor_res = DISP_WIDTH;
    disp_drv.ver_res = DISP_HEIGHT;
#endif
    disp_drv.flush_cb = disp_flush;
    // disp_drv.render_start_cb = dips_render_start_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    /*Register a touchpad input device*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

void idf_setup()
{
    Serial.begin(115200);

    // initialize IO expander so we can control reset/irq lines if wired that way
    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;
    if (io.init(Wire, I2C_SDA_PIN, I2C_SCL_PIN, chip_address)) {
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

    io.pinMode(BOARD_PCA_14_VCOM_CTRL, INPUT);
    io.pinMode(BOARD_PCA_15_TPS_WAKEUP, INPUT);

    // Speed up & stabilise GT911 on ESP32
    Wire.setClock(400000);

    touch.setPins((int)EXTIO_PIN(BOARD_PCA_00_T_RST), SENSOR_IRQ);
    touch.setGpioCallback(gpioMode, gpioWrite, gpioRead);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L, SENSOR_SDA, SENSOR_SCL)) {
        while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
            delay(1000);
            if (touch.begin(Wire, GT911_SLAVE_ADDRESS_L, SENSOR_SDA, SENSOR_SCL)) {
                Serial.println("GT911 found on retry!");
                break;
            }
        }
    }

    Serial.println("Init GT911 Sensor success!");
    // Prefer level-triggered mode for reliable polling/readout in GUI loops.
    // (Edge-triggered modes generate short pulses which can be missed.)
    touch.setInterruptMode(LOW_LEVEL_QUERY);

    epaper.initPanel(BB_PANEL_LILYGO_T5P4, 40000000);
    epaper.setPanelSize(DISP_WIDTH, DISP_HEIGHT);
    pFramebuffer = epaper.currentBuffer();
    epaper.fillScreen(BBEP_WHITE);
    // epaper.fillScreen(15);
#if EPD_USE_4BPP_GRAY
    epaper.setMode(BB_MODE_4BPP);
#else
    epaper.clearWhite(); 
    epaper.setMode(BB_MODE_1BPP);
    epaper.setPasses(7, 5);
#endif

    lv_port_disp_init();

    ui_entry();
}

void idf_loop()
{
    lv_timer_handler();
    delay(1);
}

void setup()
{
    if (psramInit()) {
        Serial.println("\nThe PSRAM is correctly initialized");
    } else {
        Serial.println("\nPSRAM does not work");
    }

    idf_setup();
}

void loop()
{
    idf_loop();
}
