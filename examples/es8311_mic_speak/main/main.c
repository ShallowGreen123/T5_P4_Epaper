#include "esp_log.h"

#include "board_es8311.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ES8311 microphone loopback example");
    es8311_start();
}
