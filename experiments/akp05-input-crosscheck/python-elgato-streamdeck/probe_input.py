#!/usr/bin/env python3
"""AKP05E (0300:3004) input cross-check via python-elgato-streamdeck.

Independent verification method for the "AKP05E demo-unit input is
unreachable" hypothesis (see docs/protocols/streamdeck/akp05_input_corrections.md
section 7.1). The existing scripts/akp05_input_probe.py reads raw hidraw
directly; THIS harness drives the *same* device through the transport layer
of abcminiuser/python-elgato-streamdeck (PyPI: ``streamdeck``) -- the
canonical low-level Elgato Stream Deck library that Boatswain and
StreamController both build on.

If this library's HIDAPI transport also reads zero input reports on press,
that is a 6th independent method agreeing with the proof chain (raw hidraw,
GET_REPORT poll, evdev, usbmon, mirajazz) and further pins the finding on
firmware rather than on our stack.

Why a bespoke harness and not just ``DeviceManager().enumerate()``:
python-elgato-streamdeck only enumerates *Elgato* VID/PID pairs (USB VID
0x0fd9). The AKP05E is VID 0x0300, so the high-level API never sees it. We
therefore go one layer down to the raw transport, enumerate 0300:3004 by
VID/PID, open it, and poll ``read()`` -- exactly what a StreamDeck subclass
would do once its device table knew the PID.

IMPORTANT (verified live 2026-07-04): the current library ships a single
**libusb-backed** transport (``LibUSBHIDAPI``, loading libhidapi-libusb via
ctypes) -- NOT a hidraw transport. ``enumerate()`` sees the device, but
``open()`` claims the USB interface via libusb, which needs write access to
the USB bus node AND requires detaching the kernel usbhid driver -- so it
conflicts with anything already holding the device over hidraw (the OpenDeck
akp05 plugin, Wine's winedevice.exe, our own app/sidecar). Free the device
and grant USB-node write before expecting open() to succeed.

Setup (throwaway venv; the transport uses the SYSTEM libhidapi-libusb):
    python3 -m venv /tmp/sd-venv && . /tmp/sd-venv/bin/activate
    pip install streamdeck            # LibUSBHIDAPI loads libhidapi-libusb.so.0 via ctypes

Run:
    python3 probe_input.py                      # run until Ctrl-C
    python3 probe_input.py --seconds 60         # timed capture

Then physically press keys / turn encoders / touch the strip. Any nonzero
read is a POSITIVE result (input IS reachable via this stack) and would
overturn the current finding -- capture it and stop.
"""

from __future__ import annotations

import argparse
import contextlib
import sys
import time

# The current python-elgato-streamdeck ships a single libusb-backed transport
# (LibUSBHIDAPI, loading libhidapi-libusb via ctypes). enumerate(vid, pid)
# returns ready-to-open Device objects directly -- there is no connect() step.
try:
    from StreamDeck.Transport.LibUSBHIDAPI import LibUSBHIDAPI
except ImportError:
    LibUSBHIDAPI = None

VID = 0x0300
PID = 0x3004

_OPEN_HELP = (
    "Could not open any interface. LibUSBHIDAPI claims the USB interface via "
    "libusb, so it needs BOTH:\n"
    "  1. write access to the USB bus node, e.g.\n"
    "       sudo setfacl -m u:$(id -u):rw /dev/bus/usb/<BUS>/<DEV>\n"
    "     (find <BUS>/<DEV> in `lsusb` for 0300:3004), and\n"
    "  2. the device NOT held by another process -- libusb must detach the\n"
    "     kernel usbhid driver, which conflicts with anything using hidraw:\n"
    "     the OpenDeck akp05 plugin, Wine's winedevice.exe, or our own\n"
    "     app/sidecar. Stop those first (check: fuser /dev/hidraw*)."
)


def main() -> int:
    """Open every 0300:3004 interface via libusb, poll input, report the verdict."""
    parser = argparse.ArgumentParser(description="AKP05E input cross-check")
    parser.add_argument(
        "--seconds", type=float, default=0.0, help="stop after N seconds (0 = until Ctrl-C)"
    )
    args = parser.parse_args()

    if LibUSBHIDAPI is None:
        sys.exit("python-elgato-streamdeck not installed: pip install streamdeck")
    try:
        transport = LibUSBHIDAPI()
    except Exception as exc:
        sys.exit(f"libusb HIDAPI backend failed to load ({exc}). Need libhidapi-libusb.so.")

    matches = list(transport.enumerate(VID, PID))
    if not matches:
        sys.exit(f"No {VID:#06x}:{PID:#06x} device enumerated. Plugged in?")
    print(f"Enumerated {len(matches)} interface(s) for {VID:#06x}:{PID:#06x}; opening each.")

    devices = []
    for dev in matches:
        try:
            dev.open()
            devices.append(dev)
        except Exception as exc:
            print(f"  interface open failed (non-fatal): {exc}")
    if not devices:
        sys.exit(_OPEN_HELP)

    print(f"Opened {len(devices)} interface(s). Press keys / turn dials / touch the strip.")
    print("Any nonzero read below overturns the 'input unreachable' finding.\n")

    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
    frames = 0
    with contextlib.suppress(KeyboardInterrupt):
        while deadline is None or time.monotonic() < deadline:
            for dev in devices:
                data = dev.read(64)
                if data and any(data):
                    frames += 1
                    print(f"  INPUT[{frames}] len={len(data)} {bytes(data).hex(' ')}")
            time.sleep(0.005)

    for dev in devices:
        close = getattr(dev, "close", None)
        if callable(close):
            close()

    print(f"\nDone. Nonzero input frames observed: {frames}")
    print("0 frames => agrees with the existing proof chain (firmware-level, not stack).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
