# M5GFX E0470A01 684 x 1216 16-bit EPD Test

This ESP-IDF example drives the fly-wired E0470A01-AF-CF panel from the T5-P4
V0.2 board with `m5stack/m5gfx: ^0.2.23`.

## Resolution correction

The hardware directory is named `648x1216`, but both supplied documents identify
the panel as **684 x 1216**:

- `E0470A03-AF-S A version specification`, sections 1 and 7.1: 684 x 1216
- `E0470A01-AF-CF(A)(1)` mechanical drawing: 684 x 1216 pixels

The source driver shifts 1216 pixels and the gate driver scans 684 rows. The
M5GFX native buffer is therefore 1216 x 684 and is rotated to a 684 x 1216
portrait drawing surface.

## Panel configuration

- 16-bit I80 EPD data bus
- 20 MHz source clock, reduced for the fly-wire connection
- 10 dummy clocks after each source line
- `Panel_EPD::line_padding = 20` bytes (10 clocks x 2 bytes on a 16-bit bus)
- XSTL is low for exactly the first 16-bit source clock of each line
- Adjacent DMA bytes are swapped so each 8-pixel word reaches the panel as
  pixels 0..3 on D15..D8 and pixels 4..7 on D7..D0
- TPS651851 VCOM default: -1.60 V
- GPIO53 and GPIO54 are forced low so the front light stays off

The A01 document is a 40-pin mechanical/pin drawing and does not include full
AC timing. The supplied A03 specification is for a different 34-pin, 8-bit
panel. Its source timing and 10 dummy clocks are used as the initial timing
reference; verify them against the A01 lot documentation if the supplier can
provide it.

M5GFX's stock ESP32 `Bus_EPD` uses XSTL as the I80 chip-select signal, which
keeps XSTL low for the entire DMA line. This example supplies a local compatible
implementation in `main/Bus_EPD_16.cpp`: the I80 command phase carries the first
16-bit word with XSTL low, then the data phase carries the remaining words with
XSTL high. The generated `managed_components` directory is not modified.

ESP32-P4 I80 normally places the first byte of each DMA word on D7..D0. M5GFX's
EPD encoder stores the first four pixels in that byte, while this 16-bit panel
expects the first four pixels on D15..D8. Without the adjacent-byte swap, every
8-pixel group is displayed as pixels 4..7 followed by pixels 0..3. Large blocks
still look plausible, but text, curves, and diagonal lines appear broken. The
local bus swaps both the DMA data and the first command-phase word. The example
also performs one full white quality refresh before drawing the test pattern so
an older incorrectly ordered image does not remain as ghosting.

VCOM is panel-lot-specific. Change `kVcomMillivolts` in `main/main.cpp` if the
panel label or factory data specifies another value.

## Wiring

| Signal | GPIO | Signal | GPIO |
| --- | ---: | --- | ---: |
| D0 | 27 | D8 | 50 |
| D1 | 28 | D9 | 49 |
| D2 | 29 | D10 | 23 |
| D3 | 30 | D11 | 22 |
| D4 | 31 | D12 | 11 |
| D5 | 32 | D13 | 21 |
| D6 | 33 | D14 | 20 |
| D7 | 34 | D15 | 2 |
| CKH / XCL | 24 | STH / XSTL | 25 |
| LEH / XLE | 26 | CKV | 13 |
| STV / SPV | 48 | Front light 1 / 2 | 53 / 54 |

GPIO25/XSTL is routed through the I80 D/C phase generator. GPIO51 only occupies
the unused M5GFX power/OE config fields and is not connected to the panel.

EPD OE, MODE, TPS WAKEUP, TPS PWRUP, VCOM control, and power-good are handled
through the board PCA9535 and TPS651851. The shutdown sequence holds OE low for
at least 12 us before disabling the high-voltage rails.

## Build and flash

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

The test image contains corner labels, a 16-level gray ramp, checkerboards,
geometry, and one-pixel line patterns. If the corner labels are not in their
matching physical corners, adjust `kPortraitRotation` before changing the data
pin order.

## Diagnostic log and fly-wire limits

Lines tagged `EPD_DIAG` report:

- PCA9535 OE, MODE, WAKEUP, PWRUP, VCOM_CTRL, PWR_GOOD, and INT levels
- TPS651851 ENABLE, VCOM1/VCOM2, per-rail PG, and REVID registers
- every waveform frame's line count, source clocks per line, XSTL pulse width,
  and I80 transmit error count

A healthy scan reports `lines=684`, `clocks/line=162`, `XSTL=1-clock`,
`byte-lane swap=on`, and `tx_errors=0`. A powered panel should report TPS
`PG=0xFA`, VCOM registers for the configured voltage, and PCA `PWR_GOOD=1`.
During standby, TPS `PG=0x20` is allowed: it only means the VN base converter
remains ready while all four panel rails (VDDH, VPOS, VEE, and VNEG) are off.

D0-D15, CKH, XSTL, XLE, CKV, and STV are output-only signals. DMA completion
only proves that the ESP32-P4 peripheral sent data; it cannot prove that a
fly-wire is continuous at the panel FPC. PCA/TPS readback can expose power-side
faults, but a single open signal wire normally produces no log error. Check
continuity with a multimeter, or probe the adapter/FPC end with a logic analyzer
or oscilloscope. Do not run an automatic high/low GPIO test while the unpowered
panel is connected because it can back-power the panel through its inputs.
