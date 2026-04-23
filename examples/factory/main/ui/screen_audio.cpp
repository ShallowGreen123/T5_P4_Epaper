#include "ui_screens.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "factory_audio.h"
#include "factory_assets.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

constexpr uint32_t kUiRefreshMs = 350;
constexpr lv_coord_t kWaveBarMinWidth = 3;

struct StatusPill {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *value;
};

enum class AudioAction : intptr_t {
    Record = 0,
    Playback,
    Loopback,
    Stop,
};

static StatusPill s_codec_pill = {};
static StatusPill s_mic_pill = {};
static StatusPill s_speaker_pill = {};
static StatusPill s_record_pill = {};
static lv_obj_t *s_result_badge = nullptr;
static lv_obj_t *s_result_label = nullptr;
static lv_obj_t *s_wave_stage = nullptr;
static lv_obj_t *s_wave_midline = nullptr;
static lv_obj_t *s_wave_bars[FACTORY_AUDIO_WAVEFORM_BINS] = {};
static lv_obj_t *s_level_fill = nullptr;
static lv_obj_t *s_level_label = nullptr;
static lv_obj_t *s_peak_label = nullptr;
static lv_obj_t *s_noise_label = nullptr;
static lv_obj_t *s_clip_label = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_footer_label = nullptr;
static lv_timer_t *s_refresh_timer = nullptr;

static void refresh_audio_ui();

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

