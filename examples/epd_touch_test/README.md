# M5GFX EPD Touch Test

This example drives the T5-P4 E-Paper panel through the managed `m5stack/m5gfx`
component and shows GT911 touch diagnostics directly on the EPD.

It displays:

- touch controller status
- raw GT911 coordinates
- mapped M5GFX screen coordinates
- touch sample counter
- a live marker in a scaled screen preview

Build and flash from this directory:

```bash
idf.py set-target esp32p4
idf.py build flash monitor
```
