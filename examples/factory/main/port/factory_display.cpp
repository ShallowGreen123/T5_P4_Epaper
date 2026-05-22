#include "factory_display.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#include <FastEPD.h>

#include "bsp/esp-bsp.h"
#include "board_config.h"

namespace {

static const char *TAG = "factory_display";

constexpr int kDrawBufPixels = FACTORY_BOARD_WIDTH * FACTORY_DRAW_BUF_LINES;
constexpr int kFramebufferPitch1bpp = (FACTORY_BOARD_WIDTH + 7) / 8;
constexpr int kFramebufferPitch4bpp = FACTORY_BOARD_WIDTH / 2;
constexpr size_t kFramebufferBytes1bpp = (size_t)kFramebufferPitch1bpp * FACTORY_BOARD_HEIGHT;
constexpr uint8_t kDisableMaxPartialRefreshesBeforeFull = 0;
constexpr uint8_t kMinMaxPartialRefreshesBeforeFull = 5;
constexpr uint8_t kMaxMaxPartialRefreshesBeforeFull = 30;
constexpr uint8_t kMaxPartialRefreshesStep = 5;
// In runtime, prefer a low-flash whole-screen rewrite after a modest number of
// partial updates. This follows the fastEPD_lvgl_demo pattern: the first
// refresh performs a deep clear, later full-screen refreshes rewrite the panel
// without an extra black/white clear cycle so we can suppress ghosting from
// earlier partial updates without turning every maintenance refresh into a
// visible flash.
constexpr uint8_t kDefaultMaxPartialRefreshesBeforeFull = kDisableMaxPartialRefreshesBeforeFull;
constexpr int kInitialFullRefreshMode = CLEAR_SLOW;
constexpr int kRuntimeFullRefreshMode = CLEAR_SLOW;
constexpr uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static FASTEPD s_epaper;
static lv_color_t *s_draw_buf_1 = nullptr;
static lv_color_t *s_draw_buf_2 = nullptr;
static uint8_t *s_framebuffer = nullptr;
static uint8_t *s_full_refresh_backup_1bpp = nullptr;
static bool s_use_4bpp_color = false;
static bool s_enable_dither = false;
static bool s_low_flash = true;
static bool s_first_4bpp_refresh = true;
static bool s_force_full_refresh = true;
static bool s_force_clean_full_refresh = false;
static bool s_display_started = false;
static int s_pending_y_min = FACTORY_BOARD_HEIGHT;
static int s_pending_y_max = -1;
static uint32_t s_partial_refresh_count = 0;
static uint8_t s_max_partial_refreshes_before_full = kDefaultMaxPartialRefreshesBeforeFull;
static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static lv_disp_drv_t s_disp_drv;
static lv_disp_t *s_disp = nullptr;
static char s_mode_summary[96] = "1bpp partial refresh";

static factory_display_mode_info_t s_mode_info = {
    .width = FACTORY_BOARD_WIDTH,
    .height = FACTORY_BOARD_HEIGHT,
    .native_width = FACTORY_BOARD_WIDTH,
    .native_height = FACTORY_BOARD_HEIGHT,
    .rotation_deg = 90,
    .mirror_mode = 0,
    .partial_passes = FACTORY_EPD_PARTIAL_PASSES,
    .full_passes = FACTORY_EPD_FULL_PASSES,
    .mode_name = "1bpp monochrome",
    .mode_summary = s_mode_summary,
};

static uint16_t normalize_rotation(uint16_t rotation_deg)
{
    rotation_deg = (uint16_t)(rotation_deg % 360U);
    if ((rotation_deg % 90U) != 0U) {
        return 0;
    }
    return rotation_deg;
}

static uint8_t normalize_max_partial_refreshes_before_full(uint8_t count)
{
    if (count == kDisableMaxPartialRefreshesBeforeFull) {
        return kDisableMaxPartialRefreshesBeforeFull;
    }

    if (count < kMinMaxPartialRefreshesBeforeFull) {
        count = kMinMaxPartialRefreshesBeforeFull;
    }
    if (count > kMaxMaxPartialRefreshesBeforeFull) {
        count = kMaxMaxPartialRefreshesBeforeFull;
    }

    count = (uint8_t)(((count + (kMaxPartialRefreshesStep / 2U)) / kMaxPartialRefreshesStep) * kMaxPartialRefreshesStep);
    if (count < kMinMaxPartialRefreshesBeforeFull) {
        count = kMinMaxPartialRefreshesBeforeFull;
    }
    if (count > kMaxMaxPartialRefreshesBeforeFull) {
        count = kMaxMaxPartialRefreshesBeforeFull;
    }
    return count;
}

static int framebuffer_pitch()
{
    return s_use_4bpp_color ? kFramebufferPitch4bpp : kFramebufferPitch1bpp;
}

static void *alloc_display_buffer(size_t size)
{
    void *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return buffer;
}

static bool ensure_full_refresh_backup_1bpp()
{
    if (s_full_refresh_backup_1bpp != nullptr) {
        return true;
    }

    s_full_refresh_backup_1bpp = static_cast<uint8_t *>(alloc_display_buffer(kFramebufferBytes1bpp));
    if (s_full_refresh_backup_1bpp == nullptr) {
        ESP_LOGE(TAG, "failed to allocate 1bpp full-refresh backup buffer");
        return false;
    }
    return true;
}

static bool run_monochrome_recondition_full_refresh(bool deep_clean)
{
    if (s_framebuffer == nullptr || !ensure_full_refresh_backup_1bpp()) {
        return false;
    }

    memcpy(s_full_refresh_backup_1bpp, s_framebuffer, kFramebufferBytes1bpp);

    if (deep_clean) {
        if (s_epaper.clearBlack(true) != BBEP_SUCCESS) {
            s_framebuffer = s_epaper.currentBuffer();
            if (s_framebuffer != nullptr) {
                memcpy(s_framebuffer, s_full_refresh_backup_1bpp, kFramebufferBytes1bpp);
            }
            ESP_LOGW(TAG, "clearBlack failed during 1bpp deep clean refresh");
            return false;
        }

        s_framebuffer = s_epaper.currentBuffer();
        if (s_framebuffer == nullptr) {
            ESP_LOGE(TAG, "currentBuffer returned null after 1bpp clearBlack");
            return false;
        }
    }

    if (s_epaper.clearWhite(true) != BBEP_SUCCESS) {
        memcpy(s_framebuffer, s_full_refresh_backup_1bpp, kFramebufferBytes1bpp);
        ESP_LOGW(TAG, "clearWhite failed during 1bpp recondition refresh");
        return false;
    }

    s_framebuffer = s_epaper.currentBuffer();
    if (s_framebuffer == nullptr) {
        ESP_LOGE(TAG, "currentBuffer returned null after 1bpp clearWhite");
        return false;
    }

    memcpy(s_framebuffer, s_full_refresh_backup_1bpp, kFramebufferBytes1bpp);
    if (s_epaper.fullUpdate(CLEAR_NONE, true) != BBEP_SUCCESS) {
        ESP_LOGW(TAG, "1bpp redraw after white recondition failed");
        return false;
    }

    return true;
}

static void update_mode_summary()
{
    const char *mirror_text = "normal";
    const char *dither_text = s_enable_dither ? "dth" : "mono";
    const char *flash_text = s_low_flash ? "lowflash" : "fullflash";
    switch (s_mode_info.mirror_mode & 0x3U) {
        case 1:
            mirror_text = "mirror-x";
            break;
        case 2:
            mirror_text = "mirror-y";
            break;
        case 3:
            mirror_text = "mirror-xy";
            break;
        default:
            break;
    }

    s_mode_info.width = (s_mode_info.rotation_deg == 90U || s_mode_info.rotation_deg == 270U)
                            ? FACTORY_BOARD_HEIGHT
                            : FACTORY_BOARD_WIDTH;
    s_mode_info.height = (s_mode_info.rotation_deg == 90U || s_mode_info.rotation_deg == 270U)
                             ? FACTORY_BOARD_WIDTH
                             : FACTORY_BOARD_HEIGHT;
    s_mode_info.mode_name = s_use_4bpp_color ? "4bpp grayscale" : "1bpp monochrome";

    if (s_use_4bpp_color) {
        snprintf(
            s_mode_summary,
            sizeof(s_mode_summary),
            "4bpp grayscale | rot %u | %s | %s",
            s_mode_info.rotation_deg,
            mirror_text,
            flash_text);
    } else {
        char auto_text[16] = {};
        if (s_max_partial_refreshes_before_full == kDisableMaxPartialRefreshesBeforeFull) {
            snprintf(auto_text, sizeof(auto_text), "off");
        } else {
            snprintf(auto_text, sizeof(auto_text), "%u", (unsigned)s_max_partial_refreshes_before_full);
        }

        snprintf(
            s_mode_summary,
            sizeof(s_mode_summary),
            "1bpp partial refresh | rot %u | %s | %s | p%u/f%u | auto %s",
            s_mode_info.rotation_deg,
            mirror_text,
            dither_text,
            s_mode_info.partial_passes,
            s_mode_info.full_passes,
            auto_text);
    }
}

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
    if (s_use_4bpp_color) {
        int clear_mode = CLEAR_NONE;
        if (s_force_full_refresh) {
            clear_mode = CLEAR_SLOW;
        } else if (!s_low_flash) {
            clear_mode = CLEAR_SLOW;
        } else if (s_first_4bpp_refresh) {
            clear_mode = CLEAR_SLOW;
        }

        s_epaper.fullUpdate(clear_mode, true);
        s_force_full_refresh = false;
        s_force_clean_full_refresh = false;
        s_display_started = true;
        s_first_4bpp_refresh = false;
        s_partial_refresh_count = 0;
        begin_flush();
        return;
    }