static StatusPill create_status_pill(lv_obj_t *parent, const char *title)
{
    StatusPill pill = {};
    pill.card = lv_obj_create(parent);
    lv_obj_set_size(pill.card, lv_pct(23), 72);
    style_flat_panel(pill.card, 12, 2);
    lv_obj_set_style_pad_all(pill.card, 10, LV_PART_MAIN);

    pill.title = lv_label_create(pill.card);
    lv_label_set_text(pill.title, title);
    lv_obj_align(pill.title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(pill.title, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(pill.title, lv_color_black(), LV_PART_MAIN);

    pill.value = lv_label_create(pill.card);
    lv_label_set_text(pill.value, "--");
    lv_obj_align(pill.value, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_font(pill.value, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(pill.value, lv_color_black(), LV_PART_MAIN);
    return pill;
}

static void update_status_pill(StatusPill *pill, const char *value, bool active)
{
    if (pill == nullptr || pill->card == nullptr) {
        return;
    }

    lv_color_t bg = active ? lv_color_black() : lv_color_white();
    lv_color_t fg = active ? lv_color_white() : lv_color_black();
    lv_obj_set_style_bg_color(pill->card, bg, LV_PART_MAIN);
    lv_obj_set_style_border_color(pill->card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(pill->title, fg, LV_PART_MAIN);
    lv_obj_set_style_text_color(pill->value, fg, LV_PART_MAIN);
    set_text_if_changed(pill->value, value);
}

static void update_result_badge(const char *text, bool pass)
{
    if (s_result_badge == nullptr || s_result_label == nullptr) {
        return;
    }

    lv_obj_set_style_bg_color(s_result_badge, pass ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_result_label, pass ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    set_text_if_changed(s_result_label, text);
}

static void update_waveform(const factory_audio_state_t *state)
{
    if (state == nullptr || s_wave_stage == nullptr) {
        return;
    }

    lv_obj_update_layout(s_wave_stage);
    const lv_coord_t width = lv_obj_get_content_width(s_wave_stage);
    const lv_coord_t height = lv_obj_get_content_height(s_wave_stage);
    if (width <= 0 || height <= 0) {
        return;
    }

    const lv_coord_t baseline = height / 2;
    const lv_coord_t usable_amp = baseline > 18 ? baseline - 18 : baseline;
    const lv_coord_t bar_width = width / FACTORY_AUDIO_WAVEFORM_BINS > kWaveBarMinWidth
                                     ? width / FACTORY_AUDIO_WAVEFORM_BINS
                                     : kWaveBarMinWidth;

    if (s_wave_midline != nullptr) {
        lv_obj_set_size(s_wave_midline, lv_pct(100), 2);
        lv_obj_set_pos(s_wave_midline, 0, baseline);
    }

    for (size_t i = 0; i < FACTORY_AUDIO_WAVEFORM_BINS; ++i) {
        if (s_wave_bars[i] == nullptr) {
            continue;
        }

        const int8_t sample = state->waveform[i];
        lv_coord_t amp = (lv_coord_t)((sample >= 0 ? sample : -sample) * usable_amp / 100);
        if (amp < 2) {
            amp = 2;
        }

        const lv_coord_t x = (FACTORY_AUDIO_WAVEFORM_BINS <= 1)
                                 ? 0
                                 : (lv_coord_t)((i * (width - bar_width)) / (FACTORY_AUDIO_WAVEFORM_BINS - 1));
        const lv_coord_t y = sample >= 0 ? baseline - amp : baseline;
        lv_obj_set_size(s_wave_bars[i], bar_width, amp);
        lv_obj_set_pos(s_wave_bars[i], x, y);
    }
}

static const char *mode_name(factory_audio_mode_t mode)
{
    switch (mode) {
        case FACTORY_AUDIO_MODE_MONITOR:
            return "monitor";
        case FACTORY_AUDIO_MODE_RECORD:
            return "recording";
        case FACTORY_AUDIO_MODE_PLAYBACK:
            return "playback";
        case FACTORY_AUDIO_MODE_LOOPBACK:
            return "loopback";
        case FACTORY_AUDIO_MODE_ERROR:
            return "error";
        case FACTORY_AUDIO_MODE_IDLE:
        default:
            return "idle";
    }
}

static void refresh_audio_ui()
{
    factory_audio_state_t state = {};
    factory_audio_get_state(&state);

    const bool overall_pass = state.codec_ready && state.i2s_ready && state.mic_ready && state.speaker_ready;
    update_result_badge(overall_pass ? "AUDIO PASS" : state.result_text, overall_pass);
    update_status_pill(&s_codec_pill, state.codec_ready ? "OK" : "WAIT", state.codec_ready);
    update_status_pill(&s_mic_pill, state.mic_ready ? "LIVE" : "LOW", state.mic_ready);
    update_status_pill(&s_speaker_pill, state.speaker_ready ? "PASS" : "READY", state.speaker_ready);
    update_status_pill(&s_record_pill, state.recording_ready ? "SAVED" : "EMPTY", state.recording_ready);
    update_waveform(&state);

    if (s_level_fill != nullptr) {
        lv_obj_set_width(s_level_fill, lv_pct(state.rms_percent));
    }

    char text[160] = {};
    snprintf(text, sizeof(text), "Mic Level  RMS %u%%", state.rms_percent);
    set_text_if_changed(s_level_label, text);
    snprintf(text, sizeof(text), "Peak %u%%", state.peak_percent);
    set_text_if_changed(s_peak_label, text);
    snprintf(text, sizeof(text), "Noise %u%%", state.noise_floor_percent);
    set_text_if_changed(s_noise_label, text);
    snprintf(text, sizeof(text), "Clip %" PRIu32, state.clip_count);
    set_text_if_changed(s_clip_label, text);
    set_text_if_changed(s_status_label, state.status_text);
    snprintf(text,
             sizeof(text),
             "%" PRIu32 " Hz / 16-bit stereo / gain %u dB / vol %u / %s / %u KB",
             state.sample_rate_hz,
             state.mic_gain_db,
             state.volume_percent,
             mode_name(state.mode),
             (unsigned)(state.bytes_recorded / 1024));
    set_text_if_changed(s_footer_label, text);
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_audio_ui();
}

static void action_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const AudioAction action = (AudioAction)(intptr_t)lv_event_get_user_data(e);
    switch (action) {
        case AudioAction::Record:
            factory_audio_record_3s();
            break;
        case AudioAction::Playback:
            factory_audio_playback();
            break;
        case AudioAction::Loopback:
            factory_audio_start_loopback();
            break;
        case AudioAction::Stop:
        default:
            factory_audio_stop();
            break;
    }
    refresh_audio_ui();
}

static void create_audio(lv_obj_t *parent)
{
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Audio Showcase");

    s_result_badge = lv_obj_create(parent);
    lv_obj_set_size(s_result_badge, 220, 46);
    lv_obj_align(s_result_badge, LV_ALIGN_TOP_RIGHT, -28, 18);
    style_flat_panel(s_result_badge, 14, 2);
    lv_obj_set_style_pad_all(s_result_badge, 0, LV_PART_MAIN);

    s_result_label = lv_label_create(s_result_badge);
    lv_label_set_text(s_result_label, "INIT");
    lv_obj_center(s_result_label);
    lv_obj_set_style_text_font(s_result_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_result_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 96, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);

    lv_obj_t *status_row = create_transparent_row(panel);
    s_codec_pill = create_status_pill(status_row, "Codec");
    s_mic_pill = create_status_pill(status_row, "Mic");
    s_speaker_pill = create_status_pill(status_row, "Speaker");
    s_record_pill = create_status_pill(status_row, "Record");

    s_wave_stage = lv_obj_create(panel);
    lv_obj_set_width(s_wave_stage, lv_pct(100));
    lv_obj_set_flex_grow(s_wave_stage, 1);
    style_flat_panel(s_wave_stage, 14, 2);
    lv_obj_set_style_pad_all(s_wave_stage, 14, LV_PART_MAIN);

    s_wave_midline = lv_obj_create(s_wave_stage);
    lv_obj_set_style_bg_color(s_wave_midline, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_wave_midline, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wave_midline, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_wave_midline, 0, LV_PART_MAIN);

    for (size_t i = 0; i < FACTORY_AUDIO_WAVEFORM_BINS; ++i) {
        s_wave_bars[i] = lv_obj_create(s_wave_stage);
        lv_obj_set_style_bg_color(s_wave_bars[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_wave_bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_wave_bars[i], 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_wave_bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_wave_bars[i], 0, LV_PART_MAIN);
    }

    lv_obj_t *meter_row = create_transparent_row(panel);
    lv_obj_set_height(meter_row, 78);

    lv_obj_t *level_wrap = lv_obj_create(meter_row);
    lv_obj_set_size(level_wrap, lv_pct(62), 72);
    style_flat_panel(level_wrap, 12, 2);
    lv_obj_set_style_pad_all(level_wrap, 12, LV_PART_MAIN);

    s_level_label = lv_label_create(level_wrap);
    lv_label_set_text(s_level_label, "Mic Level");
    lv_obj_align(s_level_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(s_level_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_level_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *level_track = lv_obj_create(level_wrap);
    lv_obj_set_size(level_track, lv_pct(100), 18);
    lv_obj_align(level_track, LV_ALIGN_BOTTOM_MID, 0, 0);
    style_flat_panel(level_track, 4, 2);
    lv_obj_set_style_pad_all(level_track, 0, LV_PART_MAIN);

    s_level_fill = lv_obj_create(level_track);
    lv_obj_set_size(s_level_fill, lv_pct(0), lv_pct(100));
    lv_obj_align(s_level_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_level_fill, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_level_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_level_fill, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_level_fill, 0, LV_PART_MAIN);

    lv_obj_t *metric_wrap = lv_obj_create(meter_row);
    lv_obj_set_size(metric_wrap, lv_pct(36), 72);
    style_flat_panel(metric_wrap, 12, 2);
    lv_obj_set_style_pad_all(metric_wrap, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(metric_wrap, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(metric_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(metric_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_peak_label = factory_ui_create_info_label(metric_wrap, "Peak --");
    s_noise_label = factory_ui_create_info_label(metric_wrap, "Noise --");
    s_clip_label = factory_ui_create_info_label(metric_wrap, "Clip --");

    lv_obj_t *button_row = create_transparent_row(panel);
    lv_obj_t *record_btn =
        factory_ui_create_action_button(button_row, "Record 3s", action_btn_event_cb, (void *)static_cast<intptr_t>(AudioAction::Record));
    lv_obj_set_width(record_btn, lv_pct(23));
    lv_obj_t *play_btn =
        factory_ui_create_action_button(button_row, "Playback", action_btn_event_cb, (void *)static_cast<intptr_t>(AudioAction::Playback));
    lv_obj_set_width(play_btn, lv_pct(23));
    lv_obj_t *loop_btn =
        factory_ui_create_action_button(button_row, "Safe Loop", action_btn_event_cb, (void *)static_cast<intptr_t>(AudioAction::Loopback));
    lv_obj_set_width(loop_btn, lv_pct(23));
    lv_obj_t *stop_btn =
        factory_ui_create_action_button(button_row, "Stop", action_btn_event_cb, (void *)static_cast<intptr_t>(AudioAction::Stop));
    lv_obj_set_width(stop_btn, lv_pct(23));

    s_status_label = factory_ui_create_info_label(panel, "Audio page ready.");
    lv_obj_set_width(s_status_label, lv_pct(100));

    s_footer_label = factory_ui_create_info_label(panel, "");
    lv_obj_set_width(s_footer_label, lv_pct(100));
    lv_obj_set_style_text_align(s_footer_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    refresh_audio_ui();
}

static void entry_audio(void)
{
    if (factory_audio_init()) {
        factory_audio_start_monitor();
    }
    refresh_audio_ui();
    if (s_refresh_timer == nullptr) {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, kUiRefreshMs, nullptr);
    }
}

static void exit_audio(void)
{
    if (s_refresh_timer != nullptr) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    factory_audio_stop();
}

static void destroy_audio(void)
{
    s_codec_pill = {};
    s_mic_pill = {};
    s_speaker_pill = {};
    s_record_pill = {};
    s_result_badge = nullptr;
    s_result_label = nullptr;
    s_wave_stage = nullptr;
    s_wave_midline = nullptr;
    memset(s_wave_bars, 0, sizeof(s_wave_bars));
    s_level_fill = nullptr;
    s_level_label = nullptr;
    s_peak_label = nullptr;
    s_noise_label = nullptr;
    s_clip_label = nullptr;
    s_status_label = nullptr;
    s_footer_label = nullptr;
}

static scr_lifecycle_t s_audio_lifecycle = {
    .create = create_audio,
    .entry = entry_audio,
    .exit = exit_audio,
    .destroy = destroy_audio,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_audio_lifecycle(void)
{
    return &s_audio_lifecycle;
}
