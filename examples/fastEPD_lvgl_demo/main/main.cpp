/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include <esp_heap_caps.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "lvgl.h"
#include <FastEPD.h>
#include "ui.h"
#include "scr_mrg.h"
#include "TouchDrvGT911.hpp"
#include "ExtensionIOXL9555.hpp" // expander used for pin control on some boards

static const char *TAG = "fastEPD_lvgl";

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

static bool g_epd_use_4bpp_gray = true;
static bool g_epd_enable_dither = true;
static uint8_t g_epd_mirror_mode = 0;
static uint16_t g_epd_rotation_deg = 0;
static bool g_epd_4bpp_low_flash = true;
static int g_epd_1bpp_partial_passes = 7;
static int g_epd_1bpp_full_passes = 5;
static bool g_first_4bpp_refresh = true;

// --- GT911 touch globals --------------------------------------------------
TouchDrvGT911 touch;
int16_t gt_x[5], gt_y[5];

ExtensionIOXL9555 io;

static constexpr uint32_t EXTIO_PIN_BASE = 0x1000;
static constexpr uint32_t EXTIO_PIN(uint32_t pin) { return EXTIO_PIN_BASE + pin; }

static void idf_gpio_mode(uint32_t pin, uint8_t mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void gpioWrite(uint32_t pin, uint8_t value)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        io.digitalWrite((uint8_t)(pin - EXTIO_PIN_BASE), value);
        return;
    }
    gpio_set_level((gpio_num_t)pin, value);
}

static int gpioRead(uint32_t pin)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        return io.digitalRead((uint8_t)(pin - EXTIO_PIN_BASE));
    }
    return gpio_get_level((gpio_num_t)pin);
}

static void gpioMode(uint32_t pin, uint8_t mode)
{
    if (pin >= EXTIO_PIN_BASE && pin < (EXTIO_PIN_BASE + 16)) {
        io.pinMode((uint8_t)(pin - EXTIO_PIN_BASE), mode);
        return;
    }
    idf_gpio_mode(pin, mode);
}

// --------------------------------------------------------------------------



FASTEPD epaper;
uint8_t *decodebuffer = NULL;
uint8_t *pFramebuffer;

static uint16_t epd_norm_rotation(uint16_t rotation)
{
    rotation %= 360;
    if (rotation % 90 != 0)
    {
        return 0;
    }
    return rotation;
}

void ui_adjust_set_rotation(uint16_t rotation)
{
    g_epd_rotation_deg = epd_norm_rotation(rotation);
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->driver)
    {
        lv_coord_t hor_res = ((g_epd_rotation_deg == 90) || (g_epd_rotation_deg == 270)) ? DISP_HEIGHT : DISP_WIDTH;
        lv_coord_t ver_res = ((g_epd_rotation_deg == 90) || (g_epd_rotation_deg == 270)) ? DISP_WIDTH : DISP_HEIGHT;
        if ((disp->driver->hor_res != hor_res) || (disp->driver->ver_res != ver_res))
        {
            disp->driver->hor_res = hor_res;
            disp->driver->ver_res = ver_res;
            disp->driver->rotated = LV_DISP_ROT_NONE;

            ESP_LOGI(TAG,
                     "rotation=%u hor_res=%d ver_res=%d LV_HOR_RES=%d LV_VER_RES=%d",
                     g_epd_rotation_deg,
                     disp->driver->hor_res,
                     disp->driver->ver_res,
                     LV_HOR_RES,
                     LV_VER_RES);

            lv_disp_drv_update(disp, disp->driver);
        }
    }
}

uint16_t ui_adjust_get_rotation(void)
{
    return g_epd_rotation_deg;
}

void ui_adjust_set_mirror(uint8_t mirror_mode)
{
    g_epd_mirror_mode = mirror_mode & 0x3;
}

uint8_t ui_adjust_get_mirror(void)
{
    return g_epd_mirror_mode;
}

