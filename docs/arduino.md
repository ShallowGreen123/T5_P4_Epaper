使用 Arduino 作为 esp-idf 的组件使用；

1、从 esp-idf 的例程中复制 hello_world 例程到自己的项目中，进入 hello_world 例程，添加组件 `idf.py add-dependency "espressif/arduino-esp32^3.2.0"`

2、新建一个文件 `sdkconfig.defaults` 将下面的内容粘贴上去；
~~~cmake
CONFIG_IDF_TARGET="esp32p4"

CONFIG_ARDUINO_VARIANT="esp32p4"
#CONFIG_AUTOSTART_ARDUINO=y

CONFIG_FREERTOS_HZ=1000
~~~

`CONFIG_AUTOSTART_ARDUINO` 为是否使用 Arduino 格式；

3、将 `hello_world_main.c` 重命名为 `hello_world_main.cpp`; 将 CmakeList 中的 `.c` 改为 `.cpp`;

4、更改测试代码

没有使能 CONFIG_AUTOSTART_ARDUINO 变量

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
    while(1) {
        Serial.printf("Hello world!\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

~~~

使能 CONFIG_AUTOSTART_ARDUINO 变量

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

void setup(void)
{
    Serial.begin(115200);
}

void loop(void)
{
    Serial.printf("Hello world!\n");
    delay(1000);
}

~~~


5、最后使用 `idf.py build` 编译代码；