    const bool auto_full_refresh =
        s_max_partial_refreshes_before_full != kDisableMaxPartialRefreshesBeforeFull &&
        s_partial_refresh_count >= s_max_partial_refreshes_before_full;

    if (s_force_full_refresh || !s_display_started || auto_full_refresh) {
        const bool clean_full_refresh = s_force_clean_full_refresh;
        s_force_clean_full_refresh = false;
        if (!run_monochrome_recondition_full_refresh(clean_full_refresh)) {
            const int clear_mode = s_display_started ? kRuntimeFullRefreshMode : kInitialFullRefreshMode;
            s_epaper.fullUpdate(clear_mode, true);
        }
        s_force_full_refresh = false;
        s_display_started = true;
        s_partial_refresh_count = 0;
    } else if (s_pending_y_min >= 0 && s_pending_y_max >= s_pending_y_min) {
        s_epaper.partialUpdate(true, s_pending_y_min, s_pending_y_max);
        s_partial_refresh_count++;
    }
    begin_flush();
}

static bool apply_color_mode(bool use_4bpp)
{
    if (use_4bpp) {
        if (s_epaper.setMode(BB_MODE_4BPP) != BBEP_SUCCESS) {
            ESP_LOGE(TAG, "setMode(BB_MODE_4BPP) failed");
            return false;
        }
        s_use_4bpp_color = true;
        s_framebuffer = s_epaper.currentBuffer();
        if (s_framebuffer == nullptr) {
            ESP_LOGE(TAG, "currentBuffer returned null in 4bpp mode");
            return false;
        }
        s_epaper.fillScreen(BBEP_WHITE);
        s_first_4bpp_refresh = true;
    } else {
        if (s_epaper.clearWhite() != BBEP_SUCCESS) {
            ESP_LOGW(TAG, "clearWhite before switching to 1bpp failed");
        }
        if (s_epaper.setMode(BB_MODE_1BPP) != BBEP_SUCCESS) {
            ESP_LOGE(TAG, "setMode(BB_MODE_1BPP) failed");
            return false;
        }
        s_use_4bpp_color = false;
        s_epaper.setPasses(s_mode_info.partial_passes, s_mode_info.full_passes);
        s_framebuffer = s_epaper.currentBuffer();
        if (s_framebuffer == nullptr) {
            ESP_LOGE(TAG, "currentBuffer returned null in 1bpp mode");
            return false;
        }
        s_epaper.fillScreen(BBEP_WHITE);
    }

    s_force_full_refresh = true;
    s_display_started = false;
    s_partial_refresh_count = 0;
    begin_flush();
    update_mode_summary();
    return true;
}