void ui_adjust_set_passes(int partial_passes, int full_passes)
{
    if (partial_passes < 1) partial_passes = 1;
    if (partial_passes > 15) partial_passes = 15;
    if (full_passes < 1) full_passes = 1;
    if (full_passes > 15) full_passes = 15;

    g_epd_1bpp_partial_passes = partial_passes;
    g_epd_1bpp_full_passes = full_passes;
    if (!g_epd_use_4bpp_gray)
    {
        epaper.setPasses(g_epd_1bpp_partial_passes, g_epd_1bpp_full_passes);
    }
}

int ui_adjust_get_partial_passes(void)
{
    return g_epd_1bpp_partial_passes;
}

int ui_adjust_get_full_passes(void)
{
    return g_epd_1bpp_full_passes;
}

void ui_adjust_set_enable_dither(bool enable)
{
    g_epd_enable_dither = enable;
}

bool ui_adjust_get_enable_dither(void)
{
    return g_epd_enable_dither;
}

void ui_adjust_set_low_flash(bool enable)
{
    g_epd_4bpp_low_flash = enable;
    g_first_4bpp_refresh = true;
}

bool ui_adjust_get_low_flash(void)
{
    return g_epd_4bpp_low_flash;
}

void ui_adjust_set_color_mode(int mode_4bpp)
{
    const bool use_4bpp = (mode_4bpp != 0);
    if (g_epd_use_4bpp_gray == use_4bpp)
    {
        return;
    }

    g_epd_use_4bpp_gray = use_4bpp;
    if (g_epd_use_4bpp_gray)
    {
        epaper.setMode(BB_MODE_4BPP);
        epaper.fillScreen(BBEP_WHITE);
        g_first_4bpp_refresh = true;
    }
    else
    {
        epaper.clearWhite();
        epaper.setMode(BB_MODE_1BPP);
        epaper.setPasses(g_epd_1bpp_partial_passes, g_epd_1bpp_full_passes);
    }
    pFramebuffer = epaper.currentBuffer();
}

