使用 Arduino 作为 esp-idf 的组件使用；

1、从 esp-idf 的例程中复制 hello_world 例程到自己的项目中，进入 hello_world 例程，添加组件 `idf.py add-dependency "espressif/arduino-esp32^3.2.0"`

2、新建一个文件 `sdkconfig.defaults` 将下面的内容粘贴上去；
~~~cmake
CONFIG_IDF_TARGET="esp32p4"

CONFIG_ARDUINO_VARIANT="esp32p4"

CONFIG_FREERTOS_HZ=1000
~~~

3、将 `hello_world_main.c` 重命名为 `hello_world_main.cpp`; 将 CmakeList 中的 `.c` 改为 `.cpp`;

4、更改测试代码
~~~cpp
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

#include "Arduino.h"

extern "C" void app_main(void)
{
    Serial.begin(115200);
    Serial.printf("Hello world!\n");
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    Serial.printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    Serial.printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        Serial.printf("Get flash size failed");
        return;
    }

    Serial.printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    Serial.printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    for (int i = 10; i >= 0; i--) {
        Serial.printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    Serial.printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
~~~
5、最后使用 `idf.py build` 编译代码；


