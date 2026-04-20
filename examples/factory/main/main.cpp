#include "factory_display.h"
#include "factory_touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "factory_port.h"
#include "ui.h"

namespace {

static const char *TAG = "factory_main";

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "factory example start");

    if (!factory_display_init()) {
        ESP_LOGE(TAG, "display init failed, entering safe idle loop");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (!factory_touch_init()) {
        ESP_LOGW(TAG, "touch init failed, continuing in display-only mode");
    }

    factory_port_init();
    factory_ui_init();

    while (true) {
        factory_display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
