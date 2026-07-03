# Vendored third-party: OpenDeck frontend

`opendeck/` is a lean vendored copy of the **OpenDeck** desktop application's
SvelteKit frontend, used for the `webui` UI mode (see `docs/opendeck-ui/`).

- **Upstream:** https://github.com/nekename/OpenDeck
- **Commit:** `58b59e9cb40da7a0f6637df8ca201235e6e90f40`
- **License:** GNU General Public License v3.0 (see `opendeck/LICENSE.md`).
  Compatible with this project (GPL-3.0-or-later).

## What was vendored

Only the frontend source needed to build the static SPA: `src/`, `static/*.png`,
`package.json`, `package-lock.json`, the `*.config.ts` files, `tsconfig.json`,
`deno.json`, and `product_name.txt`.

## What was deliberately omitted

- **`static/fonts/` (~14 MB of key-title fonts)** — excluded to keep the repo
  lean. The SPA falls back to system fonts for key titles. To restore the full
  OpenDeck key-title font picker, copy `static/fonts/` from upstream before
  building.
- `src-tauri/` (the OpenDeck Rust backend) — we provide our own backend
  (mirajazz sidecar + Elgato-SDK plugin host) via the C++ `OpenDeckBridge`.
- `node_modules/`, `build/`, `.svelte-kit/`, repo scaffolding.

## How it is built and bridged

`scripts/build-webui.sh` runs `vite build` (→ `opendeck/build/`, a static SPA),
then injects `resources/opendeck-shim/tauri-shim.js` as the first `<head>`
script of `index.html`. The shim maps the frontend's `@tauri-apps/api` calls
onto our `OpenDeckBridge` over a QWebChannel (no Tauri runtime). CMake bundles
the result into the app under the `:/opendeck` Qt resource prefix when
`-DAJAZZ_BUILD_WEBUI=ON`. The default build (OFF) does not require Node and is
unaffected.
