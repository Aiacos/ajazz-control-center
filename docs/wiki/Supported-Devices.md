# Supported Devices

Support levels:

- **Full** — every feature the vendor app exposes works.
- **Functional** — core I/O works (keys/buttons/LEDs) but some advanced
  features are missing.
- **Scaffolded** — detected and enumerated; protocol still being mapped.
- **Requested** — captures wanted; open a Device Request issue.

Because protocols are reverse-engineered, support levels can change with
each release. See [CHANGELOG.md](https://github.com/Aiacos/ajazz-control-center/blob/main/CHANGELOG.md).

## Stream decks

| Model      | USB VID:PID | Keys | LCD | Encoders | Status       |
|------------|-------------|------|-----|----------|--------------|
| AKP153     | 0300:1001   | 15   | Per-key 85×85 | 0 | **Functional** — images, per-key backlight, key events |
| AKP153E    | 0300:1002   | 15   | Per-key 85×85 | 0 | **Functional** — Elite variant, same protocol family |
| AKP03      | 0300:3001   | 6    | Per-key       | 2 | Scaffolded — encoders pending |
| AKP05      | 0300:5001   | 10   | Per-key + strip | 2 | Scaffolded — strip pending |

Reference: [AKP153 protocol](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/protocols/streamdeck/akp153.md).

## Keyboards

| Family                 | Detection        | Layers | RGB | Macros | Status |
|------------------------|------------------|--------|-----|--------|--------|
| VIA / QMK-compatible   | VIA `usage_page=0xFF60` | 4+ | Full | Full | **Functional** — any VIA JSON works |
| AJAZZ AK series (proprietary) | VID 0x3151 | 3 | Full | Partial | Scaffolded |

VIA keyboards are the easiest to support — just drop the `via.json`
layout into `~/.config/ajazz-control-center/via/` and the keyboard is
fully configurable.

## Mice

| Model   | USB VID:PID | Sensor      | DPI stages | RGB zones | Status |
|---------|-------------|-------------|------------|-----------|--------|
| AJ159   | 3554:f51a   | PAW3395     | 4          | 2         | **Functional** |
| AJ199   | 3554:f51b   | PAW3395     | 4          | 2         | **Functional** |
| AJ339   | 3554:f51c   | PAW3395     | 6          | 3         | Functional — wireless pending |
| AJ380   | 3554:f51d   | PAW3950     | 6          | 4         | Scaffolded |

Reference: [AJ-series protocol](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/protocols/mouse/aj_series.md).

## Adding your device

If your AJAZZ device is not listed:

1. Capture USB traffic with Wireshark + `usbmon` (Linux) or USBPcap
   (Windows) while the vendor app changes every setting.
2. Save the `.pcapng` and a short README of what you changed.
3. Open a
   [Device Request](https://github.com/Aiacos/ajazz-control-center/issues/new?template=device_request.yml).

See [Reverse Engineering](Reverse-Engineering) for the methodology.
