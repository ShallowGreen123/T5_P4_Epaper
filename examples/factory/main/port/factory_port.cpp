#include "factory_port.h"

#include <stdio.h>

#include "factory_display.h"
#include "factory_touch.h"

namespace {

static const factory_placeholder_info_t kPlaceholderPages[] = {
    {FACTORY_PAGE_BATTERY, "Battery", "Reserved for future BQ25896/BQ27220 integration.", "This page currently shows a stable factory placeholder only.", "MOCK_READY"},
    {FACTORY_PAGE_WIFI, "WiFi", "Reserved for future ESP32-C6 or scan workflow integration.", "Networking is intentionally not initialized in factory v1.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_SD, "SD Card", "Reserved for future SD card health and read/write checks.", "The UI flow exists now so the real driver can slot in later.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_GPS, "GPS", "Reserved for future GNSS lock and serial diagnostics.", "Factory v1 keeps this page as a mock endpoint.", "NOT_IMPLEMENTED"},
    {FACTORY_PAGE_LORA, "LoRa", "Reserved for future radio bring-up and loopback diagnostics.", "The screen structure is ready for a real port-layer backend.", "NOT_IMPLEMENTED"},
};

static factory_runtime_info_t s_runtime_info = {
    .app_name = "Factory Example",
    .app_version = "v1",
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

}  // namespace

extern "C" void factory_port_init(void)
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

extern "C" const factory_runtime_info_t *factory_port_get_runtime_info(void)
{
    s_runtime_info.touch_ready = factory_touch_is_ready();
    s_runtime_info.touch_status = runtime_touch_status();
    return &s_runtime_info;
}

extern "C" const factory_placeholder_info_t *factory_port_get_placeholder_page(factory_page_id_t page_id)
{
    for (size_t i = 0; i < sizeof(kPlaceholderPages) / sizeof(kPlaceholderPages[0]); ++i) {
        if (kPlaceholderPages[i].page_id == page_id) {
            return &kPlaceholderPages[i];
        }
    }
    return nullptr;
}
