#!/bin/sh
# Post-install hook for .deb / .rpm packages.
#
# Runs as root with the filesystem already populated. Reloads udev so
# the rules we just shipped take effect without a reboot. Triggers udev
# to apply the new ACLs to devices that are already plugged in.
#
# Nothing here asks the user for input or requires them to join a group,
# because `99-ajazz.rules` uses `TAG+="uaccess"` which is handled by
# systemd-logind on the fly.

set -e

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=usb --subsystem-match=hidraw || true
fi

# Refresh desktop database so the launcher shows up immediately.
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi

exit 0
