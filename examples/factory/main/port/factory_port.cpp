#include "factory_port.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "board_config.h"
#include "factory_battery.h"
#include "factory_display.h"
#include "factory_touch.h"
#include "factory_wifi.h"

namespace {

static constexpr const char *TAG = "factory_port";

static const factory_page_info_t kPageInfo[] = {
    {FACTORY_PAGE_CLOCK, "Clock", "Reserved for RTC and wall-clock bring-up.", "This page keeps the FastEPD demo entry but does not enable RTC logic in this factory build.", "PLACEHOLDER"},
    {FACTORY_PAGE_LORA, "LoRa", "Reserved for radio bring-up and loopback testing.", "LoRa transport is intentionally stubbed so the menu skeleton stays stable while the port layer remains thin.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_SD, "SD Card", "Browse the SD card from /sdcard and inspect file details.", "This build mounts the card on demand, lists the current directory, and lets you inspect files without modifying card contents.", "READ_ONLY"},
    {FACTORY_PAGE_WIFI, "WiFi", "FastEPD WiFi scan UI is live with a stubbed backend.", "The layout and refresh loop were migrated from the demo, while actual networking is still disabled in this factory build.", "UI_ONLY"},
    {FACTORY_PAGE_BATTERY, "Battery", "Basic BQ25896/BQ27220 charging and gauge telemetry.", "The factory battery page reuses the power dashboard bring-up path, but trims the UI down to essential VBUS, charge state, SOC, current, voltage, temperature, and capacity data.", "LIVE_BASIC"},
    {FACTORY_PAGE_AUDIO, "Audio", "ES8311 microphone, speaker, recording, playback, and loopback showcase.", "This page validates the codec over shared I2C, streams mic samples over I2S, records a short PCM sample, draws a waveform, and plays the capture through the speaker path.", "LIVE_AUDIO"},
    {FACTORY_PAGE_HDMI, "HDMI", "LT8912B HDMI bridge showcase and diagnostics.", "The factory HDMI page drives an external monitor with RGB888 test patterns, motion diagnostics, and later camera/audio/SD showcase modes.", "LIVE_HDMI"},
    {FACTORY_PAGE_GPS, "GPS", "Reserved for GNSS lock and serial diagnostics.", "This page preserves the demo navigation slot without starting any GNSS task.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_SHUTDOWN, "Shutdown", "Safe placeholder for a future power-off flow.", "Real shutdown is intentionally disabled in this migration build.", "SAFE_PLACEHOLDER"},
    {FACTORY_PAGE_SLEEP, "Sleep", "Safe placeholder for a future deep-sleep flow.", "Real sleep entry is intentionally disabled in this migration build.", "SAFE_PLACEHOLDER"},
};

static factory_runtime_info_t s_runtime_info = {
    .app_name = "Factory Demo",
    .app_version = "local",
    .target = CONFIG_IDF_TARGET,
    .width = 0,
    .height = 0,
    .touch_ready = false,
    .display_mode = "",
    .touch_status = "",
    .boot_mode = "",
};

static constexpr gpio_num_t kBacklightGpios[] = {
    (gpio_num_t)FACTORY_BACKLIGHT_IO11_GPIO,
    (gpio_num_t)FACTORY_BACKLIGHT_IO12_GPIO,
};

static bool s_backlight_gpio_ready = false;
static bool s_backlight_enabled[sizeof(kBacklightGpios) / sizeof(kBacklightGpios[0])] = {};

static bool ensure_backlight_gpio_ready()
{
    if (s_backlight_gpio_ready) {
        return true;
    }

    gpio_config_t config = {};
    config.intr_type = GPIO_INTR_DISABLE;
    config.mode = GPIO_MODE_OUTPUT;
    config.pin_bit_mask = (1ULL << FACTORY_BACKLIGHT_IO11_GPIO) | (1ULL << FACTORY_BACKLIGHT_IO12_GPIO);
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configure backlight gpios failed: %s", esp_err_to_name(err));
        return false;
    }

    for (size_t i = 0; i < sizeof(kBacklightGpios) / sizeof(kBacklightGpios[0]); ++i) {
        gpio_set_level(kBacklightGpios[i], 0);
        s_backlight_enabled[i] = false;
    }

    s_backlight_gpio_ready = true;
    return true;
}

static int backlight_index_to_offset(uint8_t index)
{
    if (index < 1U || index > (uint8_t)(sizeof(kBacklightGpios) / sizeof(kBacklightGpios[0]))) {
        return -1;
    }
    return (int)index - 1;
}

static const char *runtime_touch_status()
{
    return factory_touch_is_ready() ? "Touch ready" : "Touch unavailable";
}

static void refresh_runtime_info()
{
    const factory_display_mode_info_t *display_info = factory_display_get_mode_info();

    s_runtime_info.width = display_info->width;
    s_runtime_info.height = display_info->height;
    s_runtime_info.display_mode = display_info->mode_summary;
    s_runtime_info.touch_ready = factory_touch_is_ready();
    s_runtime_info.touch_status = runtime_touch_status();
    s_runtime_info.boot_mode = "Home menu";
}

}  // namespace

extern "C" void factory_port_init(void)
{
    factory_wifi_init();
    factory_battery_init();
    (void)ensure_backlight_gpio_ready();
    refresh_runtime_info();
}

extern "C" const factory_runtime_info_t *factory_port_get_runtime_info(void)
{
    refresh_runtime_info();
    return &s_runtime_info;
}

extern "C" const factory_page_info_t *factory_port_get_page_info(factory_page_id_t page_id)
{
    for (size_t i = 0; i < sizeof(kPageInfo) / sizeof(kPageInfo[0]); ++i) {
        if (kPageInfo[i].page_id == page_id) {
            return &kPageInfo[i];
        }
    }
    return nullptr;
}

extern "C" bool factory_port_get_backlight_enabled(uint8_t index)
{
    const int offset = backlight_index_to_offset(index);
    if (offset < 0 || !ensure_backlight_gpio_ready()) {
        return false;
    }
    return s_backlight_enabled[offset];
}

extern "C" void factory_port_set_backlight_enabled(uint8_t index, bool enabled)
{
    const int offset = backlight_index_to_offset(index);
    if (offset < 0 || !ensure_backlight_gpio_ready()) {
        return;
    }

    const esp_err_t err = gpio_set_level(kBacklightGpios[offset], enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set backlight gpio %d failed: %s", (int)kBacklightGpios[offset], esp_err_to_name(err));
        return;
    }

    s_backlight_enabled[offset] = enabled;
}