int ui_adjust_get_color_mode(void)
{
    return g_epd_use_4bpp_gray ? 1 : 0;
}

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t x1 = area->x1;
    const int32_t y1 = area->y1;
    const int32_t x2 = area->x2;
    const int32_t y2 = area->y2;

    const int32_t w = x2 - x1 + 1;
    const int32_t h = y2 - y1 + 1;

    const int32_t pitch = g_epd_use_4bpp_gray ? (DISP_WIDTH / 2) : ((DISP_WIDTH + 7) / 8);
    static const uint8_t bayer4[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };

    for (int32_t y = 0; y < h; y++)
    {
        for (int32_t x = 0; x < w; x++)
        {
            lv_color_t c = color_p[y * w + x];

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
            if (g_epd_mirror_mode & 0x1)
            {
                logical_x = (disp->hor_res - 1) - logical_x;
            }
            if (g_epd_mirror_mode & 0x2)
            {
                logical_y = (disp->ver_res - 1) - logical_y;
            }

            int32_t dst_x = logical_x;
            int32_t dst_y = logical_y;

            if (g_epd_rotation_deg == 90)
            {
                dst_x = (DISP_WIDTH - 1) - logical_y;
                dst_y = logical_x;
            }
            else if (g_epd_rotation_deg == 180)
            {
                dst_x = (DISP_WIDTH - 1) - logical_x;
                dst_y = (DISP_HEIGHT - 1) - logical_y;
            }
            else if (g_epd_rotation_deg == 270)
            {
                dst_x = logical_y;
                dst_y = (DISP_HEIGHT - 1) - logical_x;
            }

            if ((dst_x < 0) || (dst_x >= DISP_WIDTH) || (dst_y < 0) || (dst_y >= DISP_HEIGHT))
            {
                continue;
            }

            if (g_epd_use_4bpp_gray)
            {
                uint8_t g4 = gray >> 4;
                int32_t idx = dst_y * pitch + (dst_x >> 1);
                if ((dst_x & 1) == 0)
                {
                    pFramebuffer[idx] = (pFramebuffer[idx] & 0x0F) | (g4 << 4);
                }
                else
                {
                    pFramebuffer[idx] = (pFramebuffer[idx] & 0xF0) | g4;
                }
            }
            else
            {
                int32_t idx = dst_y * pitch + (dst_x >> 3);
                uint8_t mask = 0x80 >> (dst_x & 7);
                bool is_white = false;
                if (g_epd_enable_dither)
                {
                    uint8_t threshold = (uint8_t)(bayer4[dst_y & 3][dst_x & 3] * 16);
                    is_white = (gray >= threshold);
                }
                else
                {
                    is_white = (gray >= 128);
                }

                if (is_white)
                {
                    pFramebuffer[idx] |= mask;
                }
                else
                {
                    pFramebuffer[idx] &= (uint8_t)~mask;
                }
            }
        }
    }

    if (g_epd_use_4bpp_gray)
    {
        if (lv_disp_flush_is_last(disp))
        {
            if (g_epd_4bpp_low_flash)
            {
                if (g_first_4bpp_refresh)
                {
                    epaper.fullUpdate(CLEAR_SLOW, true);
                    g_first_4bpp_refresh = false;
                }
                else
                {
                    epaper.fullUpdate(CLEAR_NONE, true);
                }
            }
            else
            {
                epaper.fullUpdate(CLEAR_SLOW, true);
            }
        }
    }
    else
    {
        int32_t corners_x[4] = {x1, x1, x2, x2};
        int32_t corners_y[4] = {y1, y2, y1, y2};
        int32_t min_py = DISP_HEIGHT - 1;
        int32_t max_py = 0;

        for (int i = 0; i < 4; i++)
        {
            int32_t lx = corners_x[i];
            int32_t ly = corners_y[i];
            if (g_epd_mirror_mode & 0x1)
            {
                lx = (disp->hor_res - 1) - lx;
            }
            if (g_epd_mirror_mode & 0x2)
            {
                ly = (disp->ver_res - 1) - ly;
            }

            int32_t px = lx;
            int32_t py = ly;
            if (g_epd_rotation_deg == 90)
            {
                px = (DISP_WIDTH - 1) - ly;
                py = lx;
            }
            else if (g_epd_rotation_deg == 180)
            {
                px = (DISP_WIDTH - 1) - lx;
                py = (DISP_HEIGHT - 1) - ly;
            }
            else if (g_epd_rotation_deg == 270)
            {
                px = ly;
                py = (DISP_HEIGHT - 1) - lx;
            }

            if (py < min_py) min_py = py;
            if (py > max_py) max_py = py;
        }

        if (min_py < 0) min_py = 0;
        if (max_py >= DISP_HEIGHT) max_py = DISP_HEIGHT - 1;
        epaper.partialUpdate(true, min_py, max_py);
    }

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

        int32_t phys_x = (DISP_WIDTH - 1) - (int32_t)touch_y;
        int32_t phys_y = (int32_t)touch_x;

        int32_t lx = phys_x;
        int32_t ly = phys_y;
        if (g_epd_rotation_deg == 90)
        {
            lx = phys_y;
            ly = (DISP_WIDTH - 1) - phys_x;
        }
        else if (g_epd_rotation_deg == 180)
        {
            lx = (DISP_WIDTH - 1) - phys_x;
            ly = (DISP_HEIGHT - 1) - phys_y;
        }
        else if (g_epd_rotation_deg == 270)
        {
            lx = (DISP_HEIGHT - 1) - phys_y;
            ly = phys_x;
        }

        lv_disp_t *disp = lv_disp_get_default();
        lv_coord_t hor_res = DISP_WIDTH;
        lv_coord_t ver_res = DISP_HEIGHT;
        if (disp)
        {
            hor_res = lv_disp_get_hor_res(disp);
            ver_res = lv_disp_get_ver_res(disp);
        }

        if (g_epd_mirror_mode & 0x1)
        {
            lx = (hor_res - 1) - lx;
        }
        if (g_epd_mirror_mode & 0x2)
        {
            ly = (ver_res - 1) - ly;
        }

        if (lx < 0) lx = 0;
        if (ly < 0) ly = 0;
        if (lx >= hor_res) lx = hor_res - 1;
        if (ly >= ver_res) ly = ver_res - 1;

        last_x = (lv_coord_t)lx;
        last_y = (lv_coord_t)ly;

        data->state = LV_INDEV_STATE_PR;
        ESP_LOGD(TAG, "touch pressed: %d, %d (raw: %d, %d)", last_x, last_y, touch_x, touch_y);
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

    lv_color_t *lv_disp_buf_1 = (lv_color_t *)heap_caps_calloc(DISP_BUF_SIZE, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *lv_disp_buf_2 = (lv_color_t *)heap_caps_calloc(DISP_BUF_SIZE, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lv_disp_buf_1 || !lv_disp_buf_2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        abort();
    }
    ESP_LOGI(TAG, "epaper w = %d, h = %d", epaper.width(), epaper.height());

    lv_disp_draw_buf_init(&draw_buf, lv_disp_buf_1, lv_disp_buf_2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_WIDTH;
    disp_drv.ver_res = DISP_HEIGHT;
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
    // initialize IO expander so we can control reset/irq lines if wired that way
    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;
    if (io.begin((i2c_port_t)I2C_NUM_0, chip_address, I2C_SDA_PIN, I2C_SCL_PIN)) {
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
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else {
        while (1) {
            ESP_LOGE(TAG, "Failed to find XL9555 - check your wiring!");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    io.pinMode(BOARD_PCA_14_VCOM_CTRL, INPUT);
    io.pinMode(BOARD_PCA_15_TPS_WAKEUP, INPUT);

    touch.setPins((int)EXTIO_PIN(BOARD_PCA_00_T_RST), SENSOR_IRQ);
    touch.setGpioCallback(gpioMode, gpioWrite, gpioRead);
    if (!touch.begin((i2c_port_t)I2C_NUM_0, GT911_SLAVE_ADDRESS_L, SENSOR_SDA, SENSOR_SCL)) {
        while (1) {
            ESP_LOGE(TAG, "Failed to find GT911 - check your wiring!");
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (touch.begin((i2c_port_t)I2C_NUM_0, GT911_SLAVE_ADDRESS_L, SENSOR_SDA, SENSOR_SCL)) {
                ESP_LOGI(TAG, "GT911 found on retry!");
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Init GT911 Sensor success!");
    // Prefer level-triggered mode for reliable polling/readout in GUI loops.
    // (Edge-triggered modes generate short pulses which can be missed.)
    touch.setInterruptMode(LOW_LEVEL_QUERY);

    epaper.initPanel(BB_PANEL_LILYGO_T5P4, 40000000);
    epaper.setPanelSize(DISP_WIDTH, DISP_HEIGHT);
    pFramebuffer = epaper.currentBuffer();
    epaper.fillScreen(BBEP_WHITE);
    // epaper.fillScreen(15);
    if (g_epd_use_4bpp_gray)
    {
        epaper.setMode(BB_MODE_4BPP);
    }
    else
    {
        epaper.clearWhite();
        epaper.setMode(BB_MODE_1BPP);
        epaper.setPasses(g_epd_1bpp_partial_passes, g_epd_1bpp_full_passes);
    }

    lv_port_disp_init();

    ui_entry();
}

void idf_loop()
{
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(1));
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Free SPIRAM before setup: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    idf_setup();

    while (true) {
        idf_loop();
    }
}
