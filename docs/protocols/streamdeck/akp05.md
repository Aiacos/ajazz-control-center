# AJAZZ AKP05 / AKP05E — Wire Protocol (work in progress)

"Stream Dock Plus"-class controller: 15 keys on the main grid + 4 endless rotary encoders + tiny LCDs above each encoder + a horizontal touch strip. Our backend models it through `IDisplayCapable` + `IEncoderCapable`; `IRgbCapable` may be added later.

## Identification

| Property       | Value (provisional)        |
|----------------|----------------------------|
| Vendor ID      | `0x0300`                   |
| Product ID     | `0x5001`                   |
| Packet size    | 512 bytes                  |

## Encoder events

Expected layout of an encoder input report (to be verified against a capture):

```
byte 0..8 : 0x00
byte 9    : 0x20 + encoder_id  (0x20..0x23)
byte 10   : rotation sign       (0x01 = CW, 0xff = CCW)
byte 11   : press state          (0x00 = up, 0x01 = down)
```

## Touch strip

The horizontal strip reports absolute X position (0..639) and gesture hints (tap, swipe-left, swipe-right, long-press).

## Status

| Area                  | State       |
|-----------------------|-------------|
| Backend scaffolding   | ✅ present   |
| Image upload          | 🟠 stubbed   |
| Encoder / dial events | 🟠 stubbed   |
| Touch strip           | 🟠 stubbed   |
| Integration fixtures  | 🟠 missing   |
