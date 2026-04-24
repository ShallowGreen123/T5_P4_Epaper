#include "ui_screens.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "factory_assets.h"
#include "factory_camera.h"
#include "factory_camera_processor.h"
#include "factory_display.h"
#include "factory_sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_theme.h"

#ifndef CONFIG_FACTORY_CAMERA_OUTPUT_WIDTH
#define CONFIG_FACTORY_CAMERA_OUTPUT_WIDTH 640
#endif

#ifndef CONFIG_FACTORY_CAMERA_OUTPUT_HEIGHT
#define CONFIG_FACTORY_CAMERA_OUTPUT_HEIGHT 360
#endif

#ifndef CONFIG_FACTORY_CAMERA_TASK_STACK_SIZE
#define CONFIG_FACTORY_CAMERA_TASK_STACK_SIZE 8192
#endif

namespace {

static const char *TAG = "screen_camera";

constexpr uint32_t kPhotoWidth = CONFIG_FACTORY_CAMERA_OUTPUT_WIDTH;
constexpr uint32_t kPhotoHeight = CONFIG_FACTORY_CAMERA_OUTPUT_HEIGHT;
constexpr size_t kPhotoPixels = (size_t)kPhotoWidth * kPhotoHeight;
constexpr size_t kPhotoPitch1bpp = (kPhotoWidth + 7U) / 8U;
constexpr size_t kPhotoBytes1bpp = kPhotoPitch1bpp * kPhotoHeight;
constexpr size_t kPhotoPitch4bpp = (kPhotoWidth + 1U) / 2U;
constexpr size_t kPhotoBytes4bpp = kPhotoPitch4bpp * kPhotoHeight;
constexpr size_t kPaletteBytes1bpp = sizeof(lv_color32_t) * 2U;
constexpr size_t kPaletteBytes4bpp = sizeof(lv_color32_t) * 16U;
constexpr uint32_t kUiRefreshMs = 250;

enum class CameraState {
    Idle,
    Capturing,
    Processing,
    Done,
    Error,
    Saving,
};

static lv_obj_t *s_result_badge = nullptr;
static lv_obj_t *s_result_label = nullptr;
static lv_obj_t *s_photo_stage = nullptr;
static lv_obj_t *s_photo_img = nullptr;
static lv_obj_t *s_placeholder = nullptr;
static lv_obj_t *s_capture_btn = nullptr;
static lv_obj_t *s_save_btn = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_footer_label = nullptr;
static lv_timer_t *s_refresh_timer = nullptr;

static SemaphoreHandle_t s_lock = nullptr;
static TaskHandle_t s_capture_task = nullptr;
static bool s_capture_active = false;
static uint8_t *s_photo_map_1bpp = nullptr;
static uint8_t *s_photo_map_4bpp = nullptr;
static uint8_t *s_photo_gray = nullptr;
static uint8_t *s_pending_gray = nullptr;
static lv_img_dsc_t s_photo_dsc_1bpp = {};
static lv_img_dsc_t s_photo_dsc_4bpp = {};
static bool s_runtime_ready = false;
static bool s_has_photo = false;
static bool s_pending_photo = false;
static bool s_photo_img_uses_4bpp = false;
static CameraState s_state = CameraState::Idle;
static char s_status_text[160] = "Ready.";
static char s_footer_text[160] = "";

static void refresh_camera_ui();

static void *alloc_psram(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = malloc(size);
    }
    return ptr;
}

static void set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == nullptr || text == nullptr) {
        return;
    }

    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void style_flat_panel(lv_obj_t *obj, lv_coord_t radius, uint8_t border_width)
{
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, border_width, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_transparent_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static const char *state_label(CameraState state)
{
    switch (state) {
        case CameraState::Capturing:
            return "CAPTURING";
        case CameraState::Processing:
            return "PROCESSING";
        case CameraState::Done:
            return "DONE";
        case CameraState::Error:
            return "ERROR";
        case CameraState::Saving:
            return "SAVING";
        case CameraState::Idle:
        default:
            return "READY";
    }
}

static bool state_busy(CameraState state)
{
    return state == CameraState::Capturing || state == CameraState::Processing || state == CameraState::Saving;
}

static void set_camera_state(CameraState state, const char *status, const char *footer)
{
    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }

    s_state = state;
    if (status != nullptr) {
        snprintf(s_status_text, sizeof(s_status_text), "%s", status);
    }
    if (footer != nullptr) {
        snprintf(s_footer_text, sizeof(s_footer_text), "%s", footer);
    }

    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }
}

