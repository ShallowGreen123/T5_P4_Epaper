#include "factory_display.h"
#include "factory_touch.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "factory_port.h"
#include "ui.h"

namespace {

static const char *TAG = "factory_main";

#ifndef FACTORY_BOOT_BUTTON_GPIO
#define FACTORY_BOOT_BUTTON_GPIO 35
#endif

constexpr TickType_t kBootButtonDebounceTicks = pdMS_TO_TICKS(40);

static bool s_boot_button_enabled = false;
static bool s_boot_button_stable_pressed = false;
static bool s_boot_button_last_sample_pressed = false;
static TickType_t s_boot_button_last_change_tick = 0;

static void init_boot_button_full_refresh()
{
#if FACTORY_BOOT_BUTTON_GPIO >= 0
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << FACTORY_BOOT_BUTTON_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BOOT button GPIO%d init failed: %s", FACTORY_BOOT_BUTTON_GPIO, esp_err_to_name(err));
        return;
    }

    s_boot_button_enabled = true;
    s_boot_button_last_sample_pressed = gpio_get_level((gpio_num_t)FACTORY_BOOT_BUTTON_GPIO) == 0;
    s_boot_button_stable_pressed = s_boot_button_last_sample_pressed;
    s_boot_button_last_change_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "BOOT button full refresh enabled on GPIO%d", FACTORY_BOOT_BUTTON_GPIO);
#else
    ESP_LOGI(TAG, "BOOT button full refresh disabled");
#endif
}

static void poll_boot_button_full_refresh()
{
#if FACTORY_BOOT_BUTTON_GPIO >= 0
    if (!s_boot_button_enabled) {
        return;
    }

    const bool pressed = gpio_get_level((gpio_num_t)FACTORY_BOOT_BUTTON_GPIO) == 0;
    const TickType_t now = xTaskGetTickCount();

    if (pressed != s_boot_button_last_sample_pressed) {
        s_boot_button_last_sample_pressed = pressed;
        s_boot_button_last_change_tick = now;
        return;
    }

    if (pressed == s_boot_button_stable_pressed ||
        (now - s_boot_button_last_change_tick) < kBootButtonDebounceTicks) {
        return;
    }

    s_boot_button_stable_pressed = pressed;
    if (pressed) {
        ESP_LOGI(TAG, "BOOT button pressed: request full refresh");
        factory_display_request_full_refresh();
    }
#endif
}

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
    init_boot_button_full_refresh();

    while (true) {
        poll_boot_button_full_refresh();
        factory_display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
