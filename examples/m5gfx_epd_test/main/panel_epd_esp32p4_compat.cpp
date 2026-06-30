#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4
// Upstream m5stack/m5gfx gates Panel_EPD.cpp to ESP32-S3 only, but this
// example uses the same Bus_EPD path on ESP32-P4. Rebuild that translation
// unit locally so the managed dependency can stay untouched.
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "lgfx/v1/platforms/esp32/Panel_EPD.cpp"
#undef CONFIG_IDF_TARGET_ESP32S3
#endif