static void set_camera_statusf(CameraState state, const char *format, ...)
{
    char text[sizeof(s_status_text)] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    set_camera_state(state, text, nullptr);
}

static bool camera_preview_uses_4bpp()
{
    return factory_display_get_color_mode();
}

static const lv_img_dsc_t *active_photo_dsc()
{
    return camera_preview_uses_4bpp() ? &s_photo_dsc_4bpp : &s_photo_dsc_1bpp;
}

static void set_photo_img_source(bool force)
{
    if (s_photo_img == nullptr) {
        return;
    }

    const bool use_4bpp = camera_preview_uses_4bpp();
    if (!force && s_photo_img_uses_4bpp == use_4bpp) {
        return;
    }

    s_photo_img_uses_4bpp = use_4bpp;
    lv_img_set_src(s_photo_img, active_photo_dsc());
    lv_obj_invalidate(s_photo_img);
}

static bool render_photo_maps()
{
    if (s_photo_gray == nullptr || s_photo_map_1bpp == nullptr || s_photo_map_4bpp == nullptr) {
        return false;
    }

    if (!factory_camera_dither_bayer4(
            s_photo_gray,
            s_photo_map_1bpp + kPaletteBytes1bpp,
            kPhotoWidth,
            kPhotoHeight)) {
        return false;
    }

    return factory_camera_pack_gray4(
        s_photo_gray,
        s_photo_map_4bpp + kPaletteBytes4bpp,
        kPhotoWidth,
        kPhotoHeight);
}

static bool ensure_runtime()
{
    if (s_runtime_ready) {
        return true;
    }

    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == nullptr) {
            ESP_LOGE(TAG, "failed to create camera UI lock");
            return false;
        }
    }

    const uint32_t image_size_1bpp = lv_img_buf_get_img_size(kPhotoWidth, kPhotoHeight, LV_IMG_CF_INDEXED_1BIT);
    const uint32_t image_size_4bpp = lv_img_buf_get_img_size(kPhotoWidth, kPhotoHeight, LV_IMG_CF_INDEXED_4BIT);
    if (image_size_1bpp < kPaletteBytes1bpp + kPhotoBytes1bpp ||
        image_size_4bpp < kPaletteBytes4bpp + kPhotoBytes4bpp) {
        ESP_LOGE(TAG,
                 "unexpected LVGL indexed image sizes: 1bpp=%" PRIu32 ", 4bpp=%" PRIu32,
                 image_size_1bpp,
                 image_size_4bpp);
        return false;
    }

    if (s_photo_map_1bpp == nullptr) {
        s_photo_map_1bpp = static_cast<uint8_t *>(alloc_psram(image_size_1bpp));
    }
    if (s_photo_map_4bpp == nullptr) {
        s_photo_map_4bpp = static_cast<uint8_t *>(alloc_psram(image_size_4bpp));
    }
    if (s_photo_gray == nullptr) {
        s_photo_gray = static_cast<uint8_t *>(alloc_psram(kPhotoPixels));
    }
    if (s_pending_gray == nullptr) {
        s_pending_gray = static_cast<uint8_t *>(alloc_psram(kPhotoPixels));
    }
    if (s_photo_map_1bpp == nullptr || s_photo_map_4bpp == nullptr || s_photo_gray == nullptr || s_pending_gray == nullptr) {
        ESP_LOGE(TAG, "failed to allocate camera photo buffers");
        return false;
    }

    memset(s_photo_map_1bpp, 0xFF, image_size_1bpp);
    memset(s_photo_map_4bpp, 0xFF, image_size_4bpp);
    memset(s_photo_gray, 0xFF, kPhotoPixels);
    memset(s_pending_gray, 0xFF, kPhotoPixels);

    memset(&s_photo_dsc_1bpp, 0, sizeof(s_photo_dsc_1bpp));
    s_photo_dsc_1bpp.header.cf = LV_IMG_CF_INDEXED_1BIT;
    s_photo_dsc_1bpp.header.always_zero = 0;
    s_photo_dsc_1bpp.header.reserved = 0;
    s_photo_dsc_1bpp.header.w = kPhotoWidth;
    s_photo_dsc_1bpp.header.h = kPhotoHeight;
    s_photo_dsc_1bpp.data_size = image_size_1bpp;
    s_photo_dsc_1bpp.data = s_photo_map_1bpp;
    lv_img_buf_set_palette(&s_photo_dsc_1bpp, 0, lv_color_black());
    lv_img_buf_set_palette(&s_photo_dsc_1bpp, 1, lv_color_white());

    memset(&s_photo_dsc_4bpp, 0, sizeof(s_photo_dsc_4bpp));
    s_photo_dsc_4bpp.header.cf = LV_IMG_CF_INDEXED_4BIT;
    s_photo_dsc_4bpp.header.always_zero = 0;
    s_photo_dsc_4bpp.header.reserved = 0;
    s_photo_dsc_4bpp.header.w = kPhotoWidth;
    s_photo_dsc_4bpp.header.h = kPhotoHeight;
    s_photo_dsc_4bpp.data_size = image_size_4bpp;
    s_photo_dsc_4bpp.data = s_photo_map_4bpp;
    for (uint8_t i = 0; i < 16U; ++i) {
        const uint8_t level = (uint8_t)(i * 17U);
        lv_img_buf_set_palette(&s_photo_dsc_4bpp, i, lv_color_make(level, level, level));
    }

    s_runtime_ready = true;
    return true;
}

