#include "ui_screens.h"

#include <cstdio>
#include <cstring>

#include "sdkconfig.h"

#include "factory_assets.h"
#include "factory_battery.h"
#include "lvgl.h"
#include "ui_theme.h"

namespace {

struct metric_card_t {
    lv_obj_t *card;
    lv_obj_t *value;
};

constexpr uint32_t kBatteryRefreshMs = CONFIG_FACTORY_BATTERY_REFRESH_MS;
constexpr size_t kChargerMetricCount = 5;
constexpr size_t kGaugeMetricCount = 5;

static lv_obj_t *s_mode_label = nullptr;
static lv_obj_t *s_summary_label = nullptr;
static lv_timer_t *s_refresh_timer = nullptr;
static metric_card_t s_charger_metrics[kChargerMetricCount] = {};
static metric_card_t s_gauge_metrics[kGaugeMetricCount] = {};

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

static metric_card_t create_metric_card(lv_obj_t *parent, const char *title)
{
    metric_card_t metric = {};
    metric.card = factory_ui_create_value_card(parent, title, "--");
    lv_obj_set_width(metric.card, lv_pct(100));
    metric.value = lv_obj_get_child(metric.card, 1);
    return metric;
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

static void set_metric_value(metric_card_t metric, const char *text)
{
    if (metric.value == nullptr || text == nullptr) {
        return;
    }
    set_text_if_changed(metric.value, text);
}

static void refresh_battery_ui()
{
    char text[96];
    char temp_text[64];

    factory_battery_refresh();
    const factory_battery_state_t *state = factory_battery_get_state();

    set_text_if_changed(s_mode_label, primary_mode_text(state));
    set_text_if_changed(s_summary_label, factory_battery_get_status_text());

    if (state == nullptr) {
        for (size_t i = 0; i < kChargerMetricCount; ++i) {
            set_metric_value(s_charger_metrics[i], "--");
        }
        for (size_t i = 0; i < kGaugeMetricCount; ++i) {
            set_metric_value(s_gauge_metrics[i], "--");
        }
        return;
    }

    if (!state->charger_ready) {
        for (size_t i = 0; i < kChargerMetricCount; ++i) {
            set_metric_value(s_charger_metrics[i], "Not found");
        }
    } else if (!state->charger_read_ok) {
        for (size_t i = 0; i < kChargerMetricCount; ++i) {
            set_metric_value(s_charger_metrics[i], "Read error");
        }
    } else {
        std::snprintf(text,
                      sizeof(text),
                      "%s | %u mV",
                      state->vbus_connected ? "USB in" : "Battery",
                      state->vbus_voltage_mv);
        set_metric_value(s_charger_metrics[0], text);

        std::snprintf(text,
                      sizeof(text),
                      "%s | %s",
                      factory_battery_charge_status_name(state->charger_status),
                      state->charge_enabled ? "enabled" : "disabled");
        set_metric_value(s_charger_metrics[1], text);

        std::snprintf(text,
                      sizeof(text),
                      "%u mA / %u mV",
                      state->charge_current_ma,
                      state->charge_voltage_mv);
        set_metric_value(s_charger_metrics[2], text);

        std::snprintf(text,
                      sizeof(text),
                      "IIN %u mA | VSYS %u mV",
                      state->input_limit_ma,
                      state->system_voltage_mv);
        set_metric_value(s_charger_metrics[3], text);

        std::snprintf(text,
                      sizeof(text),
                      "VBAT %u mV | Term %u mA",
                      state->battery_voltage_mv,
                      state->termination_current_ma);
        set_metric_value(s_charger_metrics[4], text);
    }

    if (!state->gauge_ready) {
        for (size_t i = 0; i < kGaugeMetricCount; ++i) {
            set_metric_value(s_gauge_metrics[i], "Not found");
        }
    } else if (!state->gauge_read_ok) {
        for (size_t i = 0; i < kGaugeMetricCount; ++i) {
            set_metric_value(s_gauge_metrics[i], "Read error");
        }
    } else {
        std::snprintf(text,
                      sizeof(text),
                      "%s | 0x%04X",
                      factory_battery_gauge_state_name(state->gauge_state),
                      state->battery_status_raw);
        set_metric_value(s_gauge_metrics[0], text);

        std::snprintf(text, sizeof(text), "%u%% / %u%%", state->soc_percent, state->soh_percent);
        set_metric_value(s_gauge_metrics[1], text);

        std::snprintf(text,
                      sizeof(text),
                      "%d mA | avg %d mA",
                      (int)state->current_ma,
                      (int)state->average_current_ma);
        set_metric_value(s_gauge_metrics[2], text);

        std::snprintf(text,
                      sizeof(text),
                      "%u / %u mAh",
                      state->remaining_capacity_mah,
                      state->full_capacity_mah);
        set_metric_value(s_gauge_metrics[3], text);

        factory_battery_format_temperature(temp_text, sizeof(temp_text), state->temperature_dk);
        std::snprintf(text, sizeof(text), "%s | %u mV", temp_text, state->gauge_voltage_mv);
        set_metric_value(s_gauge_metrics[4], text);
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_battery_ui();
}

static lv_obj_t *create_metric_column(lv_obj_t *parent,
                                      const char *title,
                                      metric_card_t *metrics,
                                      size_t metric_count,
                                      const char *const *metric_titles)
{
    lv_obj_t *column = lv_obj_create(parent);
    lv_obj_set_width(column, lv_pct(48));
    lv_obj_set_flex_grow(column, 1);
    style_transparent_container(column);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title_label = lv_label_create(column);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, FACTORY_FONT_UI_WIFI_STATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_black(), LV_PART_MAIN);

    for (size_t i = 0; i < metric_count; ++i) {
        metrics[i] = create_metric_card(column, metric_titles[i]);
    }

    return column;
}

static void create_battery(lv_obj_t *parent)
{
    static const char *kChargerMetricTitles[kChargerMetricCount] = {
        "VBUS",
        "Charge",
        "Config",
        "Input / System",
        "Battery",
    };
    static const char *kGaugeMetricTitles[kGaugeMetricCount] = {
        "State",
        "SOC / SOH",
        "Current",
        "Capacity",
        "Temperature",
    };

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
    style_transparent_container(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    (void)create_metric_column(row, "Charger", s_charger_metrics, kChargerMetricCount, kChargerMetricTitles);
    (void)create_metric_column(row, "Gauge", s_gauge_metrics, kGaugeMetricCount, kGaugeMetricTitles);

    refresh_battery_ui();
}

static void entry_battery(void)
{
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
}

static void destroy_battery(void)
{
    s_mode_label = nullptr;
    s_summary_label = nullptr;
    s_refresh_timer = nullptr;
    std::memset(s_charger_metrics, 0, sizeof(s_charger_metrics));
    std::memset(s_gauge_metrics, 0, sizeof(s_gauge_metrics));
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
