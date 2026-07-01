#!/usr/bin/env bash
# Build the ajazz-sysmon monitor plugin + its btop-metrics-helper and assemble a
# .sdPlugin bundle. Linux host-target only for now (Phase 1); the cross-target
# matrix and macOS/Windows helper backends land in later phases.
#
#   ./build-bundle.sh [OUT_DIR]
#
# Produces OUT_DIR/com.ajazz.sysmon2.sdPlugin/ containing the manifest, icon,
# the plugin binary, and the helper binary (named so the plugin finds it as a
# sibling). Install by copying that dir into the app's plugins directory.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$HERE/dist}"
BUNDLE="$OUT/com.ajazz.sysmon2.sdPlugin"
TRIPLE="x86_64-unknown-linux-gnu"

echo "==> building btop-metrics-helper"
cmake -S "$HERE/metrics-helper" -B "$HERE/metrics-helper/build" -G Ninja >/dev/null
cmake --build "$HERE/metrics-helper/build" >/dev/null

echo "==> building ajazz-sysmon plugin"
cargo build --release --manifest-path "$HERE/plugin/Cargo.toml" >/dev/null

echo "==> assembling $BUNDLE"
rm -rf "$BUNDLE"
mkdir -p "$BUNDLE"
cp "$HERE/plugin/manifest.json" "$BUNDLE/manifest.json"
cp "$HERE/plugin/property-inspector.html" "$BUNDLE/property-inspector.html"
cp "$HERE/plugin/target/release/ajazz-sysmon" "$BUNDLE/ajazz-sysmon-$TRIPLE"
cp "$HERE/metrics-helper/build/btop-metrics-helper" "$BUNDLE/btop-metrics-helper"
chmod +x "$BUNDLE/ajazz-sysmon-$TRIPLE" "$BUNDLE/btop-metrics-helper"
"$BUNDLE/ajazz-sysmon-$TRIPLE" --render-test "$BUNDLE/icon.png" 2>/dev/null || true

echo "==> done: $BUNDLE"
