# AKP05E input cross-check — Boatswain

**Spike branch. Not for merge into `develop`.** One of three per-stack spike
branches probing the same question (see also the `python-elgato-streamdeck`
and `streamcontroller` experiment branches).

## Hypothesis under test

The `0300:3004` "HOTSPOTEKUSB HID DEMO" (white-label AKP05E demo unit) never
emits input reports. Proven against five independent methods in
`docs/protocols/streamdeck/akp05_input_corrections.md` §7.1 (raw hidraw,
`GET_REPORT` poll, evdev, usbmon, and the `4ndv/mirajazz` Rust library). This
branch asks whether **[Boatswain][bs]** — a mature, independent Stream Deck
stack with a completely different transport — reads any input the others miss.

## Why Boatswain is a meaningfully different test

Boatswain is a GTK4/libadwaita GNOME app written in C. Unlike our stack (and
mirajazz), which use **hidraw**, Boatswain talks to devices over **libusb**
and implements the Elgato wire protocol itself. A libusb read path claims the
interface and reads endpoints directly, so if the demo firmware selectively
starves one transport but not the other, Boatswain would expose it. (The
proof chain's usbmon capture argues against this — the kernel arms EP `0x82`
and the device declines to fill it regardless of who is reading — but an
independent libusb stack is still worth one confirmation.)

## Known blocker to verify first

Boatswain's device support is keyed to **Elgato** VID/PID pairs. The AKP05E is
VID `0x0300`, so stock Boatswain will not enumerate it. Before any input test:

1. **Verify in Boatswain's source** where the device table lives (look for the
   `.desc`/device-model registration and the VID/PID match — do not trust this
   README's guess; grep the actual checkout). Add an entry for `0300:3004`
   with AKP05E geometry (15 keys + 4 encoders + touch strip).
1. Rebuild Boatswain from source (Flatpak or meson). **Do not** rely on the
   Flathub binary — it has no `0300` entry.

## Run the readiness check (no external deps)

```bash
python3 check_device.py
```

Reports each `0300:3004` USB interface, its class, and the bound kernel driver
— the facts a libusb claim depends on (libusb must detach `usbhid` to claim an
interface). Pure stdlib; does not run Boatswain.

## Manual test procedure

Per project rules we do **not** system-install Boatswain here. To run the test
on a machine where you can:

1. `git clone https://github.com/feaneron/Boatswain && cd Boatswain`
1. Add the `0300:3004` device entry (see "Known blocker" above); build with
   `meson setup _build && meson compile -C _build`.
1. Run `_build/src/boatswain` with the AKP05E plugged in.
1. Press keys / turn encoders / touch the strip while watching
   `G_MESSAGES_DEBUG=all` output for any input event.

## Reading the result

- **No input events** → agrees with the proof chain; the demo firmware's input
  path is dead across transports (hidraw *and* libusb). Expected.
- **Any input event** → **libusb reaches input hidraw does not.** Major result;
  capture the raw endpoint bytes and reconcile with `parseInputReport` in
  `akp05_input_corrections.md`.

[bs]: https://gitlab.gnome.org/World/boatswain
