# HID capture fixtures

This directory holds raw USB-HID frames replayed by the integration test
suite (`tests/integration/test_capture_replay.cpp`) so the parsers can be
exercised without attached hardware.

## File format

Each fixture is a plain-text `.hex` file containing a single HID frame.
Whitespace, newlines, comments (`# …` until end of line) and `0x` prefixes
are all permitted; `tests/integration/hex_loader.hpp` strips them before
parsing the byte stream.

```text
# AKP153 - key 7 press, captured 2024-09-12
00 00 00 00 00 00 00 00 00 07 00 00 00 00 00 00
```

## Layout

```
fixtures/
├── akp03/
├── akp05/
├── akp153/                 # main 15-key Stream-Deck-class device
├── aj_series_mouse/
├── proprietary_keyboard/
└── malformed/              # crafted bad frames; SEC-007/008/009/010 rejection
```

Each device sub-directory follows the naming convention
`<event>_<param>.hex`, e.g. `key_press_07.hex`, `key_release_07.hex`,
`encoder_cw_01.hex`.

## How to capture real frames

Real captures should replace the seeded synthetic frames over time. The
recommended workflow on Linux:

1. Find the device path:

   ```sh
   sudo dmesg | tail -20
   # or
   lsusb -v -d 0c45:8030
   ```

1. Capture with `usbhid-dump` or the `hidapi-tools` `hid-recorder`:

   ```sh
   sudo usbhid-dump -d 0c45:8030 -e descriptor      # one-time
   sudo usbhid-dump -d 0c45:8030 -es -i 0 > raw.txt # event stream
   ```

1. Convert one frame to the `.hex` shape above (one frame per file, comments
   describing the captured input).

The macOS / Windows equivalents are documented in
`docs/dev/HID-CAPTURE.md` (alongside the udev rules / `WinUSB` bindings).

## Status

The fixtures currently shipped are **synthetic seeds**: they assert the
parser rejection logic and the happy-path field offsets we already know
from clean-room reverse engineering. As community contributors capture
traffic from physical hardware the seeds are replaced with real frames,
keeping the regression suite hardware-grounded over time.
