# usb_host_hid_basic

## What This Example Does

This example runs the LilyGo T5-P4 E-Paper as a USB HID host and verifies that a USB keyboard or mouse works through the board OTG port.

After a USB HID device is attached, the example:

- detects HID keyboard and mouse devices
- prints keyboard input to the serial monitor
- prints mouse movement and button state to the serial monitor
- leaves other HID devices as raw report dumps for debugging

The demo stays intentionally small. It does not use Wi-Fi, EPD rendering, LVGL, a Web UI, or the onboard ESP32-C6 hosted path.

## Hardware Required

- A LilyGo T5-P4 E-Paper board
- A USB keyboard, USB mouse, or a USB hub with HID devices attached
- Correct OTG cabling between the board and the target USB device

The ESP32-P4 USB Host OTG pins are:

- `USB_DM = GPIO49`
- `USB_DP = GPIO50`

## Dependencies

This example uses the official `usb_host_hid` component through the ESP-IDF component manager.

## Build and Flash

```bash
idf.py -C examples/usb_host_hid_basic set-target esp32p4
idf.py -C examples/usb_host_hid_basic build
idf.py -C examples/usb_host_hid_basic -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) usb_host_hid_basic: USB HID host example for LilyGo T5-P4 E-Paper
I (...) usb_host_hid_basic: Waiting for USB HID keyboard or mouse to be connected...
I (...) usb_host_hid_basic: HID device connected, protocol 'KEYBOARD', subclass=1
Keyboard
hello
Mouse
X:    120  Y:    -30  Buttons[L:o M:  R: ]
```

## Notes

- The example is aimed at boot-protocol USB keyboards and mice.
- Boot mouse parsing covers X/Y movement and up to three buttons.
- If a different HID device is connected, the example keeps running and prints raw HID reports instead of trying to treat it as CDC.
- `CONFIG_USB_HOST_HUBS_SUPPORTED=y` is enabled so common hub scenarios can be tested directly.
