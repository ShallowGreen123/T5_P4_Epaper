#include "factory_touch.h"

#include <stdio.h>

#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "lvgl.h"

#include "bsp/esp-bsp.h"
#include "board_config.h"
#include "factory_display.h"
#include "ui.h"

namespace {

static const char *TAG = "factory_touch";

static esp_lcd_panel_io_handle_t s_touch_io = nullptr;
static esp_lcd_touch_handle_t s_touch = nullptr;
static bool s_touch_ready = false;
static factory_touch_diag_state_t s_diag = {
    .ready = false,
    .pressed = false,
    .x = 0,
    .y = 0,
    .raw_x = 0,
    .raw_y = 0,
    .sample_count = 0,
    .status_text = "Touch unavailable",
};

static bool map_touch_coordinates(uint16_t raw_x, uint16_t raw_y, int16_t *mapped_x, int16_t *mapped_y)
{
    if (mapped_x == nullptr || mapped_y == nullptr) {
        return false;
    }

    const factory_display_mode_info_t *info = factory_display_get_mode_info();
    int32_t phys_x = FACTORY_BOARD_WIDTH - 1 - (int32_t)raw_y;
    int32_t phys_y = (int32_t)raw_x;
    int32_t lx = phys_x;
    int32_t ly = phys_y;

    switch (info->rotation_deg) {
        case 90:
            lx = phys_y;
            ly = FACTORY_BOARD_WIDTH - 1 - phys_x;
            break;
        case 180:
            lx = FACTORY_BOARD_WIDTH - 1 - phys_x;
            ly = FACTORY_BOARD_HEIGHT - 1 - phys_y;
            break;
        case 270:
            lx = FACTORY_BOARD_HEIGHT - 1 - phys_y;
            ly = phys_x;
            break;
        case 0:
        default:
            break;
    }

    if ((info->mirror_mode & 0x1U) != 0U) {
        lx = (int32_t)info->width - 1 - lx;
    }
    if ((info->mirror_mode & 0x2U) != 0U) {
        ly = (int32_t)info->height - 1 - ly;
    }

    if (lx < 0) {
        lx = 0;
    }
    if (ly < 0) {
        ly = 0;
    }
    if (lx >= info->width) {
        lx = info->width - 1;
    }
    if (ly >= info->height) {
        ly = info->height - 1;
    }

    *mapped_x = (int16_t)lx;
    *mapped_y = (int16_t)ly;
    return true;
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;

    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    uint8_t touched = 0;

    if (s_touch == nullptr) {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    esp_lcd_touch_read_data(s_touch);

    esp_lcd_touch_point_data_t touch_points[1] = {};
    if (esp_lcd_touch_get_data(s_touch, touch_points, &touched, 1) == ESP_OK && touched > 0) {
        int16_t mapped_x = 0;
        int16_t mapped_y = 0;
        map_touch_coordinates(touch_points[0].x, touch_points[0].y, &mapped_x, &mapped_y);

        last_x = (lv_coord_t)mapped_x;
        last_y = (lv_coord_t)mapped_y;
        s_diag.ready = true;
        s_diag.pressed = true;
        s_diag.x = mapped_x;
        s_diag.y = mapped_y;
        s_diag.raw_x = (int16_t)touch_points[0].x;
        s_diag.raw_y = (int16_t)touch_points[0].y;
        s_diag.sample_count++;
        s_diag.status_text = "Touch ready";
        factory_ui_notify_touch_activity();

        data->state = LV_INDEV_STATE_PR;
    } else {
        s_diag.pressed = false;
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

static void register_lvgl_touch()
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

}  // namespace

extern "C" bool factory_touch_init(void)
{
    s_touch_ready = false;
    s_diag.ready = false;
    s_diag.pressed = false;
    s_diag.x = 0;
    s_diag.y = 0;
    s_diag.raw_x = 0;
    s_diag.raw_y = 0;
    s_diag.sample_count = 0;
    s_diag.status_text = "Touch unavailable";

    if (s_touch != nullptr || s_touch_io != nullptr) {
        t5_board_touch_delete(s_touch, s_touch_io);
        s_touch = nullptr;
        s_touch_io = nullptr;
    }

    esp_err_t err = t5_board_touch_new(FACTORY_BOARD_WIDTH,
                                       FACTORY_BOARD_HEIGHT,
                                       &s_touch,
                                       &s_touch_io,
                                       nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_touch_ready = true;
    s_diag.ready = true;
    s_diag.status_text = "Touch ready";
    register_lvgl_touch();

    ESP_LOGI(TAG, "touch ready through t5_p4_board");
    return true;
}

extern "C" bool factory_touch_is_ready(void)
{
    return s_touch_ready;
}

extern "C" void factory_touch_get_diag_state(factory_touch_diag_state_t *state)
{
    if (state == nullptr) {
        return;
    }
    *state = s_diag;
}
