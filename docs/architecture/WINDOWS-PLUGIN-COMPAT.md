# Running Windows-only Stream Deck Plugins without Wine — Feasibility

> **Status:** Research (no code). **Date:** 2026-07-01.
> **Question:** Can we run Windows-only Elgato `.sdPlugin` plugins (which ship a
> `.exe`) on Linux/macOS **without Wine**?
> **Short answer:** For a subset yes; for the plugins that actually matter to us
> (HWiNFO, System Monitor) **no** — they are native Win32 binaries bridging to
> Windows-only data sources. The right move for those is **native
> reimplementation**, tracked in `docs/architecture/NATIVE-HARDWARE-MONITOR.md`.

______________________________________________________________________

## 1. The core insight

A Stream Deck plugin is **not a GUI Windows app** — it is a headless background
process that talks to the host **only over a WebSocket** (Elgato SDK JSON
protocol: `registerPlugin`, `willAppear`, `keyDown`, `setTitle`, `setImage`, …).
The host launches the plugin's executable with CLI args (`-port`, `-pluginUUID`,
`-registerEvent`, `-info`) and it connects back. So "run a Windows plugin on
Linux" reduces to exactly one question: **can the WebSocket-speaking backend run
under a non-Windows runtime?** Portability is a property of *how the backend was
packaged*, not of the protocol (transport-neutral) or the Property Inspector
(always HTML/JS).

## 2. Our stack already does most of the runtime dispatch

The embedded OpenDeck launcher
(`src/app/webui/opendeck/src-tauri/src/plugins/mod.rs`, `initialise_plugin`)
already resolves the code path per-OS and dispatches by file type:

- `.html/.htm/.xhtml` → in-process WebView (no subprocess). **Inherently portable.**
- `.js/.mjs/.cjs` → **system `node` (≥ v20)** (`flatpak-spawn --host node` under Flatpak).
- native ELF/Mach-O → `chmod 755` + exec directly.
- `CodePathWin`-only (a `.exe`) → **Wine**, as an explicit opt-in fallback.

Plus OpenDeck extensions: **`CodePathLin`**, a **`CodePaths`** map keyed by Rust
target triple, and per-OS **`manifest.linux.json`** overlays. Our own
`src/app/src/plugin_manifest.cpp::resolveEffectiveCodePath()` gates by **binary
format**: HTML/JS runs via the runtime; a bare Windows `.exe` is left unresolved
and skipped. (The manifest `OS[]` array is NOT the fence — a LOCKED Linux-accept
rule lets vendor manifests past; the binary format is the real fence.)

## 3. Runtime taxonomy — portability without Wine

| Runtime type | How it ships | No-Wine path | Verdict |
| --- | --- | --- | --- |
| HTML/JS in WebView (all PIs; some plugins) | `.html`+JS | host loads it; no external runtime | **Feasible** (already works) |
| Node via bundled/system Node (v2 SDK, `Nodejs` field) | `.js` source | run with system `node ≥20` | **Feasible** |
| Node compiled to `.exe` (`pkg`/`nexe`/SEA) | self-contained `.exe` | recover the `.js` (point `CodePathLin` at source), run with system Node | **Feasible if source reachable**; `pkg` default V8-**bytecode** is version-locked → fragile |
| .NET Core / .NET 5+ | framework-dependent `.dll` or self-contained `.exe` | `dotnet Plugin.dll` (ignore the apphost `.exe`) | **Feasible** (framework-dependent); self-contained Windows-RID needs per-OS rebuild |
| .NET Framework | Windows-CLR `.exe` | **Mono** (`mono Plugin.exe`) — a native CLR, not Wine | **Partial** — breaks on WPF/WinForms/Win32 P-Invoke |
| Native C++/Win32 `.exe` | PE binary | none | **Not without reimplementation** |
| Bridge-to-Windows-app (HWiNFO SMEM, Voicemod) | any | none | **Not without reimplementation** against a native data source |

