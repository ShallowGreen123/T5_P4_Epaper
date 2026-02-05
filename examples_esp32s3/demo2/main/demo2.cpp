/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "Arduino.h"
#include <FastEPD.h>
#include "smiley.h"
#include "AnimatedGIF.h"
#include "ft5536.h"
#include "Wire.h"
#include "lvgl.h"

#define DISP_WIDTH 1440
#define DISP_HEIGHT   720

#define DISP_BUF_SIZE (DISP_WIDTH * DISP_HEIGHT)
#define REFRESH_MODE_FAST 0
#define REFRESH_MODE_NORMAL 1
#define REFRESH_MODE_NEAT 2

LV_FONT_DECLARE(Font_Mono_Bold_30);

LV_IMG_DECLARE(img_test)

lv_obj_t *img;
lv_obj_t *label;
int cnt = 0;


FASTEPD epaper;


struct fts_ts_event events[FTS_MAX_POINTS_SUPPORT];

int i, j;
float f;

// int JPEGDraw(JPEGDRAW *pDraw)
// {
//   int x, y, iPitch = epaper.width()/2;
//   uint8_t *s, *d, *pBuffer = epaper.currentBuffer();
//   for (y=0; y<pDraw->iHeight; y++) {
//     d = &pBuffer[((pDraw->y + y)*iPitch) + (pDraw->x/2)];
//     s = (uint8_t *)pDraw->pPixels;
//     s += (y * pDraw->iWidth);
//     for (x=0; x<pDraw->iWidth; x+=2) {
//         *d++ = (s[0] & 0xf0) | (s[1] >> 4);
//         s += 2;
//     } // for x
//   } // for y
//   return 1;
// } /* JPEGDraw() */

uint8_t pack2_rgb565_to_4bpp(uint16_t p0, uint16_t p1) {
    // 提取 RGB565
    uint8_t r0 = (p0 >> 11) & 0x1F;
    uint8_t g0 = (p0 >> 5)  & 0x3F;
    uint8_t b0 =  p0        & 0x1F;

    uint8_t r1 = (p1 >> 11) & 0x1F;
    uint8_t g1 = (p1 >> 5)  & 0x3F;
    uint8_t b1 =  p1        & 0x1F;

    // 转 8-bit 灰度（近似）
    uint8_t gray0 = (r0*76 + g0*150 + b0*30) >> 8;
    uint8_t gray1 = (r1*76 + g1*150 + b1*30) >> 8;

    // 压到 4-bit
    uint8_t g4_0 = gray0 >> 4;
    uint8_t g4_1 = gray1 >> 4;

    // 两像素合成 1 字节
    return (g4_0 << 4) | g4_1;
}

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t w = lv_area_get_width(area)/2;
    uint16_t h = lv_area_get_height(area);
    uint16_t m = 0, n = 0;
    lv_color32_t *t32 = (lv_color32_t *)color_p;
    lv_color16_t *c16 = (lv_color16_t *)color_p;
    uint8_t *pBuffer = epaper.currentBuffer();

    Serial.printf("disp_flush w = %d, h = %d\n", w, h);

    int w2 = w * 2;
    for (int i = 0; i < h; i++)
    {
        for (int j = 0; j < w2 / 2; j++)
        {
            lv_color_t t = *(color_p + (i * w2) + j);
            *(color_p + (i * w2) + j) = *(color_p + (i * w2) + (w2 - j - 1));
            *(color_p + (i * w2) + (w2 - j - 1)) = t;
        }
    }

    int j = 0;
    for(int i = 0; i < (w * h); i++)
    {
        // uint8_t full = pack2_rgb565_to_4bpp(c16[j].full, c16[j + 1].full);
        // pBuffer[i] = full;
        // c16 += 2;

        //  // 提取 RGB565
        // uint8_t r0 = c16->ch.red;
        // uint8_t g0 = c16->ch.green;
        // uint8_t b0 = c16->ch.blue;

        // uint8_t r1 = (c16+1)->ch.red;
        // uint8_t g1 = (c16+1)->ch.green;
        // uint8_t b1 = (c16+1)->ch.blue;

        // // 转 8-bit 灰度（近似）
        // uint8_t gray0 = (r0*76 + g0*150 + b0*30) >> 8;
        // uint8_t gray1 = (r1*76 + g1*150 + b1*30) >> 8;

        // // 压到 4-bit
        // uint8_t g4_0 = gray0 >> 4;
        // uint8_t g4_1 = gray1 >> 4;

        // // 两像素合成 1 字节
        // pBuffer[i]  = (g4_0 << 4) | g4_1;
        // c16 += 2;

        lv_color8_t ret;
        LV_COLOR_SET_R8(ret, LV_COLOR_GET_R(*t32) >> 5); /*8 - 3  = 5*/
        LV_COLOR_SET_G8(ret, LV_COLOR_GET_G(*t32) >> 5); /*8 - 3  = 5*/
        LV_COLOR_SET_B8(ret, LV_COLOR_GET_B(*t32) >> 6); /*8 - 2  = 6*/
        pBuffer[i] = ret.full;
        t32++;
    }

    // for (int i = 0; i < (w * h); i++)
    // {
    //     lv_color8_t ret;
    //     LV_COLOR_SET_R8(ret, LV_COLOR_GET_R(*t32) >> 5); /*8 - 3  = 5*/
    //     LV_COLOR_SET_G8(ret, LV_COLOR_GET_G(*t32) >> 5); /*8 - 3  = 5*/
    //     LV_COLOR_SET_B8(ret, LV_COLOR_GET_B(*t32) >> 6); /*8 - 2  = 6*/
        
    //     if(m >= w) {
    //         m = 0;
    //         n++;
    //     }
    //     // epaper.drawPixel(m++, n, ret.full);

    //     pBuffer[i] = ret.full;
    //     t32++;
    // }

    // epaper.setRotation(0);
    epaper.fullUpdate(true, true);
    // epaper.setPasses(7);
    // epaper.partialUpdate(true);

    /* Inform the graphics library that you are ready with the flushing */
    lv_disp_flush_ready(disp);
}

