#!/usr/bin/env python3
"""Boatswain readiness check for the AKP05E (0300:3004) demo unit.

Boatswain (com.feaneron.Boatswain) is a GTK4/libadwaita Stream Deck app that
talks to devices over **libusb** (not hidraw). Before pointing it at the
AKP05E, two preconditions matter for a libusb claim:

1. The device must be present on the bus.
2. Whether a kernel driver (usbhid) is already bound to each interface --
   libusb must detach it to claim the interface. This report lists that so a
   failed Boatswain claim can be diagnosed as "kernel owns the interface"
   vs "device absent" vs "Boatswain never enumerated the PID".

Pure stdlib -- reads /sys, no external packages, no system mutation.

This does NOT run Boatswain (a GUI app; per project rules we do not
system-install it here). It reports the hardware facts Boatswain needs, and
the README documents the manual test procedure and the source change
required to make Boatswain recognise VID 0x0300.
"""

from __future__ import annotations

from pathlib import Path

VID = "0300"
PID = "3004"


def _read(path: Path, default: str = "?") -> str:
    """Return the stripped text of a sysfs attribute, or a default if absent."""
    return path.read_text().strip() if path.exists() else default


def main() -> int:
    """Report every USB interface of 0300:3004 and its bound kernel driver."""
    sys_usb = Path("/sys/bus/usb/devices")
    if not sys_usb.exists():
        print("no /sys/bus/usb/devices -- not Linux or no sysfs")
        return 1

    found = 0
    for dev in sorted(sys_usb.iterdir()):
        if _read(dev / "idVendor", "") != VID or _read(dev / "idProduct", "") != PID:
            continue
        found += 1
        print(f'Device {dev.name}: {VID}:{PID} "{_read(dev / "product")}"')
        # Each interface is a child dir like "1-2:1.0"; report its class + driver.
        for iface in sorted(dev.glob(f"{dev.name}:*")):
            cls = _read(iface / "bInterfaceClass")
            driver = (iface / "driver").resolve().name if (iface / "driver").exists() else "(none)"
            print(f"  interface {iface.name}: class=0x{cls} driver={driver}")

    if not found:
        print(f"No {VID}:{PID} on the bus. Plug in the AKP05E and retry.")
        return 1
    print(
        "\nReadiness: to test Boatswain, its device table must be patched to accept "
        f"VID 0x{VID} (see README). libusb will detach the listed kernel drivers on claim."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
