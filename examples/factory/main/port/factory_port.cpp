#include "factory_port.h"

#include "factory_display.h"
#include "factory_touch.h"

namespace {

static const factory_page_info_t kPageInfo[] = {
    {FACTORY_PAGE_CLOCK, "Clock", "Reserved for RTC and wall-clock bring-up.", "This page keeps the FastEPD demo entry but does not enable RTC logic in this factory build.", "PLACEHOLDER"},
    {FACTORY_PAGE_LORA, "LoRa", "Reserved for radio bring-up and loopback testing.", "LoRa transport is intentionally stubbed so the menu skeleton stays stable while the port layer remains thin.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_SD, "SD Card", "Reserved for SD card mount and read/write diagnostics.", "The page is ready for a future storage probe, but this build keeps it as a lightweight placeholder.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_WIFI, "WiFi", "Reserved for ESP32-C6 or scan workflow integration.", "Networking is intentionally not initialized in this factory build.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_BATTERY, "Battery", "Reserved for BQ25896/BQ27220 integration.", "Power IC access is not enabled yet, so the page currently reports placeholder state only.", "PLACEHOLDER"},
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
#ifdef CONFIG_FACTORY_BOOT_TOUCH_DIAGNOSTICS
    s_runtime_info.boot_mode = "Touch diagnostics";
#else
    s_runtime_info.boot_mode = "Home menu";
#endif
}

}  // namespace

extern "C" void factory_port_init(void)
{
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
