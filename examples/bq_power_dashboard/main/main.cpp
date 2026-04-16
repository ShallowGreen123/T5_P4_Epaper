#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "lvgl.h"
#include <FastEPD.h>

#include "bq25896.h"
#include "bq25896_hal_esp_idf.h"
#include "bq27220.h"

namespace
{
constexpr char TAG[] = "bq_power_dash";

constexpr int DISP_WIDTH = 1440;
constexpr int DISP_HEIGHT = 720;
constexpr int DRAW_BUF_LINES = 40;
constexpr int DRAW_BUF_PIXELS = DISP_WIDTH * DRAW_BUF_LINES;
constexpr int FRAMEBUFFER_PITCH = (DISP_WIDTH + 7) / 8;
constexpr int PANEL_SPI_CLOCK_HZ = 40000000;
constexpr uint8_t PARTIAL_PASSES = 7;
constexpr uint8_t FULL_PASSES = 5;

constexpr uint16_t RECOVER_IINLIM_MA = 1000;
constexpr uint16_t RECOVER_ICHG_MA = 512;
constexpr uint16_t RECOVER_IPRECHG_MA = 128;
constexpr uint16_t RECOVER_ITERM_MA = 128;
constexpr uint16_t RECOVER_VREG_MV = 4208;
constexpr uint16_t RECOVER_SYSMIN_MV = 3300;
constexpr uint16_t RECOVER_CAPACITY_MAH = 1100;

constexpr int16_t CURRENT_THRESHOLD_MA = 20;

constexpr uint32_t LOCAL_REFRESH_MS = CONFIG_BQ_POWER_DASHBOARD_LOCAL_REFRESH_MS;
constexpr uint32_t FULL_REFRESH_MS = CONFIG_BQ_POWER_DASHBOARD_FULL_REFRESH_MS;

enum : size_t
{
    BQ25896_LINE_TITLE = 0,
    BQ25896_LINE_VBUS,
    BQ25896_LINE_VBUS_MV,
    BQ25896_LINE_VSYS_MV,
    BQ25896_LINE_VBAT_MV,
    BQ25896_LINE_VREG_MV,
    BQ25896_LINE_ICHG_MA,
    BQ25896_LINE_PRE_MA,
    BQ25896_LINE_ITERM_MA,
    BQ25896_LINE_CHG_ADC_MA,
    BQ25896_LINE_STATUS,
    BQ25896_LINE_COUNT
};

enum : size_t
{
    BQ27220_LINE_TITLE = 0,
    BQ27220_LINE_VBUS,
    BQ27220_LINE_STATE,
    BQ27220_LINE_SOC_FCC_SOH,
    BQ27220_LINE_TEMP,
    BQ27220_LINE_AVG_I,
    BQ27220_LINE_VOLT,
    BQ27220_LINE_CHARGE_VOLTAGE,
    BQ27220_LINE_TAPER_CURRENT,
    BQ27220_LINE_FULL,
    BQ27220_LINE_REM_FULL,
    BQ27220_LINE_COUNT
};

struct Bq25896Snapshot
{
    bool initialized;
    bool read_ok;
    bq25896_status_t status;
    bq25896_adc_t adc;
    bq25896_charge_config_t cfg;
};

struct Bq27220PanelData
{
    bool initialized;
    bool read_ok;
    bool vbus_connected;
    BQ27220State state;
    BQ27220Snapshot gauge;
};

struct DashboardUi
{
    lv_obj_t *screen;
    lv_obj_t *left_panel;
    lv_obj_t *right_panel;
    std::array<lv_obj_t *, BQ25896_LINE_COUNT> bq25896_labels;
    std::array<lv_obj_t *, BQ27220_LINE_COUNT> bq27220_labels;
};

static FASTEPD s_epaper;
static uint8_t *s_framebuffer = nullptr;
static int s_pending_y_min = DISP_HEIGHT;
static int s_pending_y_max = -1;
static bool s_force_full_refresh = true;
static bool s_display_started = false;

static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static bq25896_hal_esp_idf_ctx_t s_bq25896_hal_ctx = {};
static bq25896_t s_bq25896 = {};
static BQ27220 s_bq27220;

static bool s_bq25896_ready = false;
static bool s_bq27220_ready = false;
static DashboardUi s_ui = {};
static esp_timer_handle_t s_lvgl_tick_timer = nullptr;
static int64_t s_last_full_refresh_ms = 0;

static lv_color_t *s_draw_buf_1 = nullptr;
static lv_color_t *s_draw_buf_2 = nullptr;

static const char *bq25896_err_name(bq25896_err_t err)
{
    switch (err)
    {
        case BQ25896_OK:
            return "OK";
        case BQ25896_WARN_CLAMPED:
            return "WARN_CLAMPED";
        case BQ25896_ERR_NULL:
            return "ERR_NULL";
        case BQ25896_ERR_INVALID_ARG:
            return "ERR_INVALID_ARG";
        case BQ25896_ERR_I2C:
            return "ERR_I2C";
        case BQ25896_ERR_TIMEOUT:
            return "ERR_TIMEOUT";
        case BQ25896_ERR_DEVICE_ID:
            return "ERR_DEVICE_ID";
        case BQ25896_ERR_UNSUPPORTED:
            return "ERR_UNSUPPORTED";
        case BQ25896_ERR_NOT_INITIALIZED:
            return "ERR_NOT_INITIALIZED";
        default:
            return "ERR_UNKNOWN";
    }
}

static bool bq25896_apply_step(const char *step_name, bq25896_err_t err)
{
    if (BQ25896_FAILED(err))
    {
        ESP_LOGE(TAG, "BQ25896 %s failed: %s (%d)", step_name, bq25896_err_name(err), (int)err);
        return false;
    }

    if (err == BQ25896_WARN_CLAMPED)
    {
        ESP_LOGW(TAG, "BQ25896 %s clamped to register step", step_name);
    }

    return true;
}

static const char *charge_status_name(bq25896_charge_status_t status)
{
    switch (status)
    {
        case BQ25896_CHARGE_STATUS_NOT_CHARGING:
            return "IDLE";
        case BQ25896_CHARGE_STATUS_PRECHARGE:
            return "PRECHG";
        case BQ25896_CHARGE_STATUS_FAST_CHARGE:
            return "FAST";
        case BQ25896_CHARGE_STATUS_TERMINATION_DONE:
            return "DONE";
        default:
            return "UNKNOWN";
    }
}

static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *current = lv_label_get_text(label);

