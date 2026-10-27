# CrossPoint Reader T5-P4

This example ports the reading-focused parts of CrossPoint Reader to the
LilyGo T5-P4 ESP-IDF board support in this repository.

## Scope

- EPUB, TXT, and Markdown file scanning from SD card.
- EPUB metadata, spine, TOC, chapter pagination, section cache, page turns, and progress restore.
- TXT/Markdown streaming page index, page cache metadata, page turns, and progress restore.
- T5-P4 FastEPD display, GT911 touch, SDSPI SD card, PSRAM-oriented defaults.

The example does not port networking, KOReader Sync, OPDS, OTA, XTC, the full
settings UI, or CrossPoint's X4 power/button layer.

## SD Layout

Place books in either:

```text
/sdcard/books
/sdcard
```

The example creates cache files under:

```text
/sdcard/.crosspoint_reader
```

## Build

Load an ESP-IDF environment, then run:

```bash
idf.py -C examples/crosspoint_reader_t5_p4 set-target esp32p4
idf.py -C examples/crosspoint_reader_t5_p4 build
```

Flash and monitor with the serial port for your board:

```bash
idf.py -C examples/crosspoint_reader_t5_p4 -p <PORT> flash monitor
```

## Touch Controls

- Library: tap a row to open a book; bottom buttons page the list or rescan SD.
- Reader: tap left/right thirds for previous/next page; tap center for menu.
- Menu: return to library, open EPUB TOC, refresh current page, or rescan SD.
- TOC: tap an entry to jump; bottom buttons page the TOC or return.

## Known Limits

- Images inside EPUB chapters are skipped or rendered as alt-text placeholders
  in this first reading-focused port. The converter factory is stubbed so image
  failures do not fail chapter pagination.
- CJK coverage depends on the bundled CrossPoint fonts.
- This is an example app, not a full CrossPoint firmware replacement.

## Source

CrossPoint Reader code was ported from:

```text
D:\dgx\code\2_studycode\crosspoint-reader
```

The imported reading core is kept in `components/crosspoint_core` and remains
under the CrossPoint Reader MIT license provenance.
