## esp-hosted-c6-Slave

参考 [esp32_p4_function_ev_board.md](https://github.com/espressif/esp-hosted-mcu/blob/ee11e08cd4f0b8f5f5c4c4138fa890697562d3cf/docs/esp32_p4_function_ev_board.md) 如何使用 esp-hosted；

参考 https://github.com/espressif/esp-hosted-mcu/tree/main/slave

## esp-hosted-slave

ESP32-C6-MINI 需要编译下载 esp-hosted-mcu 项目中的 slave 程序才可以通过 SDIO 与 ESP32-P4 通信，编译改项目需要的 esp-idf 版本 `version: ">=5.3" `

esp-hosted-mcu 编译步骤：

1. 克隆项目到本地：`git clone --recursive https://github.com/espressif/esp-hosted-mcu/tree/main`

2. 进入 `esp-hosted-mcu/slave` 目录下面

3. 设置目标芯片：`idf.py set-target esp32c6`

4. 编译项目：`idf.py build`

5. 编译完成后下载：`idf.py -p <your_port> flash`

## 下载方式：

1. 使用 usb 转串口工具与 ESP32-C6-MINI 模块连接；C6-MINI 与串口的接线为 TX-RX、RX-TX、GND-GND、BOOT-GND；C6-MINI 的 BOOT 引脚需要接地进入下载模式；

2. 给板子上电，esp32p4 也需要进入下载模式，避免 C6-MINI 在下载程序的被 esp32p4 干扰，导致下载失败；

3. 然后编译下载 esp-hosted-c6-slave 程序，下载完成后断开 C6-MINI 的 BOOT 与 GND 的连接，然后重新上电就可以了；