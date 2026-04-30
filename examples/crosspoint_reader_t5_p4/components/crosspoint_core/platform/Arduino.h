#pragma once

#include "Print.h"
#include "WString.h"

#include <algorithm>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef PROGMEM
#define PROGMEM
#endif

inline unsigned long millis()
{
    return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}

inline unsigned long micros()
{
    return static_cast<unsigned long>(esp_timer_get_time());
}

inline void delay(unsigned long ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

class EspCompat {
public:
    uint32_t getFreeHeap() const { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }
};

extern EspCompat ESP;
