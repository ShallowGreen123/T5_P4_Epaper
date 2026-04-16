# T5-P4 EPUB Reader

This example ports the core reader flow from `atomic14/diy-esp32-epub-reader` onto the LilyGo T5-P4 E-paper board.

Implemented in this example:

- EPUB scan from `/sdcard/books`, falling back to `/sdcard`
- Book list, table of contents, and paged reading
- Basic JPEG and PNG rendering inside EPUB content
- GT911 touch navigation with a 3-button bottom toolbar
- FastEPD-native 4bpp grayscale rendering

Not included in v1:

- Deep sleep and RTC restore
- Battery indicator
- CJK font support
- Gesture navigation

## Build

```bash
idf.py -C examples/epub_reader_t5_p4 set-target esp32p4 build
```

## Usage

1. Copy `.epub` files to the SD card under `books/` or the SD root.
2. Flash the example and boot the board.
3. Use the bottom toolbar:
   - Left: `Prev`
   - Middle: `Next`
   - Right: `Select` or `Back`

## Notes

- This example reuses the repository-level `components/fastepd` and `components/sensorlib` components.
- Third-party sources bundled under `components/epub_3p` include TinyXML2, miniz, PNGdec, and TJpgDec. Their original license headers are preserved in the vendored files.
- Core EPUB parsing and layout logic is adapted from `atomic14/diy-esp32-epub-reader` with minimal structural changes plus T5-P4 specific fixes.