void lv_port_disp_init(void)
{
    lv_init();

    static lv_disp_draw_buf_t draw_buf;

    lv_color_t *lv_disp_buf_1 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    lv_color_t *lv_disp_buf_2 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);

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

    // static lv_indev_drv_t indev_drv;
    // lv_indev_drv_init(&indev_drv);      /*Basic initialization*/
    // indev_drv.type = LV_INDEV_TYPE_POINTER;                 /*See below.*/
    // indev_drv.read_cb = my_input_read;              /*See below.*/
    // /*Register the driver in LVGL and save the created input device object*/
    // // static lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
    // lv_indev_drv_register(&indev_drv);
}

void ui_entry(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    disp->theme = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);

    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    img = lv_img_create(obj);
    lv_img_set_src(img, &img_test);
    lv_obj_center(img);

    label = lv_label_create(obj);
    lv_label_set_text_fmt(label, "%d", cnt);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -5);

    // lv_timer_create(img_switch_test_cb, 1500, NULL);
}


void setup(void)
{
    Serial.begin(115200);

    // Wire.begin(39, 40);

    epaper.initPanel(BB_PANEL_EPDIY_V7, 26666666);
    epaper.setPanelSize(DISP_WIDTH, DISP_HEIGHT);
    epaper.fillScreen(BBEP_WHITE);
    epaper.setMode(BB_MODE_4BPP);
    
    epaper.clearWhite(); // start with a white display (and buffer)

    // int id = ft5536_check_id();
    // Serial.printf("touch id=%d\n", id);

    // lv_init();
    lv_port_disp_init();

    ui_entry();

    delay(1000);
}

int tick = 0;

void loop()
{
    lv_timer_handler();
    delay(1);


    if(tick++ > 1000) {
        tick = 0;

        static int cnt = 0;
        lv_coord_t offsx = lv_rand(0, LV_HOR_RES - 120);
        lv_coord_t offsy = lv_rand(0, LV_VER_RES - 120);

        lv_obj_align(img, LV_ALIGN_TOP_LEFT, offsx, offsy);

        lv_label_set_text_fmt(label, "%d", ++cnt);

    }
}