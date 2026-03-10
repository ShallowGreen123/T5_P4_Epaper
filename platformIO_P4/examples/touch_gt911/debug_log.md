
src_dir = examples/touch_gt911 

我使用这个驱动屏幕触摸，出现下面错误，触摸使用的GT911，地址是0x5D，有时候可以正常工作，但大部分时候会出现下面这个错误，如何解决这个问题;

~~~
ESP-ROM:esp32p4-eco2-20240710
Build:Jul 10 2024
rst:0x7 (HP_SYS_HP_WDT_RESET),boot:0x30f (SPI_FAST_FLASH_BOOT)
SPI mode:DIO, clock div:1
load:0x4ff33ce0,len:0x1078
load:0x4ff29ed0,len:0xb80
load:0x4ff2cbd0,len:0x344c
entry 0x4ff29ed0
[    11][I][esp32-hal-psram.c:106] psramAddToHeap(): PSRAM added to the heap.
[    25][I][SensorCommon.tpp:65] begin(): Using Arduino Wire interface.
[    31][I][esp32-hal-i2c-ng.c:112] i2cInit(): Initializing I2C Master: num=0 sda=7 scl=8 freq=100000
[    45][I][SensorCommon.tpp:65] begin(): Using Arduino Wire interface.
[    52][W][Wire.cpp:300] begin(): Bus already started in Master Mode.
[    58][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[    67][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[    77][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[    86][E][esp32-hal-i2c-ng.c:321] i2cRead(): i2c_master_receive failed: [259] ESP_ERR_INVALID_STATE
[    95][E][Wire.cpp:527] requestFrom(): i2cRead returned Error 259
[  1101][E][esp32-hal-i2c-ng.c:321] i2cRead(): i2c_master_receive failed: [259] ESP_ERR_INVALID_STATE
[  1110][E][Wire.cpp:527] requestFrom(): i2cRead returned Error 259
[  2116][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  2125][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  2134][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  2144][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  2153][E][esp32-hal-i2c-ng.c:321] i2cRead(): i2c_master_receive failed: [259] ESP_ERR_INVALID_STATE
[  2162][E][Wire.cpp:527] requestFrom(): i2cRead returned Error 259
[  3168][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3177][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3186][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3196][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3205][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3214][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3224][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3233][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3242][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3251][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3261][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3270][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3279][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
[  3289][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
Failed to find GT911 - check your wiring!
~~~

✅根因

- 你的日志同时出现 `Wire.begin(): Bus already started...` 和 `ESP_ERR_INVALID_STATE(259)`，在 `ESP32-P4（esp32-hal-i2c-ng.c）`上，重复对同一条 I2C 总线 `Wire.begin()` 很容易把驱动搞到“非法状态”。
- `touch_gt911` 里原来没有正确通过 `XL9555` 控制 GT911 的 `RST`，也没有让驱动执行 `RST+INT` 的选址复位流程，导致大多数启动时 GT911 不在 0x5D 上响应（甚至一直没起来）。

**我已在工程里做的修复**

- 避免 ESP32 上重复 `Wire.begin()`：`lib/SensorLib/src/SensorCommon.tpp:86`（用 `Wire.getClock()==0` 判断总线是否已初始化）。
- 让 GT911 的 `RST` 通过 `XL9555(IO0)` 可控，并由驱动拉 `INT` 选定 0x5D，同时把 I2C 提升到 400k：

`examples/touch_gt911/touch_gt911.ino:63`、
`examples/touch_gt911/touch_gt911.ino:174`、
`examples/touch_gt911/touch_gt911.ino:179`。

你怎么验证

- 重新编译烧录后，I2C 扫描里应能看到 `0x5D`；并且串口里不再刷 `ESP_ERR_INVALID_STATE(259)`。
- 如果仍偶发找不到：先把 `Wire.setClock(400000)` 改回 100000 试试（线长/上拉偏弱时 400k 会不稳），再检查 `SDA/SCL` 上拉、电源是否稳定、以及 `GT911 RST` 确实接在 `XL9555` 的 `IO0(BOARD_PCA_00_T_RST)` 上。