    if ((current == nullptr) || (strcmp(current, text) != 0))
    {
        lv_label_set_text(label, text);
    }
}

static void set_bq25896_placeholder(const std::array<lv_obj_t *, BQ25896_LINE_COUNT> &labels,
                                    const char *placeholder)
{
    char line[48];

    set_label_text_if_changed(labels[BQ25896_LINE_TITLE], "BQ25896");
    std::snprintf(line, sizeof(line), "VBUS     : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_VBUS], line);
    std::snprintf(line, sizeof(line), "VBUS mV  : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_VBUS_MV], line);
    std::snprintf(line, sizeof(line), "VSYS mV  : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_VSYS_MV], line);
    std::snprintf(line, sizeof(line), "VBAT mV  : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_VBAT_MV], line);
    std::snprintf(line, sizeof(line), "VREG mV  : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_VREG_MV], line);
    std::snprintf(line, sizeof(line), "ICHG mA  : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_ICHG_MA], line);
    std::snprintf(line, sizeof(line), "PRE mA   : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_PRE_MA], line);
    std::snprintf(line, sizeof(line), "ITerm mA : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_ITERM_MA], line);
    std::snprintf(line, sizeof(line), "CHG ADC  : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_CHG_ADC_MA], line);
    std::snprintf(line, sizeof(line), "Status   : %s", placeholder);
    set_label_text_if_changed(labels[BQ25896_LINE_STATUS], line);
}

static void set_bq27220_placeholder(const std::array<lv_obj_t *, BQ27220_LINE_COUNT> &labels,
                                    const char *placeholder)
{
    char line[48];

    set_label_text_if_changed(labels[BQ27220_LINE_TITLE], "BQ27220");
    std::snprintf(line, sizeof(line), "VBUS     : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_VBUS], line);
    std::snprintf(line, sizeof(line), "State    : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_STATE], line);
    std::snprintf(line, sizeof(line), "SOC/FCC  : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_SOC_FCC_SOH], line);
    std::snprintf(line, sizeof(line), "Temp     : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_TEMP], line);
    std::snprintf(line, sizeof(line), "AvgI     : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_AVG_I], line);
    std::snprintf(line, sizeof(line), "Volt     : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_VOLT], line);
    std::snprintf(line, sizeof(line), "ChargeV  : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_CHARGE_VOLTAGE], line);
    std::snprintf(line, sizeof(line), "TaperCur : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_TAPER_CURRENT], line);
    std::snprintf(line, sizeof(line), "Full?    : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_FULL], line);
    std::snprintf(line, sizeof(line), "Rem/Full : %s", placeholder);
    set_label_text_if_changed(labels[BQ27220_LINE_REM_FULL], line);
}

static void create_panel_labels(lv_obj_t *panel, lv_coord_t width, lv_coord_t height, lv_obj_t **labels, size_t count)
{
    static const lv_coord_t LEFT_PAD = 24;
    static const lv_coord_t TOP_PAD = 20;
    static const lv_coord_t ROW_HEIGHT = 42;

    for (size_t i = 0; i < count; ++i)
    {
        labels[i] = lv_label_create(panel);
        lv_obj_set_width(labels[i], width - (LEFT_PAD * 2));
        lv_obj_set_pos(labels[i], LEFT_PAD, TOP_PAD + (lv_coord_t)(ROW_HEIGHT * i));
        lv_obj_set_style_text_color(labels[i], lv_color_black(), 0);
        lv_obj_set_style_text_align(labels[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_CLIP);
    }

    (void)height;
}

static void create_dashboard_ui()
{
    static const lv_coord_t MARGIN = 24;
    static const lv_coord_t PANEL_GAP = 24;
    const lv_coord_t panel_width = (DISP_WIDTH - (MARGIN * 2) - PANEL_GAP) / 2;
    const lv_coord_t panel_height = DISP_HEIGHT - (MARGIN * 2);

    s_ui.screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_ui.screen, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.screen, 0, 0);
    lv_obj_set_style_pad_all(s_ui.screen, 0, 0);

    s_ui.left_panel = lv_obj_create(s_ui.screen);
    lv_obj_clear_flag(s_ui.left_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_ui.left_panel, MARGIN, MARGIN);
    lv_obj_set_size(s_ui.left_panel, panel_width, panel_height);
    lv_obj_set_style_radius(s_ui.left_panel, 0, 0);
    lv_obj_set_style_border_width(s_ui.left_panel, 2, 0);
    lv_obj_set_style_border_color(s_ui.left_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_color(s_ui.left_panel, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_ui.left_panel, 0, 0);

    s_ui.right_panel = lv_obj_create(s_ui.screen);
    lv_obj_clear_flag(s_ui.right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_ui.right_panel, MARGIN + panel_width + PANEL_GAP, MARGIN);
    lv_obj_set_size(s_ui.right_panel, panel_width, panel_height);
    lv_obj_set_style_radius(s_ui.right_panel, 0, 0);
    lv_obj_set_style_border_width(s_ui.right_panel, 2, 0);
    lv_obj_set_style_border_color(s_ui.right_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_color(s_ui.right_panel, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_ui.right_panel, 0, 0);

    create_panel_labels(s_ui.left_panel, panel_width, panel_height, s_ui.bq25896_labels.data(), s_ui.bq25896_labels.size());
    create_panel_labels(s_ui.right_panel, panel_width, panel_height, s_ui.bq27220_labels.data(), s_ui.bq27220_labels.size());

    lv_obj_set_style_text_font(s_ui.bq25896_labels[BQ25896_LINE_TITLE], LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_font(s_ui.bq27220_labels[BQ27220_LINE_TITLE], LV_FONT_DEFAULT, 0);

    set_bq25896_placeholder(s_ui.bq25896_labels, "INIT");
    set_bq27220_placeholder(s_ui.bq27220_labels, "INIT");

    lv_scr_load(s_ui.screen);
}

static void begin_monochrome_flush()
{
    s_pending_y_min = DISP_HEIGHT;
    s_pending_y_max = -1;
}

static void note_flush_rows(int y1, int y2)
{
    s_pending_y_min = std::min(s_pending_y_min, y1);
    s_pending_y_max = std::max(s_pending_y_max, y2);
}

static void flush_rows_to_epd()
{
    if (s_force_full_refresh || !s_display_started)
    {
        s_epaper.fullUpdate(CLEAR_SLOW, true);
        s_force_full_refresh = false;
        s_display_started = true;
    }
    else if ((s_pending_y_min >= 0) && (s_pending_y_max >= s_pending_y_min))
    {
        s_epaper.partialUpdate(true, s_pending_y_min, s_pending_y_max);
    }

    begin_monochrome_flush();
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t clipped_x1 = std::max<int32_t>(0, area->x1);
    const int32_t clipped_y1 = std::max<int32_t>(0, area->y1);
    const int32_t clipped_x2 = std::min<int32_t>(DISP_WIDTH - 1, area->x2);
    const int32_t clipped_y2 = std::min<int32_t>(DISP_HEIGHT - 1, area->y2);

    if ((clipped_x1 > clipped_x2) || (clipped_y1 > clipped_y2))
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    const int32_t width = area->x2 - area->x1 + 1;

    for (int32_t y = clipped_y1; y <= clipped_y2; ++y)
    {
        for (int32_t x = clipped_x1; x <= clipped_x2; ++x)
        {
            const int32_t src_x = x - area->x1;
            const int32_t src_y = y - area->y1;
            const lv_color_t pixel = color_p[(src_y * width) + src_x];
            const uint8_t red = LV_COLOR_GET_R(pixel);
            const uint8_t green = LV_COLOR_GET_G(pixel);
            const uint8_t blue = LV_COLOR_GET_B(pixel);
            const uint8_t red_8 = (uint8_t)((red << 3) | (red >> 2));
            const uint8_t green_8 = (uint8_t)((green << 2) | (green >> 4));
            const uint8_t blue_8 = (uint8_t)((blue << 3) | (blue >> 2));
            const uint16_t gray = (uint16_t)(((red_8 * 76) + (green_8 * 150) + (blue_8 * 30)) >> 8);
            const bool is_white = gray >= 128;
            uint8_t &dst = s_framebuffer[(y * FRAMEBUFFER_PITCH) + (x >> 3)];
            const uint8_t mask = (uint8_t)(0x80u >> (x & 0x7));

            if (is_white)
            {
                dst = (uint8_t)(dst | mask);
            }
            else
            {
                dst = (uint8_t)(dst & (uint8_t)(~mask));
            }
        }
    }

    note_flush_rows(clipped_y1, clipped_y2);

    if (lv_disp_flush_is_last(disp_drv))
    {
        flush_rows_to_epd();
    }

    lv_disp_flush_ready(disp_drv);
}

static void init_lvgl()
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;
    static esp_timer_create_args_t tick_timer_args = {};

    lv_init();

    s_draw_buf_1 = static_cast<lv_color_t *>(
        heap_caps_calloc(DRAW_BUF_PIXELS, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_draw_buf_2 = static_cast<lv_color_t *>(
        heap_caps_calloc(DRAW_BUF_PIXELS, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if ((s_draw_buf_1 == nullptr) || (s_draw_buf_2 == nullptr))
    {
        ESP_LOGE(TAG, "LVGL draw buffer allocation failed");
        abort();
    }

    tick_timer_args.callback = &lv_tick_cb;
    tick_timer_args.name = "lvgl_tick";
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, 5000));

    lv_disp_draw_buf_init(&draw_buf, s_draw_buf_1, s_draw_buf_2, DRAW_BUF_PIXELS);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_WIDTH;
    disp_drv.ver_res = DISP_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);
}

static bool init_epaper_panel()
{
    const int init_rc = s_epaper.initPanel(BB_PANEL_LILYGO_T5P4, PANEL_SPI_CLOCK_HZ);
    if (init_rc != BBEP_SUCCESS)
    {
        ESP_LOGE(TAG, "epaper initPanel failed: %d", init_rc);
        return false;
    }

    const int mode_rc = s_epaper.setMode(BB_MODE_1BPP);
    if (mode_rc != BBEP_SUCCESS)
    {
        ESP_LOGE(TAG, "epaper setMode(1bpp) failed: %d", mode_rc);
        return false;
    }

    s_epaper.setPasses(PARTIAL_PASSES, FULL_PASSES);
    s_framebuffer = s_epaper.currentBuffer();
    if (s_framebuffer == nullptr)
    {
        ESP_LOGE(TAG, "epaper currentBuffer is null");
        return false;
    }

    s_epaper.fillScreen(BBEP_WHITE);
    begin_monochrome_flush();
    ESP_LOGI(TAG, "epaper panel ready");
    return true;
}

static esp_err_t init_i2c_bus()
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_BQ_POWER_DASHBOARD_I2C_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_BQ_POWER_DASHBOARD_I2C_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

static bool init_bq25896_device()
{
    bq25896_config_t config = {};
    bq25896_err_t rc = bq25896_get_default_config(&config);

    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(TAG, "bq25896_get_default_config failed: %s", bq25896_err_name(rc));
        return false;
    }

    rc = bq25896_hal_esp_idf_get_default_ctx(&s_bq25896_hal_ctx);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(TAG, "bq25896_hal_esp_idf_get_default_ctx failed: %s", bq25896_err_name(rc));
        return false;
    }

    s_bq25896_hal_ctx.scl_speed_hz = CONFIG_BQ_POWER_DASHBOARD_I2C_FREQ_HZ;
    s_bq25896_hal_ctx.timeout_ms = 100;

    rc = bq25896_hal_esp_idf_ctx_init(
        &s_bq25896_hal_ctx,
        s_i2c_bus,
        static_cast<uint8_t>(CONFIG_BQ_POWER_DASHBOARD_BQ25896_ADDR));
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGW(TAG, "BQ25896 not found at 0x%02X: %s",
                 CONFIG_BQ_POWER_DASHBOARD_BQ25896_ADDR,
                 bq25896_err_name(rc));
        return false;
    }

    rc = bq25896_hal_esp_idf_make_hal(&s_bq25896_hal_ctx, &config.hal);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGE(TAG, "bq25896_hal_esp_idf_make_hal failed: %s", bq25896_err_name(rc));
        return false;
    }

    config.i2c_addr_7bit = static_cast<uint8_t>(CONFIG_BQ_POWER_DASHBOARD_BQ25896_ADDR);
    config.reset_registers_on_init = true;
    config.exit_hiz_on_init = true;
    config.adc_mode = BQ25896_ADC_MODE_CONTINUOUS;
    config.watchdog = BQ25896_WATCHDOG_DISABLED;

    rc = bq25896_init(&s_bq25896, &config);
    if (BQ25896_FAILED(rc))
    {
        ESP_LOGW(TAG, "BQ25896 init failed: %s", bq25896_err_name(rc));
        return false;
    }

    if (!bq25896_apply_step("disable_otg", bq25896_disable_otg(&s_bq25896)) ||
        !bq25896_apply_step("enable_battery_power_path", bq25896_enable_battery_power_path(&s_bq25896)) ||
        !bq25896_apply_step("set_input_limit_ma", bq25896_set_input_limit_ma(&s_bq25896, RECOVER_IINLIM_MA)) ||
        !bq25896_apply_step("set_charge_current_ma", bq25896_set_charge_current_ma(&s_bq25896, RECOVER_ICHG_MA)) ||
        !bq25896_apply_step("set_precharge_current_ma", bq25896_set_precharge_current_ma(&s_bq25896, RECOVER_IPRECHG_MA)) ||
        !bq25896_apply_step("set_termination_current_ma", bq25896_set_termination_current_ma(&s_bq25896, RECOVER_ITERM_MA)) ||
        !bq25896_apply_step("set_charge_voltage_mv", bq25896_set_charge_voltage_mv(&s_bq25896, RECOVER_VREG_MV)) ||
        !bq25896_apply_step("set_system_min_voltage_mv", bq25896_set_system_min_voltage_mv(&s_bq25896, RECOVER_SYSMIN_MV)) ||
        !bq25896_apply_step("enable_charge", bq25896_enable_charge(&s_bq25896)))
    {
        return false;
    }

    ESP_LOGI(TAG, "BQ25896 ready at 0x%02X", CONFIG_BQ_POWER_DASHBOARD_BQ25896_ADDR);
    return true;
}

static bool init_bq27220_device()
{
    if (!s_bq27220.begin(
            s_i2c_bus,
            static_cast<uint8_t>(CONFIG_BQ_POWER_DASHBOARD_BQ27220_ADDR),
            CONFIG_BQ_POWER_DASHBOARD_I2C_FREQ_HZ))
    {
        ESP_LOGW(TAG, "BQ27220 begin failed at 0x%02X", CONFIG_BQ_POWER_DASHBOARD_BQ27220_ADDR);
        return false;
    }

    (void)s_bq27220.setDefaultCapacity(RECOVER_CAPACITY_MAH);
    if (!s_bq27220.init())
    {
        ESP_LOGW(TAG, "BQ27220 init failed");
        return false;
    }

    ESP_LOGI(TAG, "BQ27220 ready at 0x%02X", CONFIG_BQ_POWER_DASHBOARD_BQ27220_ADDR);
    return true;
}

static bool sample_bq25896(Bq25896Snapshot *snapshot)
{
    if (snapshot == nullptr)
    {
        return false;
    }

    std::memset(snapshot, 0, sizeof(*snapshot));
    snapshot->initialized = s_bq25896_ready;
    if (!s_bq25896_ready)
    {
        return false;
    }

    const bq25896_err_t status_rc = bq25896_read_status(&s_bq25896, &snapshot->status);
    const bq25896_err_t adc_rc = bq25896_read_adc(&s_bq25896, &snapshot->adc);
    const bq25896_err_t cfg_rc = bq25896_read_charge_config(&s_bq25896, &snapshot->cfg);

    snapshot->read_ok = BQ25896_SUCCEEDED(status_rc) && BQ25896_SUCCEEDED(adc_rc) && BQ25896_SUCCEEDED(cfg_rc);
    return snapshot->read_ok;
}

static bool sample_bq27220(const Bq25896Snapshot *charger, Bq27220PanelData *snapshot)
{
    if (snapshot == nullptr)
    {
        return false;
    }

    std::memset(snapshot, 0, sizeof(*snapshot));
    snapshot->initialized = s_bq27220_ready;
    snapshot->vbus_connected = (charger != nullptr) && charger->read_ok &&
                               (charger->status.vbus_good || charger->status.power_good);

    if (!s_bq27220_ready)
    {
        return false;
    }

    snapshot->read_ok = s_bq27220.readSnapshot(&snapshot->gauge);

    if (!snapshot->read_ok)
    {
        return false;
    }

    snapshot->state =
        BQ27220::classifyState(&snapshot->gauge, snapshot->vbus_connected, CURRENT_THRESHOLD_MA);

    return true;
}

static void update_bq25896_ui(const Bq25896Snapshot &snapshot)
{
    char line[96];
    char status_line[80];

    if (!snapshot.initialized)
    {
        set_bq25896_placeholder(s_ui.bq25896_labels, "NOT FOUND");
        return;
    }

    if (!snapshot.read_ok)
    {
        set_bq25896_placeholder(s_ui.bq25896_labels, "READ ERR");
        return;
    }

    std::snprintf(line, sizeof(line), "VBUS     : %s", (snapshot.status.vbus_good || snapshot.status.power_good) ? "IN" : "OUT");
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_VBUS], line);
    std::snprintf(line, sizeof(line), "VBUS mV  : %u", snapshot.adc.vbus_voltage_mv);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_VBUS_MV], line);
    std::snprintf(line, sizeof(line), "VSYS mV  : %u", snapshot.adc.system_voltage_mv);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_VSYS_MV], line);
    std::snprintf(line, sizeof(line), "VBAT mV  : %u", snapshot.adc.battery_voltage_mv);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_VBAT_MV], line);
    std::snprintf(line, sizeof(line), "VREG mV  : %u", snapshot.cfg.charge_voltage_mv);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_VREG_MV], line);
    std::snprintf(line, sizeof(line), "ICHG mA  : %u", snapshot.cfg.charge_current_ma);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_ICHG_MA], line);
    std::snprintf(line, sizeof(line), "PRE mA   : %u", snapshot.cfg.precharge_current_ma);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_PRE_MA], line);
    std::snprintf(line, sizeof(line), "ITerm mA : %u", snapshot.cfg.termination_current_ma);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_ITERM_MA], line);
    std::snprintf(line, sizeof(line), "CHG ADC  : %u", snapshot.adc.charge_current_ma);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_CHG_ADC_MA], line);

    std::snprintf(status_line,
                  sizeof(status_line),
                  "%s%s%s",
                  charge_status_name(snapshot.status.charge_status),
                  snapshot.status.vindpm_active ? " VINDPM" : "",
                  snapshot.status.iindpm_active ? " IINDPM" : "");
    std::snprintf(line, sizeof(line), "Status   : %s", status_line);
    set_label_text_if_changed(s_ui.bq25896_labels[BQ25896_LINE_STATUS], line);
}