static void transform_logical_to_panel(int32_t logical_x, int32_t logical_y, int32_t *panel_x, int32_t *panel_y)
{
    int32_t lx = logical_x;
    int32_t ly = logical_y;

    if ((s_mode_info.mirror_mode & 0x1U) != 0U) {
        lx = (int32_t)s_mode_info.width - 1 - lx;
    }
    if ((s_mode_info.mirror_mode & 0x2U) != 0U) {
        ly = (int32_t)s_mode_info.height - 1 - ly;
    }

    switch (s_mode_info.rotation_deg) {
        case 90:
            *panel_x = FACTORY_BOARD_WIDTH - 1 - ly;
            *panel_y = lx;
            break;
        case 180:
            *panel_x = FACTORY_BOARD_WIDTH - 1 - lx;
            *panel_y = FACTORY_BOARD_HEIGHT - 1 - ly;
            break;
        case 270:
            *panel_x = ly;
            *panel_y = FACTORY_BOARD_HEIGHT - 1 - lx;
            break;
        case 0:
        default:
            *panel_x = lx;
            *panel_y = ly;
            break;
    }
}

static void note_transformed_rows(const lv_area_t *area)
{
    const int32_t corners_x[4] = {area->x1, area->x1, area->x2, area->x2};
    const int32_t corners_y[4] = {area->y1, area->y2, area->y1, area->y2};
    int32_t min_y = FACTORY_BOARD_HEIGHT - 1;
    int32_t max_y = 0;

    for (int i = 0; i < 4; ++i) {
        int32_t panel_x = 0;
        int32_t panel_y = 0;
        transform_logical_to_panel(corners_x[i], corners_y[i], &panel_x, &panel_y);
        if (panel_y < min_y) {
            min_y = panel_y;
        }
        if (panel_y > max_y) {
            max_y = panel_y;
        }
    }

    if (min_y < 0) {
        min_y = 0;
    }
    if (max_y >= FACTORY_BOARD_HEIGHT) {
        max_y = FACTORY_BOARD_HEIGHT - 1;
    }
    note_flush_rows(min_y, max_y);
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t clipped_x1 = LV_MAX(0, area->x1);
    const int32_t clipped_y1 = LV_MAX(0, area->y1);
    const int32_t clipped_x2 = LV_MIN((int32_t)s_mode_info.width - 1, area->x2);
    const int32_t clipped_y2 = LV_MIN((int32_t)s_mode_info.height - 1, area->y2);

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

            int32_t panel_x = 0;
            int32_t panel_y = 0;
            transform_logical_to_panel(x, y, &panel_x, &panel_y);

            if (panel_x < 0 || panel_x >= FACTORY_BOARD_WIDTH || panel_y < 0 || panel_y >= FACTORY_BOARD_HEIGHT) {
                continue;
            }

            if (s_use_4bpp_color) {
                const uint8_t gray4 = (uint8_t)(gray >> 4);
                uint8_t &dst = s_framebuffer[(panel_y * framebuffer_pitch()) + (panel_x >> 1)];
                if ((panel_x & 1) == 0) {
                    dst = (uint8_t)((dst & 0x0F) | (gray4 << 4));
                } else {
                    dst = (uint8_t)((dst & 0xF0) | gray4);
                }
            } else {
                bool is_white = false;
                if (s_enable_dither) {
                    const uint8_t threshold = (uint8_t)(kBayer4[panel_y & 0x3][panel_x & 0x3] * 16U);
                    is_white = gray >= threshold;
                } else {
                    is_white = gray >= 128;
                }

                uint8_t &dst = s_framebuffer[(panel_y * framebuffer_pitch()) + (panel_x >> 3)];
                const uint8_t mask = (uint8_t)(0x80u >> (panel_x & 0x07));
                if (is_white) {
                    dst = (uint8_t)(dst | mask);
                } else {
                    dst = (uint8_t)(dst & (uint8_t)(~mask));
                }
            }
        }
    }

    if (!s_use_4bpp_color) {
        lv_area_t clipped_area = {};
        clipped_area.x1 = (lv_coord_t)clipped_x1;
        clipped_area.y1 = (lv_coord_t)clipped_y1;
        clipped_area.x2 = (lv_coord_t)clipped_x2;
        clipped_area.y2 = (lv_coord_t)clipped_y2;
        note_transformed_rows(&clipped_area);
    }
    if (lv_disp_flush_is_last(disp_drv)) {
        flush_to_panel();
    }
    lv_disp_flush_ready(disp_drv);
}

