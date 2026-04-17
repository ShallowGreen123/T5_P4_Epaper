#include "factory_display.h"

#include <stdio.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <FastEPD.h>

#include "board_config.h"

namespace {

static const char *TAG = "factory_display";

constexpr int kDrawBufPixels = FACTORY_BOARD_WIDTH * FACTORY_DRAW_BUF_LINES;
constexpr int kFramebufferPitch = (FACTORY_BOARD_WIDTH + 7) / 8;

static FASTEPD s_epaper;
static lv_color_t *s_draw_buf_1 = nullptr;
static lv_color_t *s_draw_buf_2 = nullptr;
static uint8_t *s_framebuffer = nullptr;
static bool s_force_full_refresh = true;
static bool s_display_started = false;
static int s_pending_y_min = FACTORY_BOARD_HEIGHT;
static int s_pending_y_max = -1;
static i2c_master_bus_handle_t s_i2c_bus = nullptr;

static factory_display_mode_info_t s_mode_info = {
    .width = FACTORY_BOARD_WIDTH,
    .height = FACTORY_BOARD_HEIGHT,
    .partial_passes = FACTORY_EPD_PARTIAL_PASSES,
    .full_passes = FACTORY_EPD_FULL_PASSES,
    .mode_name = "1bpp monochrome",
    .mode_summary = "1bpp partial refresh (FastEPD + LVGL)",
};

static void begin_flush()
{
    s_pending_y_min = FACTORY_BOARD_HEIGHT;
    s_pending_y_max = -1;
}

static void note_flush_rows(int y1, int y2)
{
    if (y1 < s_pending_y_min) {
        s_pending_y_min = y1;
    }
    if (y2 > s_pending_y_max) {
        s_pending_y_max = y2;
    }
}

static void flush_to_panel()
{
    if (s_force_full_refresh || !s_display_started) {
        s_epaper.fullUpdate(CLEAR_SLOW, true);
        s_force_full_refresh = false;
        s_display_started = true;
    } else if (s_pending_y_min >= 0 && s_pending_y_max >= s_pending_y_min) {
        s_epaper.partialUpdate(true, s_pending_y_min, s_pending_y_max);
    }
    begin_flush();
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t clipped_x1 = LV_MAX(0, area->x1);
    const int32_t clipped_y1 = LV_MAX(0, area->y1);
    const int32_t clipped_x2 = LV_MIN(FACTORY_BOARD_WIDTH - 1, area->x2);
    const int32_t clipped_y2 = LV_MIN(FACTORY_BOARD_HEIGHT - 1, area->y2);

    if (clipped_x1 > clipped_x2 || clipped_y1 > clipped_y2) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    const int32_t src_width = area->x2 - area->x1 + 1;

    for (int32_t y = clipped_y1; y <= clipped_y2; ++y) {
        for (int32_t x = clipped_x1; x <= clipped_x2; ++x) {
            const int32_t src_x = x - area->x1;
            const int32_t src_y = y - area->y1;
            const lv_color_t pixel = color_p[(src_y * src_width) + src_x];
            const uint8_t red = LV_COLOR_GET_R(pixel);
            const uint8_t green = LV_COLOR_GET_G(pixel);
            const uint8_t blue = LV_COLOR_GET_B(pixel);
            const uint8_t red_8 = (uint8_t)((red << 3) | (red >> 2));
            const uint8_t green_8 = (uint8_t)((green << 2) | (green >> 4));
            const uint8_t blue_8 = (uint8_t)((blue << 3) | (blue >> 2));
            const uint16_t gray = (uint16_t)(((red_8 * 76) + (green_8 * 150) + (blue_8 * 30)) >> 8);
            const bool is_white = gray >= 128;
            uint8_t &dst = s_framebuffer[(y * kFramebufferPitch) + (x >> 3)];
            const uint8_t mask = (uint8_t)(0x80u >> (x & 0x07));

            if (is_white) {
                dst = (uint8_t)(dst | mask);
            } else {
                dst = (uint8_t)(dst & (uint8_t)(~mask));
            }
        }
    }

    note_flush_rows(clipped_y1, clipped_y2);
    if (lv_disp_flush_is_last(disp_drv)) {
        flush_to_panel();
    }
    lv_disp_flush_ready(disp_drv);
}

static bool init_lvgl()
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;

    lv_init();

    s_draw_buf_1 = (lv_color_t *)heap_caps_calloc(kDrawBufPixels, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_draw_buf_2 = (lv_color_t *)heap_caps_calloc(kDrawBufPixels, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_draw_buf_1 == nullptr || s_draw_buf_2 == nullptr) {
        ESP_LOGE(TAG, "failed to allocate LVGL draw buffers");
        return false;
    }

    lv_disp_draw_buf_init(&draw_buf, s_draw_buf_1, s_draw_buf_2, kDrawBufPixels);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = FACTORY_BOARD_WIDTH;
    disp_drv.ver_res = FACTORY_BOARD_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    return true;
}

static bool init_panel()
{
    if (s_i2c_bus == nullptr) {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = (gpio_num_t)FACTORY_TOUCH_I2C_SDA;
        bus_config.scl_io_num = (gpio_num_t)FACTORY_TOUCH_I2C_SCL;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

        esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    bbepSetI2CMasterBus(s_i2c_bus);

    if (s_epaper.initPanel(BB_PANEL_LILYGO_T5P4, FACTORY_PANEL_SPI_CLOCK_HZ) != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "initPanel failed");
        return false;
    }
    if (s_epaper.setMode(BB_MODE_1BPP) != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "setMode(BB_MODE_1BPP) failed");
        return false;
    }

    s_epaper.setPasses(FACTORY_EPD_PARTIAL_PASSES, FACTORY_EPD_FULL_PASSES);
    s_framebuffer = s_epaper.currentBuffer();
    if (s_framebuffer == nullptr) {
        ESP_LOGE(TAG, "currentBuffer returned null");
        return false;
    }

    s_epaper.fillScreen(BBEP_WHITE);
    begin_flush();
    return true;
}

}  // namespace

extern "C" bool factory_display_init(void)
{
    if (!init_panel()) {
        return false;
    }
    if (!init_lvgl()) {
        return false;
    }
    ESP_LOGI(TAG, "display ready: %ux%u %s", s_mode_info.width, s_mode_info.height, s_mode_info.mode_summary);
    return true;
}

extern "C" void factory_display_task_handler(void)
{
    lv_timer_handler();
}

extern "C" void factory_display_request_full_refresh(void)
{
    s_force_full_refresh = true;
    if (lv_scr_act() != nullptr) {
        lv_obj_invalidate(lv_scr_act());
    }
}

extern "C" const factory_display_mode_info_t *factory_display_get_mode_info(void)
{
    return &s_mode_info;
}

extern "C" i2c_master_bus_handle_t factory_display_get_i2c_bus(void)
{
    return s_i2c_bus;
}
