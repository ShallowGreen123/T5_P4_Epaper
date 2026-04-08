#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "SensorLib.h"

void pinMode(uint32_t gpio, uint8_t mode)
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << gpio;
    config.intr_type = GPIO_INTR_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&config));
}

void digitalWrite(uint32_t gpio, uint8_t level)
{
    gpio_set_level((gpio_num_t)gpio, level);
}

int digitalRead(uint32_t gpio)
{
    return gpio_get_level((gpio_num_t)gpio);
}