static void update_disp_driver_resolution()
{
    if (s_disp == nullptr) {
        return;
    }

    s_disp_drv.hor_res = s_mode_info.width;
    s_disp_drv.ver_res = s_mode_info.height;
    s_disp_drv.rotated = LV_DISP_ROT_NONE;
    lv_disp_drv_update(s_disp, &s_disp_drv);
}

static bool init_lvgl()
{
    static lv_disp_draw_buf_t draw_buf;

    lv_init();

    s_draw_buf_1 = (lv_color_t *)heap_caps_calloc(kDrawBufPixels, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_draw_buf_2 = (lv_color_t *)heap_caps_calloc(kDrawBufPixels, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_draw_buf_1 == nullptr || s_draw_buf_2 == nullptr) {
        ESP_LOGE(TAG, "failed to allocate LVGL draw buffers");
        return false;
    }

    lv_disp_draw_buf_init(&draw_buf, s_draw_buf_1, s_draw_buf_2, kDrawBufPixels);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = s_mode_info.width;
    s_disp_drv.ver_res = s_mode_info.height;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.draw_buf = &draw_buf;
    s_disp_drv.full_refresh = 0;
    s_disp = lv_disp_drv_register(&s_disp_drv);

    return s_disp != nullptr;
}

static bool init_panel()
{
    if (s_i2c_bus == nullptr) {
        if (bsp_i2c_init() != ESP_OK) {
            ESP_LOGE(TAG, "bsp_i2c_init failed");
            return false;
        }
        s_i2c_bus = bsp_i2c_get_handle();
        if (s_i2c_bus == nullptr) {
            ESP_LOGE(TAG, "bsp_i2c_get_handle returned null");
            return false;
        }
    }

    bbepSetI2CMasterBus(s_i2c_bus);

    if (s_epaper.initPanel(BB_PANEL_LILYGO_T5P4, FACTORY_PANEL_SPI_CLOCK_HZ) != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "initPanel failed");
        return false;
    }

    // The board definition already provides panel size and allocates the backing buffers.
    // Calling setPanelSize() again after initPanel() causes FastEPD to reject the request.
    s_mode_info.native_width = (uint16_t)s_epaper.width();
    s_mode_info.native_height = (uint16_t)s_epaper.height();
    s_mode_info.width = s_mode_info.native_width;
    s_mode_info.height = s_mode_info.native_height;
    update_mode_summary();

    if (s_epaper.setMode(BB_MODE_1BPP) != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "setMode(BB_MODE_1BPP) failed");
        return false;
    }

    s_epaper.setPasses(s_mode_info.partial_passes, s_mode_info.full_passes);
    s_framebuffer = s_epaper.currentBuffer();
    if (s_framebuffer == nullptr) {
        ESP_LOGE(TAG, "currentBuffer returned null");
        return false;
    }

    s_epaper.fillScreen(BBEP_WHITE);
    s_use_4bpp_color = false;
    s_low_flash = true;
    s_first_4bpp_refresh = true;
    begin_flush();
    return true;
}

}  // namespace

