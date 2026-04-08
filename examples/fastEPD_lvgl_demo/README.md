# FastEPD LVGL Demo for ESP-IDF

This is a standalone ESP-IDF port of `platformIO_P4/examples/lvgl_demo`.

It keeps the FastEPD + LVGL UI, GT911 touch, XL9555 GPIO expander, and LilyGo T5 P4 e-paper display settings. The WiFi screen is present, but scanning is intentionally disabled in this pure ESP-IDF version.

Build:

```sh
idf.py -C examples/fastEPD_lvgl_demo set-target esp32p4 build
```