static void capture_task(void *arg)
{
    (void)arg;
    bool powered = false;
    bool inited = false;
    uint8_t *raw_frame = nullptr;
    char footer[sizeof(s_footer_text)] = {};
    factory_camera_frame_info_t frame = {};
    uint32_t captured_len = 0;

    set_camera_state(CameraState::Capturing, "Powering camera rails...", "");

    if (!factory_camera_power_on()) {
        set_camera_state(CameraState::Error, "Camera power-up failed.", "");
        goto done;
    }
    powered = true;

    if (!factory_camera_is_detected()) {
        set_camera_state(CameraState::Error, "No supported camera sensor responded on SCCB.", "");
        goto done;
    }

    set_camera_state(CameraState::Capturing, "Starting MIPI CSI video device...", nullptr);
    if (!factory_camera_init()) {
        set_camera_state(CameraState::Error, "Camera video initialization failed.", "");
        goto done;
    }
    inited = true;

    if (!factory_camera_get_frame_info(&frame) || frame.bytes_per_frame == 0) {
        set_camera_state(CameraState::Error, "Camera returned an unsupported frame format.", "");
        goto done;
    }

    raw_frame = static_cast<uint8_t *>(alloc_psram(frame.bytes_per_frame));
    if (raw_frame == nullptr) {
        set_camera_state(CameraState::Error, "Not enough memory for the raw camera frame.", "");
        goto done;
    }

    set_camera_statusf(CameraState::Capturing,
                       "Capturing %" PRIu32 "x%" PRIu32 " %s...",
                       frame.width,
                       frame.height,
                       factory_camera_pixel_format_name(frame.pixel_format));
    if (!factory_camera_capture(raw_frame, frame.bytes_per_frame, &captured_len)) {
        set_camera_state(CameraState::Error, "Camera frame capture failed.", "");
        goto done;
    }

    if (s_pending_gray == nullptr) {
        set_camera_state(CameraState::Error, "Camera buffers are not ready.", "");
        goto done;
    }

    set_camera_statusf(CameraState::Processing,
                       "Scaling to %" PRIu32 "x%" PRIu32 " grayscale...",
                       kPhotoWidth,
                       kPhotoHeight);
    if (!factory_camera_process_frame_to_grayscale(
            raw_frame,
            frame.width,
            frame.height,
            frame.pixel_format,
            s_pending_gray,
            kPhotoWidth,
            kPhotoHeight)) {
        set_camera_state(CameraState::Error, "Image processing failed.", "");
        goto done;
    }

    snprintf(footer,
             sizeof(footer),
             "Source %" PRIu32 "x%" PRIu32 " %s / %" PRIu32 " bytes",
             frame.width,
             frame.height,
             factory_camera_pixel_format_name(frame.pixel_format),
             captured_len);

    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    s_pending_photo = true;
    s_state = CameraState::Done;
    snprintf(s_status_text, sizeof(s_status_text), "Photo captured.");
    snprintf(s_footer_text, sizeof(s_footer_text), "%s", footer);
    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }

done:
    if (raw_frame != nullptr) {
        free(raw_frame);
    }
    if (inited) {
        factory_camera_deinit();
    }
    if (powered) {
        factory_camera_power_off();
    }

    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_capture_task = nullptr;
        s_capture_active = false;
        xSemaphoreGive(s_lock);
    }
    vTaskDelete(nullptr);
}