extern "C" bool factory_display_init(void)
{
    update_mode_summary();
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
    s_force_clean_full_refresh = false;
    if (lv_scr_act() != nullptr) {
        lv_obj_invalidate(lv_scr_act());
    }
}

extern "C" void factory_display_refresh_now(bool full_refresh)
{
    if (full_refresh) {
        s_force_full_refresh = true;
    }
    s_force_clean_full_refresh = false;

    if (lv_scr_act() != nullptr) {
        lv_obj_invalidate(lv_scr_act());
    }
    if (lv_layer_top() != nullptr) {
        lv_obj_invalidate(lv_layer_top());
    }

    if (s_disp != nullptr) {
        lv_refr_now(s_disp);
    }
}

extern "C" void factory_display_refresh_now_clean(void)
{
    s_force_full_refresh = true;
    s_force_clean_full_refresh = true;

    if (lv_scr_act() != nullptr) {
        lv_obj_invalidate(lv_scr_act());
    }
    if (lv_layer_top() != nullptr) {
        lv_obj_invalidate(lv_layer_top());
    }

    if (s_disp != nullptr) {
        lv_refr_now(s_disp);
    }
}

extern "C" const factory_display_mode_info_t *factory_display_get_mode_info(void)
{
    return &s_mode_info;
}

