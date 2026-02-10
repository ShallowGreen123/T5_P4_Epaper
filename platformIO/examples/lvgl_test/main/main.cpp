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
#include "Roboto_Black_50.h"

#define DISP_WIDTH 1440
#define DISP_HEIGHT 720
#define DISP_BUF_SIZE (DISP_WIDTH * DISP_HEIGHT)

LV_FONT_DECLARE(Font_Mono_Bold_30);

LV_IMG_DECLARE(img_test)

// 1 = 4bpp grayscale (2 pixels per byte)
// 0 = 1bpp (1 bit per pixel)
#define EPD_USE_4BPP_GRAY 0

// 1 = enable ordered dithering (better gradients in 1bpp)
// 0 = simple threshold
#define EPD_ENABLE_DITHER 0

// Mirror controls for LVGL flush
// EPD_MIRROR_MODE: 0 = normal, 1 = mirror X, 2 = mirror Y, 3 = mirror X+Y (180 deg)
#define EPD_MIRROR_MODE 1

FASTEPD epaper;
uint8_t *decodebuffer = NULL;
uint8_t *pFramebuffer;
// LVGL
extern void ui_entry(void);

// static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
// {
//     uint16_t w = lv_area_get_width(area) / 2;
//     uint16_t h = lv_area_get_height(area);
//     uint8_t *pBuffer = epaper.currentBuffer();

//     Serial.printf("disp_flush w = %d, h = %d\n", w, h);

// #if 1   // Mirror screen or not
//     int w2 = w * 2;
//     for (int i = 0; i < h; i++)
//     {
//         for (int j = 0; j < w2 / 2; j++)
//         {
//             lv_color_t t = *(color_p + (i * w2) + j);
//             *(color_p + (i * w2) + j) = *(color_p + (i * w2) + (w2 - j - 1));
//             *(color_p + (i * w2) + (w2 - j - 1)) = t;
//         }
//     }
// #endif

//     // lv_color32_t *t32 = (lv_color32_t *)color_p;
//     // for (int i = 0; i < (w * h); i++)
//     // {
//     //     // 32bpp -> 8bpp (R3G3B2)
//     //     lv_color8_t ret;
//     //     LV_COLOR_SET_R8(ret, LV_COLOR_GET_R(*t32) >> 5); /*8 - 3  = 5*/
//     //     LV_COLOR_SET_G8(ret, LV_COLOR_GET_G(*t32) >> 5); /*8 - 3  = 5*/
//     //     LV_COLOR_SET_B8(ret, LV_COLOR_GET_B(*t32) >> 6); /*8 - 2  = 6*/
//     //     pBuffer[i] = ret.full;
//     //     t32++;
//     // }

//     lv_color16_t *c16 = (lv_color16_t *)color_p;
//     int j = 0;
//     for (int i = 0; i < (w*2 * h); i+=2)
//     {
//         // 提取 RGB565
//         uint8_t r0 = c16[i].ch.red;
//         uint8_t g0 = c16[i].ch.green;
//         uint8_t b0 = c16[i].ch.blue;
//         uint8_t r1 = c16[i + 1].ch.red;
//         uint8_t g1 = c16[i + 1].ch.green;
//         uint8_t b1 = c16[i + 1].ch.blue;

//         // 5/6-bit -> 8-bit 扩展
//         uint8_t r08 = (r0 << 3) | (r0 >> 2);
//         uint8_t g08 = (g0 << 2) | (g0 >> 4);
//         uint8_t b08 = (b0 << 3) | (b0 >> 2);
//         uint8_t r18 = (r1 << 3) | (r1 >> 2);
//         uint8_t g18 = (g1 << 2) | (g1 >> 4);
//         uint8_t b18 = (b1 << 3) | (b1 >> 2);

//         // 8-bit 扩展 -> 8-bit 灰度
//         uint8_t gray0 = (r08 * 76 + g08 * 150 + b08 * 30) >> 8;
//         uint8_t gray1 = (r18 * 76 + g18 * 150 + b18 * 30) >> 8;

//         // 8-bit 灰度 -> 4-bit 灰度
//         uint8_t g4_0 = gray0 >> 4;
//         uint8_t g4_1 = gray1 >> 4;

//         pBuffer[j++] = (g4_0 << 4) | g4_1;
//     }

//     // epaper.setRotation(0);
//     epaper.fullUpdate(true, true);
//     // epaper.setPasses(7);
//     // epaper.partialUpdate(true);

//     /* Inform the graphics library that you are ready with the flushing */
//     lv_disp_flush_ready(disp);
// }

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

            int32_t dst_x = x1 + x;
            int32_t dst_y = y1 + y;
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
    epaper.fullUpdate(true, true);
#else
    epaper.partialUpdate(true, y1, y2);
#endif
    // epaper.einkPower(true);
    // or partial update: epaper.partialUpdate(true, y1, y2);

    lv_disp_flush_ready(disp);
}

void lv_port_disp_init(void)
{
    lv_init();

    static lv_disp_draw_buf_t draw_buf;

    lv_color_t *lv_disp_buf_1 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    lv_color_t *lv_disp_buf_2 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), DISP_BUF_SIZE);
    Serial.printf("epaper w = %d, h = %d\n", epaper.width(), epaper.height());

    // decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), DISP_BUF_SIZE);
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
}
int tick = 0;
BB_RECT rect; // rectangle for getting the text size
void idf_setup()
{
    epaper.initPanel(BB_PANEL_EPDIY_V7, 26666666);
    epaper.setPanelSize(DISP_WIDTH, DISP_HEIGHT);
    pFramebuffer = epaper.currentBuffer();
    epaper.fillScreen(BBEP_WHITE);
    // epaper.fillScreen(15);
#if EPD_USE_4BPP_GRAY
    epaper.setMode(BB_MODE_4BPP);
    epaper.fillRect(600, 100, 200, 200, 0x8);
    epaper.setFont(FONT_12x16);
    epaper.setTextColor(0xe);
    epaper.drawString("4-bpp", 670, 160);
    epaper.drawString("Regional Update", 610, 192);
    rect.x = 600;
    rect.y = 100;
    rect.h = rect.w = 200;
    epaper.fullUpdate(true, false, &rect); // update only the rectangle
    delay(3000);

#else
    epaper.setMode(BB_MODE_1BPP);
    epaper.setPasses(7, 5);
    // epaper.setPasses(7);
    epaper.clearWhite(); 
    // delay(1000);
#endif
    
    
    // start with a white display (and buffer)

    lv_port_disp_init();

    ui_entry();
}


void idf_loop()
{
    // lv_task_handler();
    lv_timer_handler();
    delay(1);

    if (tick++ > 1000)
    {
        tick = 0;

        // epaper.fillRect(600, 100, 200, 200, 0x8);
        // epaper.setFont(FONT_12x16);
        // epaper.setTextColor(0x1);
        // epaper.drawString("4-bpp", 670, 160);
        // epaper.drawString("Regional Update", 610, 192);
        // rect.x = 600;
        // rect.y = 100;
        // rect.h = rect.w = 200;
        // epaper.fullUpdate(true, false, &rect);
    }
}
