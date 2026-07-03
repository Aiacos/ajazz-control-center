# AKP05E input cross-check — python-elgato-streamdeck

**Spike branch. Not for merge into `develop`.** Isolated harness for one
question, tested independently on its own branch alongside the Boatswain and
StreamController spikes.

## Hypothesis under test

The `0300:3004` "HOTSPOTEKUSB HID DEMO" (white-label AKP05E demo unit)
**never emits input reports** — key presses, encoder turns, and touch-strip
taps produce nothing on any HID interface. This is documented and proven
against five independent methods in
`docs/protocols/streamdeck/akp05_input_corrections.md` §7.1:

1. raw hidraw read
1. `GET_REPORT` polling
1. evdev (`event*`)
1. raw `usbmon` filtered to the device
1. the reference Rust library `4ndv/mirajazz` (its own `async_hid` backend)

All five captured **zero input on press**. The kernel arms endpoint `0x82`
correctly; the device declines to fill it. Leading theory: demo/engineering
firmware with the input path stubbed.

## What this spike adds

A **6th independent method**: [`abcminiuser/python-elgato-streamdeck`][lib]
(PyPI `streamdeck`) — the canonical low-level Elgato library that both
Boatswain and StreamController sit on top of. If its HIDAPI transport also
reads zero, that is one more independent stack agreeing the fault is
firmware-level. If it reads *anything*, that overturns the finding and is a
major result — capture it immediately.

Note the library only enumerates Elgato VID `0x0fd9` at its high level, so
`DeviceManager().enumerate()` never sees the AKP05E. The harness drops to the
raw transport and opens `0300:3004` directly — the same path a `StreamDeck`
subclass would take once its device table knew the PID.

## Run

Project rule: **no system installs.** Use a throwaway venv.

```bash
python3 -m venv /tmp/sd-venv && . /tmp/sd-venv/bin/activate
pip install streamdeck               # transport uses the SYSTEM libhidapi-libusb.so.0
python3 probe_input.py --seconds 60  # then press keys / turn dials / touch strip
```

## Live-run result (2026-07-04) — the transport is libusb, and open() is blocked here

Verified on the actual `0300:3004` unit. The current library ships a single
**libusb-backed** transport (`LibUSBHIDAPI`); there is no hidraw transport.
The probe behaves like this:

- `enumerate(0x0300, 0x3004)` → **2 interfaces found** (libusb sees the device).
- `open()` → **fails: "Could not open HID device"** on both interfaces.

Two independent blockers, both real on this machine:

1. **USB bus node not writable.** `/dev/bus/usb/001/022` is `root:root rw-rw-r--` with **no `uaccess` ACL** (unlike the hidraw nodes, which have
   one). libusb needs write access to claim. Transient dev fix:
   `sudo setfacl -m u:$(id -u):rw /dev/bus/usb/001/022` (renumber per `lsusb`).
1. **The device is already held over hidraw.** libusb must detach the kernel
   `usbhid` driver to claim the interface, which conflicts with existing
   holders. `fuser /dev/hidraw16 /dev/hidraw17` showed:
   - the **OpenDeck `opendeck-akp05-linux` plugin** (ambiso, v0.10.2), and
   - **Wine's `winedevice.exe`**, which grabs *all* hidraw nodes including the
     AKP05E's.

So a definitive libusb read requires stopping those holders **and** granting
USB-node write — a disruptive change to running apps, not done automatically.
The hidraw path (`scripts/akp05_input_probe.py`) can read concurrently without
disrupting them, but that is a different transport, not this library.

## Reading the result

- **0 nonzero frames** → agrees with the proof chain; the demo unit's input
  is firmware-disabled, not a stack bug. Expected outcome.
- **Any nonzero frame** → **input IS reachable via this stack.** Overturns
  §7.1. Save the hex dump and compare its `report[9]`/`report[10]` layout to
  the `parseInputReport` structure in `akp05_input_corrections.md`.

## Remaining decisive method

Per `~/MEGAsync/ajazz-reverse-engineering/dossier/methods-and-tooling.md §2`,
the decisive method for input is **Frida-on-Windows vendor app**, or a
**retail AKP05E / Mirabox N4** unit (the demo SKU may simply lack the path).

[lib]: https://github.com/abcminiuser/python-elgato-streamdeck