static void capture_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!ensure_runtime()) {
        set_camera_state(CameraState::Error, "Camera buffers could not be allocated.", "");
        refresh_camera_ui();
        return;
    }

    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    const bool busy = s_capture_active || state_busy(s_state);
    if (!busy) {
        s_capture_active = true;
    }
    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }
    if (busy) {
        return;
    }

    set_camera_state(CameraState::Capturing, "Starting capture...", "");
    TaskHandle_t task = nullptr;
    const BaseType_t created = xTaskCreate(
        capture_task,
        "factory_cam",
        CONFIG_FACTORY_CAMERA_TASK_STACK_SIZE,
        nullptr,
        5,
        &task);
    if (created != pdTRUE || task == nullptr) {
        if (s_lock != nullptr) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_capture_active = false;
            xSemaphoreGive(s_lock);
        }
        set_camera_state(CameraState::Error, "Failed to create camera task.", "");
        refresh_camera_ui();
        return;
    }

    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_capture_active) {
            s_capture_task = task;
        }
        xSemaphoreGive(s_lock);
    }
    refresh_camera_ui();
}

static void save_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!ensure_runtime()) {
        return;
    }

    uint8_t *copy = static_cast<uint8_t *>(malloc(kPhotoBytes1bpp));
    if (copy == nullptr) {
        set_camera_state(CameraState::Error, "Not enough memory to save the photo.", nullptr);
        refresh_camera_ui();
        return;
    }

    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    const bool can_save = s_has_photo && !state_busy(s_state) && !s_capture_active;
    if (can_save) {
        s_state = CameraState::Saving;
        snprintf(s_status_text, sizeof(s_status_text), "Saving photo to SD...");
    }
    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }

    if (!can_save) {
        free(copy);
        set_camera_state(CameraState::Idle, "Capture a photo before saving.", nullptr);
        refresh_camera_ui();
        return;
    }

    if (!render_photo_maps()) {
        free(copy);
        set_camera_state(CameraState::Error, "Image rendering failed.", nullptr);
        refresh_camera_ui();
        return;
    }
    memcpy(copy, s_photo_map_1bpp + kPaletteBytes1bpp, kPhotoBytes1bpp);

    refresh_camera_ui();

    bool ok = false;
    char path[160] = {};
    if (factory_sd_enter_root()) {
        time_t now = time(nullptr);
        struct tm timeinfo = {};
        if (now > 0 && localtime_r(&now, &timeinfo) != nullptr && timeinfo.tm_year >= (2024 - 1900)) {
            char stamp[32] = {};
            strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &timeinfo);
            snprintf(path, sizeof(path), "%s/camera_%s.pbm", FACTORY_SD_MOUNT_POINT, stamp);
        } else {
            snprintf(path,
                     sizeof(path),
                     "%s/camera_%llu.pbm",
                     FACTORY_SD_MOUNT_POINT,
                     (unsigned long long)(esp_timer_get_time() / 1000ULL));
        }

        FILE *fp = fopen(path, "wb");
        if (fp != nullptr) {
            fprintf(fp, "P4\n%" PRIu32 " %" PRIu32 "\n", kPhotoWidth, kPhotoHeight);
            for (size_t i = 0; i < kPhotoBytes1bpp; ++i) {
                const uint8_t pbm_byte = (uint8_t)~copy[i];
                fwrite(&pbm_byte, 1, 1, fp);
            }
            ok = fclose(fp) == 0;
        }
    }

    free(copy);
    if (ok) {
        set_camera_state(CameraState::Done, path, nullptr);
    } else {
        set_camera_state(CameraState::Error, "Save failed. Insert or check the SD card.", nullptr);
    }
    refresh_camera_ui();
}

