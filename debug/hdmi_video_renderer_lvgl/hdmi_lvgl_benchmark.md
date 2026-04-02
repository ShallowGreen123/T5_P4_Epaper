### `plans/hdmi_lvgl_benchmark.md`

```md
# HDMI LVGL Benchmark Migration Plan

## Objective

Convert the existing `hdmi_video_renderer` example into a new application that:

- runs on `ESP32-P4-Function-EV-Board + ESP-HDMI-Bridge`
- uses the existing BSP HDMI display path
- targets `ESP-IDF 5.4.x`
- outputs over HDMI in `RGB888`
- boots directly into `lv_demo_benchmark()`

The first supported mode is:

- `800x600`
- `RGB888`
- benchmark on boot
- no SD card video playback required in the default flow

---

## Non-goals for the first iteration

These are intentionally out of scope until the basic path is stable:

- 1080p optimization
- multiple LVGL demos
- touch or encoder UI navigation
- audio playback retention
- custom low-level HDMI driver rewrite
- large-scale cleanup of all legacy playback code
- performance tuning beyond basic build/run stability

---

## Repository baseline summary

The current baseline example is `hdmi_video_renderer`.

It already provides:
- HDMI-oriented application structure
- BSP display initialization
- default 800x600-oriented HDMI boot path
- playback-driven frame presentation path
- SD card and media-playback-oriented startup logic

What we want to keep:
- HDMI display bring-up and board/BSP assumptions

What we want to replace:
- media playback as the default application pipeline

---

## Working assumptions

1. BSP HDMI path is the safest display baseline.
2. IDF 5.4.x compatibility takes priority over adopting newer configs.
3. Benchmark should be introduced only after HDMI initialization is separated cleanly.
4. First success criteria is “boots and shows LVGL benchmark on HDMI”, not “fully polished example”.

If any of these assumptions conflict with the checked-in code, the code wins and the plan should be updated.

---

## Milestone tracker

## M0 - Freeze and verify baseline
**Status:** pending

### Goal
Create a stable starting point before functional changes.

### Tasks
- Copy or branch `hdmi_video_renderer` into a new working target
- Confirm the target is `esp32p4`
- Confirm the project builds with `ESP-IDF 5.4.x`
- Record any config drift caused by files generated under a newer IDF
- Keep current runtime behavior unchanged

### Validation
```bash
idf.py set-target esp32p4
idf.py build