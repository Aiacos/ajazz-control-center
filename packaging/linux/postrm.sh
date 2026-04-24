#!/bin/sh
# Post-remove hook for .deb / .rpm packages.
set -e

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

exit 0
