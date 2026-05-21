# usb_host_hid_basic

## 示例功能

这个示例让 LilyGo T5-P4 E-Paper 作为 USB HID Host 运行，用来验证 USB 键盘和鼠标是否能通过板子的 OTG 口正常工作。

当 USB HID 设备接入后，示例会：

- 识别 USB 键盘和 USB 鼠标
- 将键盘输入直接打印到串口监视器
- 将鼠标位移和按键状态打印到串口监视器
- 对其他 HID 设备输出原始 report，方便调试

本示例只保留最基础的 HID Host 验证流程，不接入 Wi-Fi、EPD、LVGL、Web UI，也不依赖板载 ESP32-C6 的 hosted 功能。

## 硬件要求

- 一块 LilyGo T5-P4 E-Paper 开发板
- 一个 USB 键盘、USB 鼠标，或者接有 HID 设备的 USB Hub
- 正确的 USB OTG 连接线

ESP32-P4 的 USB Host OTG 引脚为：

- `USB_DM = GPIO49`
- `USB_DP = GPIO50`

## 依赖

本示例通过 ESP-IDF Component Manager 使用官方 `usb_host_hid` 组件。

## 编译和烧录

```bash
idf.py -C examples/usb_host_hid_basic set-target esp32p4
idf.py -C examples/usb_host_hid_basic build
idf.py -C examples/usb_host_hid_basic -p <PORT> flash monitor
```

## 预期日志

你应该能看到类似下面的输出：

```text
I (...) usb_host_hid_basic: USB HID host example for LilyGo T5-P4 E-Paper
I (...) usb_host_hid_basic: Waiting for USB HID keyboard or mouse to be connected...
I (...) usb_host_hid_basic: HID device connected, protocol 'KEYBOARD', subclass=1
Keyboard
hello
Mouse
X:    120  Y:    -30  Buttons[L:o M:  R: ]
```

## 说明

- 本示例重点支持 boot-protocol USB 键盘和鼠标。
- Boot 鼠标解析范围为 X/Y 位移和最多三个按键。
- 如果接入的是其他 HID 设备，示例不会再误按 CDC 处理，而是继续运行并输出原始 HID report。
- 已启用 `CONFIG_USB_HOST_HUBS_SUPPORTED=y`，可直接验证常见的 USB Hub 场景。
