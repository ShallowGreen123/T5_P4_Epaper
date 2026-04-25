# epub_reader_t5_p4

## What This Example Does

This example turns the T5-P4 E-Paper board into a simple EPUB reader. It scans the SD card for books, renders text and images on the e-paper panel, and lets you navigate with the GT911 touch screen.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- A microSD card inserted in the onboard slot.
- One or more `.epub` files stored in `/sdcard/books` or in the SD card root.
- Optional note: the current implementation does not include deep sleep restore, battery UI, or CJK font support.

## Build and Flash

```bash
idf.py -C examples/epub_reader_t5_p4 set-target esp32p4
idf.py -C examples/epub_reader_t5_p4 build
idf.py -C examples/epub_reader_t5_p4 -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) t5_epub_board: XL9555 initialized
I (...) t5_epub_board: GT911 initialized
I (...) t5_epub_board: Panel dimensions: 1440x720
I (...) t5_epub_board: Display ready: 1440x720
```

Most of the runtime behavior is visible on the screen rather than in the serial log.

## Troubleshooting

- If the UI boots but no books are listed, confirm the SD card mounts correctly and the `.epub` files are under `books/` or the card root.
- If touch does not work, check the GT911 path and board revision-specific reset/address behavior.
- If a book renders incorrectly, try a simpler EPUB first because advanced layouts and some fonts are not fully supported yet.