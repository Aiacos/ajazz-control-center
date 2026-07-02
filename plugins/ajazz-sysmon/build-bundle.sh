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

# btop uses std::ranges::to (C++23), which lands in libstdc++ with GCC 14 —
# ubuntu-24.04 CI runners default to GCC 13.3 and fail (Release run
# 28619135595). Honour an explicit $CXX, otherwise pick the newest capable
# g++ on PATH; error out loudly instead of letting the compile spray 100
# template errors.
HELPER_CXX="${CXX:-}"
if [ -z "$HELPER_CXX" ]; then
    for cand in g++ g++-15 g++-14; do
        command -v "$cand" >/dev/null 2>&1 || continue
        if [ "$("$cand" -dumpversion | cut -d. -f1)" -ge 14 ]; then
            HELPER_CXX="$cand"
            break
        fi
    done
fi
if [ -z "$HELPER_CXX" ]; then
    echo "ERROR: no g++ >= 14 found (btop needs std::ranges::to / libstdc++ 14)" >&2
    exit 1
fi

# No stdout suppression: CMake prints usage errors (bad -S/-B, generator not
# found) to STDOUT, so `>/dev/null` turned real CI failures into a silent
# exit-1 with only the echo above in the ninja log (Release run 28618367640).
# `set -x` makes the failing command line explicit in packaging-job logs.
set -x
echo "==> building btop-metrics-helper (CXX=$HELPER_CXX)"
cmake -S "$HERE/metrics-helper" -B "$HERE/metrics-helper/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$HELPER_CXX"
cmake --build "$HERE/metrics-helper/build"

echo "==> building ajazz-sysmon plugin"
cargo build --release --manifest-path "$HERE/plugin/Cargo.toml"

echo "==> assembling $BUNDLE"
rm -rf "$BUNDLE"
mkdir -p "$BUNDLE"
cp "$HERE/plugin/manifest.json" "$BUNDLE/manifest.json"
cp "$HERE/plugin/property-inspector.html" "$BUNDLE/property-inspector.html"
cp "$HERE/plugin/target/release/ajazz-sysmon" "$BUNDLE/ajazz-sysmon-$TRIPLE"
cp "$HERE/metrics-helper/build/btop-metrics-helper" "$BUNDLE/btop-metrics-helper"
chmod +x "$BUNDLE/ajazz-sysmon-$TRIPLE" "$BUNDLE/btop-metrics-helper"
# --render-test <metric> <out.png>: argv[2] is the METRIC, argv[3] the output —
# passing the path as argv[2] silently rendered to /tmp/sysmon-tile.png and the
# bundle shipped with no icon at all (blank entry in the SPA plugin list).
"$BUNDLE/ajazz-sysmon-$TRIPLE" --render-test cpu "$BUNDLE/icon.png" 2>/dev/null || true

echo "==> done: $BUNDLE"