extern "C" void factory_display_set_rotation(uint16_t rotation_deg)
{
    const uint16_t normalized = normalize_rotation(rotation_deg);
    if (s_mode_info.rotation_deg == normalized) {
        return;
    }

    s_mode_info.rotation_deg = normalized;
    update_mode_summary();
    update_disp_driver_resolution();
    factory_display_request_full_refresh();
}

extern "C" uint16_t factory_display_get_rotation(void)
{
    return s_mode_info.rotation_deg;
}

extern "C" void factory_display_set_mirror(uint8_t mirror_mode)
{
    mirror_mode &= 0x3U;
    if (s_mode_info.mirror_mode == mirror_mode) {
        return;
    }

    s_mode_info.mirror_mode = mirror_mode;
    update_mode_summary();
    factory_display_request_full_refresh();
}

extern "C" uint8_t factory_display_get_mirror(void)
{
    return s_mode_info.mirror_mode;
}

extern "C" void factory_display_set_passes(uint8_t partial_passes, uint8_t full_passes)
{
    if (partial_passes < 1U) {
        partial_passes = 1U;
    }
    if (partial_passes > 15U) {
        partial_passes = 15U;
    }
    if (full_passes < 1U) {
        full_passes = 1U;
    }
    if (full_passes > 15U) {
        full_passes = 15U;
    }

    s_mode_info.partial_passes = partial_passes;
    s_mode_info.full_passes = full_passes;
    if (!s_use_4bpp_color) {
        s_epaper.setPasses(partial_passes, full_passes);
    }
    update_mode_summary();
    if (!s_use_4bpp_color) {
        factory_display_request_full_refresh();
    }
}

extern "C" void factory_display_set_color_mode(bool use_4bpp)
{
    if (s_use_4bpp_color == use_4bpp) {
        return;
    }

    if (!apply_color_mode(use_4bpp)) {
        update_mode_summary();
        return;
    }

    if (lv_scr_act() != nullptr) {
        lv_obj_invalidate(lv_scr_act());
    }
}

extern "C" bool factory_display_get_color_mode(void)
{
    return s_use_4bpp_color;
}

extern "C" void factory_display_set_dither(bool enable)
{
    if (s_enable_dither == enable) {
        return;
    }

    s_enable_dither = enable;
    update_mode_summary();
    if (!s_use_4bpp_color) {
        factory_display_request_full_refresh();
    }
}

extern "C" bool factory_display_get_dither(void)
{
    return s_enable_dither;
}

extern "C" void factory_display_set_low_flash(bool enable)
{
    if (s_low_flash == enable) {
        return;
    }

    s_low_flash = enable;
    s_first_4bpp_refresh = true;
    update_mode_summary();
    if (s_use_4bpp_color) {
        factory_display_request_full_refresh();
    }
}

extern "C" bool factory_display_get_low_flash(void)
{
    return s_low_flash;
}

extern "C" void factory_display_set_max_partial_refreshes_before_full(uint8_t count)
{
    const uint8_t normalized = normalize_max_partial_refreshes_before_full(count);
    if (s_max_partial_refreshes_before_full == normalized) {
        return;
    }

    s_max_partial_refreshes_before_full = normalized;
    update_mode_summary();
}

extern "C" uint8_t factory_display_get_max_partial_refreshes_before_full(void)
{
    return s_max_partial_refreshes_before_full;
}
