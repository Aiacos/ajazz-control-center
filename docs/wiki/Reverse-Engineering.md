# Reverse Engineering Notes

This project is **clean-room**: no vendor firmware, no disassembled
binaries and no NDA-covered material has been used. Everything is
documented from observed USB traffic and widely-available public
references.

The canonical protocol document is
[`docs/protocols/REVERSE_ENGINEERING.md`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/protocols/REVERSE_ENGINEERING.md).
This page is a how-to for contributors who want to add a new capture.

## Legal boundary

- We study observable I/O only (USB, HID, BLE).
- We do not ship vendor firmware, vendor assets, or vendor code.
- We cite inspiration from OSS projects
  ([OpenDeck](https://github.com/nimble-tools/opendeck),
  [elgato-streamdeck](https://github.com/streamdeck-linux-gui/streamdeck-linux-gui),
  [mirajazz](https://github.com/prplecake/mirajazz),
  [OpenRazer](https://github.com/openrazer/openrazer)) but reimplement
  in Qt/C++.

If you believe any file in this repository infringes, open a
[SECURITY advisory](https://github.com/Aiacos/ajazz-control-center/security/advisories/new)
and we will remove or replace it.

## Tools

| Platform | Capture tool                                        | Decoder                                  |
| -------- | --------------------------------------------------- | ---------------------------------------- |
| Linux    | Wireshark + `modprobe usbmon`                       | Wireshark USB HID dissector, `hid-tools` |
| Windows  | [USBPcap](https://desowin.org/usbpcap/) + Wireshark | Wireshark                                |
| macOS    | `PacketLogger` (Apple extra tools) + Wireshark      | Wireshark                                |

Bluetooth: `btmon` on Linux, `PacketLogger` on macOS.

## Capture workflow

1. Start the capture with the device **unplugged**.
1. Plug in the device — enumeration descriptors are critical.
1. Open the vendor app. For each **one** setting change (e.g. brightness
   0 → 100), save and immediately snapshot the capture with a note.
1. Change every parameter at least three times so you can see the
   encoding (constant vs. delta).
1. Change **one thing at a time**. A capture with N simultaneous changes
   is almost useless.

## Analysis pattern

For a given HID report, we record:

- `reportId`, length, direction
- field offset, width, endianness
- observed value range
- mapping to user-facing semantics

Example, AKP153 set-brightness (see
[akp153.md](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/protocols/streamdeck/akp153.md)):

```
OUT  report_id=0x03  size=32
  [0] 0x03             // report id
  [1] 0x08             // cmd: brightness
  [2] brightness 0-100
  [3..31] 0x00 padding
```

## Checksum families

- Elgato-derived stream decks use **no checksum** — they rely on USB CRC.
- AJ-series mice use an **envelope** `AA 55 <len> <cmd> <payload> <xor>`
  where `<xor>` is XOR of `len..payload[end-1]`.
- Some AJAZZ keyboards use a CRC-16/MODBUS over `len..payload`.

## Image encoding (stream decks)

AKP153 expects 85×85 BGRA raw images, row-major, chunked into 1024-byte
HID OUT reports. The last chunk sets bit 7 of `flags`. See the reference
implementation in
[`src/devices/streamdeck/akp153.cpp`](https://github.com/Aiacos/ajazz-control-center/blob/main/src/devices/streamdeck/akp153.cpp).

## Submitting a capture

Open a
[Device Request](https://github.com/Aiacos/ajazz-control-center/issues/new?template=device_request.yml)
with:

- `.pcapng` attached (compressed with `zstd` if > 10 MB).
- A `.md` with: device model, firmware version, OS of capture, and for
  every packet range, what action you performed.

Captures are archived in
[`docs/protocols/captures/`](https://github.com/Aiacos/ajazz-control-center/tree/main/docs/protocols/captures)
once reviewed.
