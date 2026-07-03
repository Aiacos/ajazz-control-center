#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the OpenDeck SPA (webui UI mode) and inject the Tauri-compat shim so it
# talks to our OpenDeckBridge over a QWebChannel. Output: a static site in
# src/app/webui/opendeck/build/ that CMake bundles into the :/opendeck Qt
# resource when -DAJAZZ_BUILD_WEBUI=ON.
#
# OpenDeck is vendored as a git submodule at src/app/webui/opendeck (pristine
# upstream nekename/OpenDeck). Update it with:
#   git submodule update --remote src/app/webui/opendeck
# Our customizations live OUTSIDE the submodule (the shim below + the C++
# OpenDeckBridge/scheme-handler), so the submodule stays pristine and bumpable.
#
# Requires Node.js (>= 20) + npm. Idempotent. See docs/opendeck-ui/.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
webui_dir="$repo_root/src/app/webui/opendeck"
shim="$repo_root/resources/opendeck-shim/tauri-shim.js"
out="$webui_dir/build"

if ! command -v npm >/dev/null 2>&1; then
    echo "build-webui: npm not found — install Node.js >= 20 to build the webui SPA." >&2
    exit 2
fi
if [[ ! -f $shim ]]; then
    echo "build-webui: missing shim at $shim" >&2
    exit 1
fi

cd "$webui_dir"

# Local UI patches (patches/opendeck/*.patch): device-layout fidelity fixes we
# carry until upstreamed (keys -> touch strip -> dials row order matching the
# physical hardware; rectangular touch-strip segments). Idempotent: an
# already-applied patch (dev working tree) is detected via --reverse --check
# and skipped; CI's pristine submodule checkout gets it applied fresh.
for p in "$repo_root"/patches/opendeck/*.patch; do
    [[ -e $p ]] || continue
    if git apply --check "$p" 2>/dev/null; then
        git apply "$p"
        echo "build-webui: applied $(basename "$p")"
    elif git apply --reverse --check "$p" 2>/dev/null; then
        echo "build-webui: $(basename "$p") already applied"
    else
        echo "build-webui: ERROR — $(basename "$p") does not apply (submodule bumped?)" >&2
        exit 1
    fi
done

echo "build-webui: installing deps in $webui_dir"
if [[ -f package-lock.json ]]; then npm ci; else npm install; fi
# esbuild's native binary postinstall is sometimes skipped under restrictive npm
# configs; ensure it's present so vite/esbuild can run.
[[ -f node_modules/esbuild/install.js ]] && node node_modules/esbuild/install.js || true

echo "build-webui: vite build"
npm run build

# Stage the shim beside the SPA and inject it as the FIRST <head> script so it
# runs before the deferred SvelteKit module entry.
cp "$shim" "$out/tauri-shim.js"
python3 - "$out/index.html" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
tag = '<script src="./tauri-shim.js"></script>'
if "tauri-shim.js" not in s:
    s = s.replace("<head>", "<head>\n\t\t" + tag, 1)
    open(p, "w", encoding="utf-8").write(s)
    print("build-webui: injected tauri-shim.js into index.html")
else:
    print("build-webui: shim already injected")
PY

echo "build-webui: done -> $out"
