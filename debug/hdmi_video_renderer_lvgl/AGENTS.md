# AGENTS.md

## Project goal

Transform the existing `hdmi_video_renderer` example into an HDMI LVGL benchmark example for:

- ESP32-P4-Function-EV-Board
- ESP-HDMI-Bridge
- ESP-IDF 5.4.x
- esp-bsp display path

The project should boot directly into LVGL benchmark over HDMI, while preserving the existing BSP-based HDMI bring-up path.

---

## Hard constraints

1. **Target hardware is fixed**
   - Board: `ESP32-P4-Function-EV-Board`
   - Bridge: `ESP-HDMI-Bridge`
   - Display path: MIPI-DSI to LT8912B HDMI bridge through BSP
   - Do not retarget to other boards unless explicitly requested.

2. **ESP-IDF version is fixed**
   - Use **ESP-IDF 5.4.x only**
   - Do not silently upgrade to 5.5.x or newer
   - If code or config is 5.5-generated, adapt it back to 5.4.x compatibility instead of upgrading the repo

3. **HDMI display constraints are fixed**
   - HDMI color format must remain **RGB888**
   - First milestone must support **800x600 only**
   - Do not add 1080p or multi-resolution logic until the 600p benchmark path is stable

4. **Preserve the HDMI bring-up path**
   - Keep the existing BSP-based display initialization
   - Prefer `bsp_display_new()` and related BSP objects
   - Do not replace BSP HDMI initialization with a custom low-level display driver in the early milestones

5. **Replace only the upper pipeline first**
   - The default boot flow should stop launching MP4 playback
   - Replace the playback pipeline with LVGL init + `lv_demo_benchmark()`
   - Avoid mixing large HDMI driver rewrites with LVGL app changes in the same milestone

6. **Keep diffs small**
   - Make milestone-based changes
   - Avoid unnecessary renames, directory moves, or wide refactors
   - Avoid changing unrelated files

---

## Engineering style

- Prefer small, reviewable commits
- Preserve existing code comments where still accurate
- Add comments only where they clarify hardware/version assumptions
- Do not add speculative optimizations unless requested
- Keep public function names clear and boring
- Do not introduce framework churn

---

## Required validation after each milestone

Always run these commands after making a milestone-sized change:

```bash
idf.py set-target esp32p4
idf.py build