static void refresh_camera_ui()
{
    if (!ensure_runtime()) {
        return;
    }

    CameraState state = CameraState::Idle;
    char status[sizeof(s_status_text)] = {};
    char footer[sizeof(s_footer_text)] = {};
    bool has_photo = false;
    bool pending_photo = false;
    bool busy = false;

    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }

    if (s_pending_photo) {
        memcpy(s_photo_gray, s_pending_gray, kPhotoPixels);
        s_pending_photo = false;
        s_has_photo = true;
        pending_photo = true;
    }

    state = s_state;
    has_photo = s_has_photo;
    busy = state_busy(state) || s_capture_active;
    snprintf(status, sizeof(status), "%s", s_status_text);
    snprintf(footer, sizeof(footer), "%s", s_footer_text);

    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }

    if (pending_photo && !render_photo_maps()) {
        set_camera_state(CameraState::Error, "Image rendering failed.", nullptr);
        state = CameraState::Error;
        has_photo = false;
        snprintf(status, sizeof(status), "Image rendering failed.");
    }

    if (s_result_badge != nullptr && s_result_label != nullptr) {
        const bool invert = state == CameraState::Done || state == CameraState::Capturing || state == CameraState::Processing;
        lv_obj_set_style_bg_color(s_result_badge, invert ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_result_label, invert ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
        set_text_if_changed(s_result_label, state_label(state));
    }

    set_text_if_changed(s_status_label, status);
    set_text_if_changed(s_footer_label, footer);

    if (s_capture_btn != nullptr) {
        if (busy) {
            lv_obj_add_state(s_capture_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_capture_btn, LV_STATE_DISABLED);
        }
    }
    if (s_save_btn != nullptr) {
        if (busy || !has_photo) {
            lv_obj_add_state(s_save_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_save_btn, LV_STATE_DISABLED);
        }
    }

    if (s_photo_img != nullptr && s_placeholder != nullptr) {
        if (has_photo) {
            lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_photo_img, LV_OBJ_FLAG_HIDDEN);
            set_photo_img_source(pending_photo || s_photo_img_uses_4bpp != camera_preview_uses_4bpp());
        } else {
            lv_obj_add_flag(s_photo_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_camera_ui();
}

static void create_camera(lv_obj_t *parent)
{
    ensure_runtime();

    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Camera");

    s_result_badge = lv_obj_create(parent);
    lv_obj_set_size(s_result_badge, 180, 46);
    lv_obj_align(s_result_badge, LV_ALIGN_TOP_RIGHT, -28, 18);
    style_flat_panel(s_result_badge, 14, 2);
    lv_obj_set_style_pad_all(s_result_badge, 0, LV_PART_MAIN);

    s_result_label = lv_label_create(s_result_badge);
    lv_label_set_text(s_result_label, "READY");
    lv_obj_center(s_result_label);
    lv_obj_set_style_text_font(s_result_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_result_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 96, 88);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 14, LV_PART_MAIN);

    s_photo_stage = lv_obj_create(panel);
    lv_obj_set_width(s_photo_stage, lv_pct(100));
    lv_obj_set_height(s_photo_stage, 440);
    style_flat_panel(s_photo_stage, 12, 2);
    lv_obj_set_style_pad_all(s_photo_stage, 0, LV_PART_MAIN);

    s_photo_img = lv_img_create(s_photo_stage);
    set_photo_img_source(true);
    lv_obj_center(s_photo_img);

    s_placeholder = lv_label_create(s_photo_stage);
    lv_label_set_text(s_placeholder, LV_SYMBOL_IMAGE "\nReady");
    lv_obj_set_style_text_align(s_placeholder, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_placeholder, FACTORY_FONT_UI_HOME_TEXT, LV_PART_MAIN);
    lv_obj_center(s_placeholder);

    lv_obj_t *button_row = create_transparent_row(panel);
    s_capture_btn = factory_ui_create_action_button(button_row, "Capture", capture_btn_event_cb, nullptr);
    lv_obj_set_width(s_capture_btn, lv_pct(48));
    s_save_btn = factory_ui_create_action_button(button_row, "Save PBM", save_btn_event_cb, nullptr);
    lv_obj_set_width(s_save_btn, lv_pct(48));

    s_status_label = factory_ui_create_info_label(panel, "Ready.");
    lv_obj_set_width(s_status_label, lv_pct(100));

    s_footer_label = factory_ui_create_info_label(panel, "");
    lv_obj_set_width(s_footer_label, lv_pct(100));
    lv_obj_set_style_text_align(s_footer_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    refresh_camera_ui();
}

static void entry_camera(void)
{
    refresh_camera_ui();
    if (s_refresh_timer == nullptr) {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, kUiRefreshMs, nullptr);
    }
}

static void exit_camera(void)
{
    if (s_refresh_timer != nullptr) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
}

static void destroy_camera(void)
{
    s_result_badge = nullptr;
    s_result_label = nullptr;
    s_photo_stage = nullptr;
    s_photo_img = nullptr;
    s_placeholder = nullptr;
    s_capture_btn = nullptr;
    s_save_btn = nullptr;
    s_status_label = nullptr;
    s_footer_label = nullptr;
    s_photo_img_uses_4bpp = false;
}

static scr_lifecycle_t s_camera_lifecycle = {
    .create = create_camera,
    .entry = entry_camera,
    .exit = exit_camera,
    .destroy = destroy_camera,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_camera_lifecycle(void)
{
    return &s_camera_lifecycle;
}
