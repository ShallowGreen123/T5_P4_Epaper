# 代码模板

在为 `ESP32 + PlatformIO + Arduino` 生成演示工程时，优先使用这些模板。

## 1. `lib/demo_core/include/demo_log.h`

```cpp
#pragma once
#include <Arduino.h>

#ifndef DEMO_BAUDRATE
#define DEMO_BAUDRATE 115200
#endif

#define DEMO_BEGIN(tag)      Serial.printf("[DEMO][BEGIN] %s\r\n", tag)
#define DEMO_END(tag)        Serial.printf("[DEMO][END] %s\r\n", tag)
#define DEMO_STEP(msg)       Serial.printf("[DEMO][STEP] %s\r\n", msg)
#define DEMO_INFO(fmt, ...)  Serial.printf("[DEMO][INFO] " fmt "\r\n", ##__VA_ARGS__)
#define DEMO_WARN(fmt, ...)  Serial.printf("[DEMO][WARN] " fmt "\r\n", ##__VA_ARGS__)
#define DEMO_ERROR(fmt, ...) Serial.printf("[DEMO][ERROR] " fmt "\r\n", ##__VA_ARGS__)
#define DEMO_PASS(tag)       Serial.printf("[DEMO][PASS] %s\r\n", tag)
#define DEMO_FAIL(tag)       Serial.printf("[DEMO][FAIL] %s\r\n", tag)
```

## 2. `lib/demo_core/include/demo_check.h`

```cpp
#pragma once
#include "demo_log.h"

#define DEMO_CHECK_TRUE(cond, msg)                 \
    do {                                           \
        if (!(cond)) {                             \
            DEMO_ERROR("CHECK FAIL: %s", msg);     \
            return false;                          \
        }                                          \
    } while (0)
```

## 3. `lib/demo_core/include/demo_config.h`

```cpp
#pragma once

namespace DemoConfig
{
    static constexpr unsigned long SERIAL_WAIT_MS = 1500;
    static constexpr unsigned long DEFAULT_TIMEOUT_MS = 10000;

    static constexpr const char *WIFI_SSID = "YOUR_WIFI_SSID";
    static constexpr const char *WIFI_PASS = "YOUR_WIFI_PASS";
}
```

## 4. `lib/demo_core/include/demo_pins.h`

```cpp
#pragma once

namespace DemoPins
{
    static constexpr int PIN_LED = 2;
    static constexpr int PIN_RST = -1;
    static constexpr int PIN_INT = -1;
}
```

## 5. Example `main.cpp`

```cpp
#include <Arduino.h>
#include "demo_log.h"
#include "demo_check.h"
#include "demo_config.h"
#include "demo_pins.h"

static const char *TAG = "wifi_scan";

static bool run_demo()
{
    DEMO_STEP("初始化模块");
    delay(100);

    DEMO_STEP("执行核心演示流程");
    DEMO_CHECK_TRUE(true, "占位检查");

    DEMO_INFO("请在这里补充真实的硬件操作和结果判断");
    return true;
}

void setup()
{
    Serial.begin(DEMO_BAUDRATE);
    delay(DemoConfig::SERIAL_WAIT_MS);

    DEMO_BEGIN(TAG);
    if (run_demo()) {
        DEMO_PASS(TAG);
    } else {
        DEMO_FAIL(TAG);
    }
    DEMO_END(TAG);
}

void loop() {}
```

## 6. `platformio.ini` 片段

```ini
[env]
platform = espressif32
framework = arduino
monitor_speed = 115200

[env:esp32s3_base]
board = esp32-s3-devkitc-1
build_flags =
    -Ilib/demo_core/include

[env:wifi_scan]
extends = env:esp32s3_base
build_src_filter =
    +<examples/wifi_scan/>
    +<lib/demo_core/src/>
```

## 7. 生成代码时的硬性要求
- 一个 example 只对应一个核心演示目标
- 公共配置和引脚不要散落在多个 example 内
- 每个 example 都要输出清晰串口日志
- 所有等待都要带超时或最大重试次数
- 对外设依赖、网络依赖、供电依赖做显式提示
- 如果资料不足，只生成骨架并标注 `TODO`
