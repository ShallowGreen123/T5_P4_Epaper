#include "ui_screens.h"

#include <cstdio>
#include <cstring>

#include "sdkconfig.h"

#include "factory_assets.h"
#include "factory_battery.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_theme.h"

namespace {

enum : size_t {
    GAUGE_LINE_VBUS = 0,
    GAUGE_LINE_STATE,
    GAUGE_LINE_SOC_FCC_SOH,
    GAUGE_LINE_TEMP,
    GAUGE_LINE_AVG_I,
    GAUGE_LINE_VOLT,
    GAUGE_LINE_CHG_V,
    GAUGE_LINE_TAP_I,
    GAUGE_LINE_FINISHED,
    GAUGE_LINE_REM_FULL,
    GAUGE_LINE_DBG,
    GAUGE_LINE_COUNT,
};

enum : size_t {
    CHARGER_LINE_VBUS = 0,
    CHARGER_LINE_VBUS_MV,
    CHARGER_LINE_VSYS_MV,
    CHARGER_LINE_VBAT_MV,
    CHARGER_LINE_VREG_MV,
    CHARGER_LINE_ICHG_MA,
    CHARGER_LINE_PRE_MA,
    CHARGER_LINE_CHG_ADC_MA,
    CHARGER_LINE_STATUS,
    CHARGER_LINE_COUNT,
};

constexpr uint32_t kBatteryRefreshMs = CONFIG_FACTORY_BATTERY_REFRESH_MS;

static lv_obj_t *s_root = nullptr;
static lv_obj_t *s_mode_label = nullptr;
static lv_obj_t *s_summary_label = nullptr;
static lv_timer_t *s_refresh_timer = nullptr;
static lv_obj_t *s_gauge_lines[GAUGE_LINE_COUNT] = {};
static lv_obj_t *s_charger_lines[CHARGER_LINE_COUNT] = {};
static lv_obj_t *s_shutdown_btn = nullptr;
static lv_obj_t *s_shutdown_prompt_overlay = nullptr;
static lv_obj_t *s_shutdown_prompt_card = nullptr;
static lv_obj_t *s_shutdown_prompt_text = nullptr;

static void refresh_battery_ui();

static void style_transparent_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_readout_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 6, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == nullptr || text == nullptr) {
        return;
    }

    const char *current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static bool shutdown_prompt_visible()
{
    return s_shutdown_prompt_overlay != nullptr &&
           !lv_obj_has_flag(s_shutdown_prompt_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_shutdown_button()
{
    if (s_shutdown_btn == nullptr) {
        return;
    }

    const factory_battery_state_t *state = factory_battery_get_state();
    const bool shutdown_allowed =
        state != nullptr &&
        state->charger_ready &&
        state->charger_read_ok &&
        !state->vbus_connected &&
        !shutdown_prompt_visible();

    if (shutdown_allowed) {
        lv_obj_clear_state(s_shutdown_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_shutdown_btn, LV_STATE_DISABLED);
    }
}

static lv_obj_t *create_line_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    return label;
}

static lv_obj_t *create_readout_panel(lv_obj_t *parent,
                                      const char *title,
                                      lv_obj_t **lines,
                                      size_t line_count)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_width(panel, lv_pct(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_readout_panel(panel);

    lv_obj_t *title_label = lv_label_create(panel);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_black(), LV_PART_MAIN);

    for (size_t i = 0; i < line_count; ++i) {
        lines[i] = create_line_label(panel, "--");
    }

    return panel;
}

static void hide_shutdown_prompt()
{
    if (s_shutdown_prompt_overlay == nullptr) {
        return;
    }

    lv_obj_add_flag(s_shutdown_prompt_overlay, LV_OBJ_FLAG_HIDDEN);
    refresh_shutdown_button();
}

static void show_shutdown_prompt()
{
    if (s_shutdown_prompt_overlay == nullptr) {
        return;
    }

    lv_obj_clear_flag(s_shutdown_prompt_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_shutdown_prompt_overlay);
    refresh_shutdown_button();
}

static const char *primary_mode_text(const factory_battery_state_t *state)
{
    if (state == nullptr) {
        return "Unavailable";
    }

    if (state->gauge_ready && state->gauge_read_ok) {
        return factory_battery_gauge_state_name(state->gauge_state);
    }

    if (state->charge_done) {
        return "Full";
    }

    if (state->charging) {
        return "Charging";
    }

    if (state->charger_ready || state->gauge_ready) {
        return state->vbus_connected ? "Standby" : "Discharge";
    }

    return "Unavailable";
}

static void set_line_value(lv_obj_t *label, const char *text)
{
    set_text_if_changed(label, text);
}

static void set_gauge_placeholder(const char *placeholder)
{
    char line[96];

    std::snprintf(line, sizeof(line), "VBUS        : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_VBUS], line);
    std::snprintf(line, sizeof(line), "State       : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_STATE], line);
    std::snprintf(line, sizeof(line), "SOC/FCC/SOH : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_SOC_FCC_SOH], line);
    std::snprintf(line, sizeof(line), "Temp        : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_TEMP], line);
    std::snprintf(line, sizeof(line), "AvgI        : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_AVG_I], line);
    std::snprintf(line, sizeof(line), "Volt        : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_VOLT], line);
    std::snprintf(line, sizeof(line), "ChgV        : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_CHG_V], line);
    std::snprintf(line, sizeof(line), "TapI        : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_TAP_I], line);
    std::snprintf(line, sizeof(line), "Finished    : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_FINISHED], line);
    std::snprintf(line, sizeof(line), "Rem/Full    : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_REM_FULL], line);
    std::snprintf(line, sizeof(line), "Dbg         : %s", placeholder);
    set_line_value(s_gauge_lines[GAUGE_LINE_DBG], line);
}

static void set_charger_placeholder(const char *placeholder)
{
    char line[96];

    std::snprintf(line, sizeof(line), "VBUS        : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_VBUS], line);
    std::snprintf(line, sizeof(line), "VBUS mV     : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_VBUS_MV], line);
    std::snprintf(line, sizeof(line), "VSYS        : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_VSYS_MV], line);
    std::snprintf(line, sizeof(line), "VBAT        : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_VBAT_MV], line);
    std::snprintf(line, sizeof(line), "VREG        : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_VREG_MV], line);
    std::snprintf(line, sizeof(line), "ICHG        : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_ICHG_MA], line);
    std::snprintf(line, sizeof(line), "PRE         : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_PRE_MA], line);
    std::snprintf(line, sizeof(line), "CHG ADC     : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_CHG_ADC_MA], line);
    std::snprintf(line, sizeof(line), "Status      : %s", placeholder);
    set_line_value(s_charger_lines[CHARGER_LINE_STATUS], line);
}

static void refresh_battery_ui()
{
    char line[128];
    char temp_text[64];

    factory_battery_refresh();
    const factory_battery_state_t *state = factory_battery_get_state();

    set_text_if_changed(s_mode_label, primary_mode_text(state));
    set_text_if_changed(s_summary_label, factory_battery_get_status_text());

    if (state == nullptr) {
        set_gauge_placeholder("--");
        set_charger_placeholder("--");
        refresh_shutdown_button();
        return;
    }

    if (!state->gauge_ready) {
        set_gauge_placeholder("Not found");
    } else if (!state->gauge_read_ok) {
        set_gauge_placeholder("Read error");
    } else {
        std::snprintf(line, sizeof(line), "VBUS        : %s", state->vbus_connected ? "IN" : "OUT");
        set_line_value(s_gauge_lines[GAUGE_LINE_VBUS], line);

        std::snprintf(line,
                      sizeof(line),
                      "State       : %s",
                      factory_battery_gauge_state_name(state->gauge_state));
        set_line_value(s_gauge_lines[GAUGE_LINE_STATE], line);

        std::snprintf(line,
                      sizeof(line),
                      "SOC/FCC/SOH : %u%%/%u/%u%%",
                      state->soc_percent,
                      state->full_capacity_mah,
                      state->soh_percent);
        set_line_value(s_gauge_lines[GAUGE_LINE_SOC_FCC_SOH], line);

        factory_battery_format_temperature(temp_text, sizeof(temp_text), state->temperature_dk);
        std::snprintf(line, sizeof(line), "Temp        : %s", temp_text);
        set_line_value(s_gauge_lines[GAUGE_LINE_TEMP], line);

        std::snprintf(line, sizeof(line), "AvgI        : %d mA", (int)state->average_current_ma);
        set_line_value(s_gauge_lines[GAUGE_LINE_AVG_I], line);

        std::snprintf(line, sizeof(line), "Volt        : %u mV", state->gauge_voltage_mv);
        set_line_value(s_gauge_lines[GAUGE_LINE_VOLT], line);

        std::snprintf(line, sizeof(line), "ChgV        : %u mV", state->gauge_charge_voltage_mv);
        set_line_value(s_gauge_lines[GAUGE_LINE_CHG_V], line);

        std::snprintf(line, sizeof(line), "TapI        : %u mA", state->gauge_taper_current_ma);
        set_line_value(s_gauge_lines[GAUGE_LINE_TAP_I], line);

        std::snprintf(line, sizeof(line), "Finished    : %s", state->charge_done ? "Yes" : "No");
        set_line_value(s_gauge_lines[GAUGE_LINE_FINISHED], line);

        std::snprintf(line,
                      sizeof(line),
                      "Rem/Full    : %u/%u mAh",
                      state->remaining_capacity_mah,
                      state->full_capacity_mah);
        set_line_value(s_gauge_lines[GAUGE_LINE_REM_FULL], line);

        std::snprintf(line,
                      sizeof(line),
                      "Dbg         : BFC=%u GFC=%u TCA=%u AI=%u V=%u",
                      state->gauge_battery_full_flag ? 1u : 0u,
                      state->gauge_gauging_full_flag ? 1u : 0u,
                      state->gauge_taper_flag ? 1u : 0u,
                      state->gauge_charge_inhibit ? 1u : 0u,
                      state->gauge_voltage_mv);
        set_line_value(s_gauge_lines[GAUGE_LINE_DBG], line);
    }

    if (!state->charger_ready) {
        set_charger_placeholder("Not found");
    } else if (!state->charger_read_ok) {
        set_charger_placeholder("Read error");
    } else {
        std::snprintf(line, sizeof(line), "VBUS        : %s", state->vbus_connected ? "IN" : "OUT");
        set_line_value(s_charger_lines[CHARGER_LINE_VBUS], line);

        std::snprintf(line, sizeof(line), "VBUS mV     : %u mV", state->vbus_voltage_mv);
        set_line_value(s_charger_lines[CHARGER_LINE_VBUS_MV], line);

        std::snprintf(line, sizeof(line), "VSYS        : %u mV", state->system_voltage_mv);
        set_line_value(s_charger_lines[CHARGER_LINE_VSYS_MV], line);

        std::snprintf(line, sizeof(line), "VBAT        : %u mV", state->battery_voltage_mv);
        set_line_value(s_charger_lines[CHARGER_LINE_VBAT_MV], line);

        std::snprintf(line, sizeof(line), "VREG        : %u mV", state->charge_voltage_mv);
        set_line_value(s_charger_lines[CHARGER_LINE_VREG_MV], line);

        std::snprintf(line, sizeof(line), "ICHG        : %u mA", state->charge_current_ma);
        set_line_value(s_charger_lines[CHARGER_LINE_ICHG_MA], line);

        std::snprintf(line, sizeof(line), "PRE         : %u mA", state->precharge_current_ma);
        set_line_value(s_charger_lines[CHARGER_LINE_PRE_MA], line);

        std::snprintf(line, sizeof(line), "CHG ADC     : %u mA", state->charger_adc_current_ma);
        set_line_value(s_charger_lines[CHARGER_LINE_CHG_ADC_MA], line);

        std::snprintf(line,
                      sizeof(line),
                      "Status      : %s / %s",
                      factory_battery_charge_status_name(state->charger_status),
                      state->charge_enabled ? "Enabled" : "Disabled");
        set_line_value(s_charger_lines[CHARGER_LINE_STATUS], line);
    }

    refresh_shutdown_button();
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_battery_ui();
}

static void shutdown_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_battery_state_t *state = factory_battery_get_state();
    if (state == nullptr || state->vbus_connected || !state->charger_ready || !state->charger_read_ok) {
        return;
    }

    show_shutdown_prompt();
}

static void shutdown_prompt_cancel_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    hide_shutdown_prompt();
    refresh_battery_ui();
}

static void shutdown_prompt_confirm_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const factory_battery_state_t *state = factory_battery_get_state();
    if (state == nullptr || state->vbus_connected || !state->charger_ready || !state->charger_read_ok) {
        hide_shutdown_prompt();
        refresh_battery_ui();
        return;
    }

    hide_shutdown_prompt();
    (void)factory_ui_request_power_off();
}

static void create_shutdown_prompt(lv_obj_t *parent)
{
    s_shutdown_prompt_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_shutdown_prompt_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_shutdown_prompt_overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_shutdown_prompt_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_shutdown_prompt_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_shutdown_prompt_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_shutdown_prompt_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_shutdown_prompt_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_shutdown_prompt_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    s_shutdown_prompt_card = lv_obj_create(s_shutdown_prompt_overlay);
    lv_obj_set_size(s_shutdown_prompt_card, lv_pct(86), LV_SIZE_CONTENT);
    lv_obj_align(s_shutdown_prompt_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_shutdown_prompt_card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_shutdown_prompt_card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_shutdown_prompt_card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_shutdown_prompt_card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_shutdown_prompt_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_shutdown_prompt_card, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_shutdown_prompt_card, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_shutdown_prompt_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_shutdown_prompt_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_shutdown_prompt_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_shutdown_prompt_card);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_text(title, "Shutdown");
    lv_obj_set_style_text_font(title, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_black(), LV_PART_MAIN);

    s_shutdown_prompt_text = lv_label_create(s_shutdown_prompt_card);
    lv_obj_set_width(s_shutdown_prompt_text, lv_pct(100));
    lv_label_set_long_mode(s_shutdown_prompt_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_shutdown_prompt_text, "Confirm shutdown? This works only on battery power.");
    lv_obj_set_style_text_font(s_shutdown_prompt_text, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_shutdown_prompt_text, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *btn_row = lv_obj_create(s_shutdown_prompt_card);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(btn_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel_btn =
        factory_ui_create_action_button(btn_row, "Cancel", shutdown_prompt_cancel_event_cb, nullptr);
    lv_obj_set_width(cancel_btn, lv_pct(48));

    lv_obj_t *confirm_btn =
        factory_ui_create_action_button(btn_row, "Confirm", shutdown_prompt_confirm_event_cb, nullptr);
    lv_obj_set_width(confirm_btn, lv_pct(48));
}

static void create_battery(lv_obj_t *parent)
{
    s_root = parent;
    factory_ui_apply_screen(parent);
    factory_ui_create_back_button(parent, "Battery");

    lv_obj_t *panel = factory_ui_create_content_panel(parent, 94, 92);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);

    s_mode_label = lv_label_create(panel);
    lv_obj_set_width(s_mode_label, lv_pct(100));
    lv_label_set_long_mode(s_mode_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_mode_label, "Battery");
    lv_obj_set_style_text_font(s_mode_label, FACTORY_FONT_UI_HOME_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_mode_label, lv_color_black(), LV_PART_MAIN);

    s_summary_label = lv_label_create(panel);
    lv_obj_set_width(s_summary_label, lv_pct(100));
    lv_label_set_long_mode(s_summary_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_summary_label, "Battery page ready.");
    lv_obj_set_style_text_font(s_summary_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_summary_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_transparent_container(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_VER);

    (void)create_readout_panel(row, "BQ27220", s_gauge_lines, GAUGE_LINE_COUNT);

    lv_obj_t *charger_panel = create_readout_panel(row, "BQ25896", s_charger_lines, CHARGER_LINE_COUNT);
    s_shutdown_btn = factory_ui_create_action_button(charger_panel, "Shutdown", shutdown_btn_event_cb, nullptr);
    lv_obj_set_width(s_shutdown_btn, lv_pct(100));

    create_shutdown_prompt(parent);

    refresh_battery_ui();
}

static void entry_battery(void)
{
    hide_shutdown_prompt();
    refresh_battery_ui();
    if (s_refresh_timer != nullptr) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    s_refresh_timer = lv_timer_create(refresh_timer_cb, kBatteryRefreshMs, nullptr);
}

static void exit_battery(void)
{
    if (s_refresh_timer != nullptr) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }

    hide_shutdown_prompt();
}

static void destroy_battery(void)
{
    s_root = nullptr;
    s_mode_label = nullptr;
    s_summary_label = nullptr;
    s_refresh_timer = nullptr;
    s_shutdown_btn = nullptr;
    s_shutdown_prompt_overlay = nullptr;
    s_shutdown_prompt_card = nullptr;
    s_shutdown_prompt_text = nullptr;
    std::memset(s_gauge_lines, 0, sizeof(s_gauge_lines));
    std::memset(s_charger_lines, 0, sizeof(s_charger_lines));
}

static scr_lifecycle_t s_battery_lifecycle = {
    .create = create_battery,
    .entry = entry_battery,
    .exit = exit_battery,
    .destroy = destroy_battery,
};

}  // namespace

extern "C" scr_lifecycle_t *factory_screen_battery_lifecycle(void)
{
    return &s_battery_lifecycle;
}