static void format_temperature_line(char *buffer, size_t buffer_size, uint16_t temp_dk)
{
    const int32_t temp_deci_c = (int32_t)temp_dk - 2731;
    const int32_t abs_deci_c = std::abs(temp_deci_c);

    std::snprintf(buffer,
                  buffer_size,
                  "Temp     : %s%" PRId32 ".%" PRId32 " degC",
                  (temp_deci_c < 0) ? "-" : "",
                  abs_deci_c / 10,
                  abs_deci_c % 10);
}

static void update_bq27220_ui(const Bq27220PanelData &snapshot)
{
    char line[96];

    if (!snapshot.initialized)
    {
        set_bq27220_placeholder(s_ui.bq27220_labels, "NOT FOUND");
        return;
    }

    if (!snapshot.read_ok)
    {
        set_bq27220_placeholder(s_ui.bq27220_labels, "READ ERR");
        return;
    }

    std::snprintf(line, sizeof(line), "VBUS     : %s", snapshot.vbus_connected ? "IN" : "OUT");
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_VBUS], line);
    std::snprintf(line, sizeof(line), "State    : %s", BQ27220::stateName(snapshot.state));
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_STATE], line);
    std::snprintf(line, sizeof(line), "SOC/FCC/SOH : %u%% / %u / %u%%",
                  snapshot.gauge.soc,
                  snapshot.gauge.fcc_mah,
                  snapshot.gauge.soh_percent);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_SOC_FCC_SOH], line);
    format_temperature_line(line, sizeof(line), snapshot.gauge.temperature_dk);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_TEMP], line);
    std::snprintf(line, sizeof(line), "AvgI     : %d mA", (int)snapshot.gauge.average_current_ma);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_AVG_I], line);
    std::snprintf(line, sizeof(line), "Volt     : %u mV", snapshot.gauge.voltage_mv);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_VOLT], line);
    std::snprintf(line, sizeof(line), "ChargeV  : %u mV", RECOVER_VREG_MV);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_CHARGE_VOLTAGE], line);
    std::snprintf(line, sizeof(line), "TaperCur : %u mA", RECOVER_ITERM_MA);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_TAPER_CURRENT], line);
    std::snprintf(line, sizeof(line), "Full?    : %s", snapshot.gauge.full ? "YES" : "NO");
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_FULL], line);
    std::snprintf(line, sizeof(line), "Rem/Full : %u / %u mAh",
                  snapshot.gauge.remaining_capacity_mah,
                  snapshot.gauge.fcc_mah);
    set_label_text_if_changed(s_ui.bq27220_labels[BQ27220_LINE_REM_FULL], line);
}

