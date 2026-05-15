#include "ui_screens.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "factory_assets.h"
#include "factory_icm20948.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

enum class IcmPageState {
    Idle,
    Ready,
    Error,
};

static lv_obj_t *s_badge = nullptr;
static lv_obj_t *s_badge_label = nullptr;
static lv_obj_t *s_read_btn = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_identity_label = nullptr;
static lv_obj_t *s_sample_label = nullptr;
static lv_obj_t *s_accel_label = nullptr;
static lv_obj_t *s_gyro_label = nullptr;
static lv_obj_t *s_mag_label = nullptr;
static lv_obj_t *s_temp_label = nullptr;
static factory_icm20948_sample_t s_sample = {};
static bool s_has_sample = false;
static IcmPageState s_state = IcmPageState::Idle;
static char s_status_text[160] = "Tap Read Sensor to sample ICM20948.";

static const char *badge_text(IcmPageState state)
{
    switch (state) {
        case IcmPageState::Ready:
            return "READY";
        case IcmPageState::Error:
            return "ERROR";
        case IcmPageState::Idle:
        default:
            return "IDLE";
    }
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

static void update_badge()
{
    if (s_badge == nullptr || s_badge_label == nullptr) {
        return;
    }

    const bool highlight = s_state == IcmPageState::Ready;
    lv_obj_set_style_bg_color(s_badge, highlight ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_badge_label, highlight ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    set_text_if_changed(s_badge_label, badge_text(s_state));
}

static void refresh_icm_ui()
{
    char line[160];

    update_badge();

    snprintf(line, sizeof(line), "Status: %s", s_status_text);
    set_text_if_changed(s_status_label, line);

    if (!s_has_sample) {
        set_text_if_changed(s_identity_label, "Address / WHO_AM_I / AK09916: -- / -- / --");
        set_text_if_changed(s_sample_label, "Sample # / Time: -- / --");
        set_text_if_changed(s_accel_label, "Accel[g]: x=-- y=-- z=--");
        set_text_if_changed(s_gyro_label, "Gyro[dps]: x=-- y=-- z=--");
        set_text_if_changed(s_mag_label, "Mag[uT]: x=-- y=-- z=--");
        set_text_if_changed(s_temp_label, "Temp[C]: --");
        return;
    }

    snprintf(line,
             sizeof(line),
             "Address / WHO_AM_I / AK09916: 0x%02X / 0x%02X / 0x%04X",
             s_sample.device_address,
             s_sample.who_am_i,
             s_sample.magnetometer_id);
    set_text_if_changed(s_identity_label, line);

    snprintf(line,
             sizeof(line),
             "Sample # / Time: %" PRIu32 " / %" PRIu64 " us",
             s_sample.sample_count,
             s_sample.sample_time_us);
    set_text_if_changed(s_sample_label, line);

    snprintf(line,
             sizeof(line),
             "Accel[g]: x=%.3f y=%.3f z=%.3f",
             s_sample.accel.x,
             s_sample.accel.y,
             s_sample.accel.z);
    set_text_if_changed(s_accel_label, line);

    snprintf(line,
             sizeof(line),
             "Gyro[dps]: x=%.3f y=%.3f z=%.3f",
             s_sample.gyro.x,
             s_sample.gyro.y,
             s_sample.gyro.z);
    set_text_if_changed(s_gyro_label, line);

    snprintf(line,
             sizeof(line),
             "Mag[uT]: x=%.3f y=%.3f z=%.3f",
             s_sample.mag.x,
             s_sample.mag.y,
             s_sample.mag.z);
    set_text_if_changed(s_mag_label, line);

    snprintf(line, sizeof(line), "Temp[C]: %.2f", s_sample.temperature_c);
    set_text_if_changed(s_temp_label, line);
}

static void reset_icm_ui()
{
    memset(&s_sample, 0, sizeof(s_sample));
    s_has_sample = false;
    s_state = IcmPageState::Idle;
    snprintf(s_status_text, sizeof(s_status_text), "Tap Read Sensor to sample ICM20948.");

    if (s_read_btn != nullptr) {
        lv_obj_clear_state(s_read_btn, LV_STATE_DISABLED);
    }

    refresh_icm_ui();
}

static void read_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_read_btn != nullptr) {
        lv_obj_add_state(s_read_btn, LV_STATE_DISABLED);
    }

    factory_icm20948_sample_t sample = {};
    char status[sizeof(s_status_text)] = {};
    const bool ok = factory_icm20948_read(&sample, status, sizeof(status));

    if (ok) {
        s_sample = sample;
        s_has_sample = true;
        s_state = IcmPageState::Ready;
    } else {
        memset(&s_sample, 0, sizeof(s_sample));
        s_has_sample = false;
        s_state = IcmPageState::Error;
    }
    snprintf(s_status_text, sizeof(s_status_text), "%s", status[0] != '\0' ? status : "ICM20948 read failed.");

    if (s_read_btn != nullptr) {
        lv_obj_clear_state(s_read_btn, LV_STATE_DISABLED);
    }

    refresh_icm_ui();
}

static void create_icm20948(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "ICM20948");

    s_badge = lv_obj_create(parent);
    lv_obj_set_size(s_badge, 180, 46);
    lv_obj_align(s_badge, LV_ALIGN_TOP_RIGHT, -28, 18);
    lv_obj_set_style_bg_color(s_badge, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_badge, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_badge, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_badge, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_badge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);

    s_badge_label = lv_label_create(s_badge);
    lv_label_set_text(s_badge_label, "IDLE");
    lv_obj_center(s_badge_label);
    lv_obj_set_style_text_font(s_badge_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_badge_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 96, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);

    lv_obj_t *summary = factory_ui_create_info_label(
        panel,
        "Tap the button to read one ICM20948 sample. This page does not auto-refresh.");
    lv_obj_set_width(summary, lv_pct(100));

    s_read_btn = factory_ui_create_action_button(panel, "Read Sensor", read_btn_event_cb, nullptr);
    lv_obj_set_width(s_read_btn, 280);

    s_status_label = factory_ui_create_info_label(panel, "");
    s_identity_label = factory_ui_create_info_label(panel, "");
    s_sample_label = factory_ui_create_info_label(panel, "");
    s_accel_label = factory_ui_create_info_label(panel, "");
    s_gyro_label = factory_ui_create_info_label(panel, "");
    s_mag_label = factory_ui_create_info_label(panel, "");
    s_temp_label = factory_ui_create_info_label(panel, "");

    reset_icm_ui();
}

static void entry_icm20948(void)
{
    reset_icm_ui();
}

static void exit_icm20948(void)
{
    factory_icm20948_deinit();
}

static void destroy_icm20948(void)
{
    factory_icm20948_deinit();
    s_badge = nullptr;
    s_badge_label = nullptr;
    s_read_btn = nullptr;
    s_status_label = nullptr;
    s_identity_label = nullptr;
    s_sample_label = nullptr;
    s_accel_label = nullptr;
    s_gyro_label = nullptr;
    s_mag_label = nullptr;
    s_temp_label = nullptr;
    memset(&s_sample, 0, sizeof(s_sample));
    s_has_sample = false;
    s_state = IcmPageState::Idle;
    s_status_text[0] = '\0';
}

static scr_lifecycle_t s_icm20948_lifecycle = {
    .create = create_icm20948,
    .entry = entry_icm20948,
    .exit = exit_icm20948,
    .destroy = destroy_icm20948,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_icm20948_lifecycle(void)
{
    return &s_icm20948_lifecycle;
}
