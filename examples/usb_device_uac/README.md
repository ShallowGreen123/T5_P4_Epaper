# usb_device_uac

## What This Example Does

This example turns the board into a USB Audio Class device. A connected host can send audio to the board speaker, capture audio from the onboard microphone, and control mute and volume from the USB side.

## Prerequisites

- A LilyGo T5-P4 board with the onboard `ES8311` audio codec path available.
- A USB data connection from the board's USB OTG port to a host computer.
- The default board support package configuration for the audio path.
- Optional: change channel counts, sample rate, VID, PID, or product strings in `idf.py menuconfig`.

## Build and Flash

```bash
idf.py -C examples/usb_device_uac set-target esp32p4
idf.py -C examples/usb_device_uac build
idf.py -C examples/usb_device_uac -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) usb_device_uac: UAC Device Start, Version: <major>.<minor>.<patch>
I (...) usb_device_uac: USB mounted
Speaker interface 1-<alt> opened
Microphone interface 2-<alt> opened
I (...) usb_uac_main: uac_device_set_volume_cb: <value>
I (...) usb_uac_main: uac_device_set_mute_cb: <value>
```

Exact interface numbers can vary with configuration.

## Troubleshooting

- The T5-P4 V0.3 OTG connector uses the ESP32-P4 OTG2.0 peripheral. Keep `CONFIG_TINYUSB_RHPORT_HS=y` so the UAC example uses the same working USB path as the MSC device example.
- USB Device mode disables the BQ25896 OTG boost before TinyUSB starts. The computer must supply VBUS; do not use a host-only OTG adapter for this connection.
- This example intentionally leaves GPIO43 MCLK unused because enabling I2S MCLK on that pin prevents USB HS enumeration on T5-P4 V0.3. The ES8311 derives its internal master clock from BCLK instead.
- If the host never enumerates the device, start with USB cable quality and host-side USB permissions or drivers.
- If playback works but recording does not, check the onboard I2S read path and the selected microphone channel count.
- If audio is distorted, keep the sample rate and channel configuration aligned between the host and `menuconfig`.