static void dashboard_refresh()
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    Bq25896Snapshot charger = {};
    Bq27220PanelData gauge = {};

    (void)sample_bq25896(&charger);
    (void)sample_bq27220(&charger, &gauge);

    update_bq25896_ui(charger);
    update_bq27220_ui(gauge);

    if ((now_ms - s_last_full_refresh_ms) >= (int64_t)FULL_REFRESH_MS)
    {
        s_force_full_refresh = true;
        s_last_full_refresh_ms = now_ms;
        lv_obj_invalidate(lv_scr_act());
    }
}

static void dashboard_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    dashboard_refresh();
}

static bool init_dashboard()
{
    bbepSetI2CMasterBus(s_i2c_bus);
    if (!init_epaper_panel())
    {
        return false;
    }

    init_lvgl();
    create_dashboard_ui();
    s_last_full_refresh_ms = esp_timer_get_time() / 1000;
    dashboard_refresh();
    lv_timer_create(dashboard_refresh_timer_cb, LOCAL_REFRESH_MS, nullptr);
    return true;
}

} /* namespace */

extern "C" void app_main(void)
{
    ESP_LOGI(TAG,
             "Power dashboard start: SDA=%d SCL=%d FREQ=%d BQ25896=0x%02X BQ27220=0x%02X",
             CONFIG_BQ_POWER_DASHBOARD_I2C_SDA,
             CONFIG_BQ_POWER_DASHBOARD_I2C_SCL,
             CONFIG_BQ_POWER_DASHBOARD_I2C_FREQ_HZ,
             CONFIG_BQ_POWER_DASHBOARD_BQ25896_ADDR,
             CONFIG_BQ_POWER_DASHBOARD_BQ27220_ADDR);

    ESP_ERROR_CHECK(init_i2c_bus());

    s_bq25896_ready = init_bq25896_device();
    s_bq27220_ready = init_bq27220_device();

    if (!init_dashboard())
    {
        ESP_LOGE(TAG, "dashboard init failed");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }

    while (true)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