**Detection heuristics for an unknown `.exe`:** `file`/PE header; `strings | grep
mscoree`/`BSJB` + `*.runtimeconfig.json` sibling ⇒ .NET (Core vs Framework via
`.NETCoreApp` vs `.NETFramework`); `strings | grep pkg/prelude|__nexe|NODE_SEA_BLOB`
⇒ bundled Node; `asar`/`electron` ⇒ Electron; imports dominated by
`user32`/`gdi32`/`kernel32` on a small binary ⇒ native Win32.

## 4. Two orthogonal axes

- **Runtime portability** — can the backend execute natively? (§3)
- **Functional portability** — even if it runs, does it depend on a Windows-only
  API or companion app? Registry / named pipes / **shared memory (HWiNFO)** /
  WMI / a Windows desktop app (**Voicemod**, iCUE). Shimmable when it's a generic
  OS service with a Unix equivalent (`/proc`, sysfs, IOKit, sockets);
  **fundamental** when it bridges to a specific Windows process.

A plugin is shippable cross-platform only if **both** axes pass. "Portable but
useless" (Voicemod) is the trap.

## 5. Our actual catalog (8 installed `.sdPlugin`)

| Plugin | Runtime | Runs on Linux now? | Verdict |
| --- | --- | --- | --- |
| `com.amansprojects.starterpack` | Rust native (ELF per triple) | **Yes** | already cross-platform |
| `me.amankhanna.oasystem` (System Info) | Rust native | **Yes** | already cross-platform |
| `opendeck-plugin-{ButeBk,gfpXkD,NZdxxI}` | Rust native (ambiso AKP05 device plugin — 3 duplicate copies) | **Yes** | already cross-platform |
| `net.voicemod.windowsdesktop` | HTML/JS | connects, but inert | **runtime-portable, functionally Windows-bound** (needs Voicemod Desktop) |
| `com.exension.hwinfo` (HWiNFO) | native Win32 `.exe` | **No** | **not without reimplementation** (+ HWiNFO shared-memory source is Windows-only) |
| `com.hotspot.streamdock.system.monitor` | native Win32 Qt5 `.exe` | **No** | **not without reimplementation** |

**6 of 8 already run on Linux.** Only HWiNFO and HotSpot System Monitor are hard
— and both are functionally Windows-bound, so no runtime trick helps.

## 6. Recommendation

- **Realistic coverage** of the truly-Windows-only pool via a compatibility
  runner: ~25–45%, dominated by non-bytecode Node bundles and framework-dependent
  .NET. The easy wins (HTML/JS, .NET Core) are largely *already* cross-platform.
- **Two low-effort levers**, if/when we build a compatibility runner (extends the
  existing dispatcher): (1) detect a .NET CLR header → run `dotnet Plugin.dll`;
  (2) extract non-bytecode Node bundles → system Node. Keep the clear gate-out +
  reason string for native Win32.
- **But for the plugins that matter to us** (system/hardware monitors), the right
  strategy is **native reimplementation**, not execution — the incumbents'
  *function* is fully portable even though their *binaries* are not, and a native
  equivalent already exists in-stack (`oasystem`). See
  `docs/architecture/NATIVE-HARDWARE-MONITOR.md`.

______________________________________________________________________

## Appendix — sources

Research reports (2026-07-01): Elgato SDK plugin runtime taxonomy (manifest
`CodePath*`/`Nodejs`/`OS[]`; OpenDeck `mod.rs` dispatch), no-Wine execution
techniques (`pkg`/`nexe`/SEA extraction, `dotnet`/Mono, dependency shims), and
our installed-plugin inventory. Key repo refs:
`src/app/webui/opendeck/src-tauri/src/plugins/mod.rs`,
`src/app/src/plugin_manifest.cpp`. External: Elgato SDK manifest docs,
`elgatosf/streamdeck` (Node v2 SDK), `vercel/pkg` + `LockBlock-dev/pkg-unpacker`,
.NET/Mono docs, HWiNFO shared-memory (Windows-only).
