# Native Cross-Platform Hardware Monitor — Design & Plan

> **Status:** Design (research complete, no code yet).
> **Date:** 2026-07-01.
> **Goal:** Instead of trying to *run* Windows-only monitor plugins (HWiNFO,
> System Monitor) on Linux/macOS, **reimplement their function natively** as a
> cross-platform Stream Deck plugin, using **btop / btop4win** (both Apache-2.0)
> as the license-compatible donor of per-OS sensor-access techniques.
> **Why this instead of a compatibility runner:** the hard Windows-only plugins
> are native Win32 binaries bridging to Windows-only data sources — no runtime
> trick recovers them (see `docs/architecture/WINDOWS-PLUGIN-COMPAT.md`). Their
> *functionality* (reading sensors), however, is fully replicable natively on
> every OS. One native plugin > N unrunnable Windows binaries.

______________________________________________________________________

## 1. Decisions at a glance

| Decision | Choice | Rationale |
| --- | --- | --- |
| **Delivery vehicle** | **Rust `.sdPlugin`** built on the `openaction` crate, **bundled with the app** | Proven cross-platform in this app today (`me.amankhanna.oasystem`, `com.amansprojects.starterpack` already run on Linux via per-target-triple binaries); out-of-process (never blocks the UI); `setFeedback` dial support already handled by `encoder_layout_renderer`. |
| **Sensor source** | **btop / btop4win collectors, reused directly** (both Apache-2.0), via a **standalone C++ `btop-metrics-helper` binary** that emits a unified JSON schema on stdout; the Rust plugin spawns it and reads the stream | User directive: "read directly from btop, it already does everything." btop covers everything `sysinfo` misses (GPU, temps) and is battle-tested per-OS. A separate helper process is a fault + privilege boundary (btop collectors `throw` + use global state; Windows LHM needs admin + a ring-0 driver — do not elevate the whole plugin). Matches the project's out-of-process house style. |
| **Graphic style** | **btop's visual aesthetic**, rendered as raster key/dial images with **`tiny-skia`** + btop's gradient palette | User directive: "use a similar graphic style." Gradient-filled area sparklines + value colored by btop's default-theme gradients. |
| **Action model** | **One parameterized "Monitor" action** + a few specialized ones | The market (HWiNFO, System Vitals) converged on a metric-picker action, not dozens of fixed actions. |
| **Differentiators** | **Threshold colors + sparkline rendering**, and **first-class dial/touch-strip** | These are what users actually want and where incumbents are weakest. |

## 1b. Finalized architecture (source-verified, 2026-07-01)

```text
Rust openaction plugin (ajazz-sysmon)
  ├─ spawns → btop-metrics-helper (C++, btop/btop4win collectors, per-OS)
  │             └─ emits one JSON object per line on stdout (unified schema)
  ├─ parses JSON → per-metric ring buffers
  └─ renders btop-style PNG (tiny-skia + btop gradient LUTs) → Instance.set_image()
       + Instance.set_feedback() for dials
```

**Plugin shell — `openaction` v2.6.0** (`OpenActionAPI/rust`, needs Rust ≥ 1.85;
we have 1.96). Verified API: implement the `Action` trait
(`const UUID`, `type Settings`, async `will_appear`/`will_disappear`/`key_down`/
`dial_rotate(ticks,pressed)`/`touch_tap(pos,hold)`/…). Outbound are methods on
`&Instance`: `set_title(Option<..>, state)`, `set_image(Option<data-uri|path>,
state)`, `set_feedback(&impl Serialize)`. `main()` = `register_action(a).await;
run(std::env::args().collect()).await`. The crate parses `-port/-pluginUUID/…`
itself. Lifecycle: one `tokio::spawn` poll task per visible instance, aborted on
`will_disappear` (matches the pattern proven live for `oasystem`).

**Metrics helper — reuse btop's collectors directly.** btop has NO API/JSON mode;
"read directly from btop" = compile its collector translation units into our own
tiny `main()`. The Linux collector is cleanly decoupled: **0 refs to
`Term::`/`Draw::`/`Global::`**. Minimal build = `src/<os>/btop_collect.cpp` +
`btop_shared.cpp` + `btop_config.cpp` + `btop_tools.cpp` + `btop_log.cpp` + a
~15-line `shim.cpp` (defining the ~6 `Runner::`/`Global::` atomics/strings btop.cpp
would provide) + **`fmt`** + **`-ldl`**. The one mandatory init is **`Shared::init()`**
(Config uses compiled-in defaults — no config file needed). Collect funcs return
references to deque histories (`.back()` = current); call **twice with a sleep**
for %-based metrics (cpu/net deltas). Per-OS difficulty: **Linux easy**, **macOS
moderate** (IOKit/CoreFoundation + `osx/smc.*` + `osx/sensors.*`), **Windows
hardest** (separate `btop4win` tree; GPU/temp via the **LHM DLL, admin required**;
GPU folded into `Cpu::` not `Gpu::`).

**Unified JSON schema** (per-OS adapter normalizes into canonical units — bytes,
°C, W, MHz, RPM, %):

```jsonc
{ "schema":1, "backend":"btop-linux|btop-macos|btop4win-lhm", "ts_ms":0,
  "cpu": { "percent":0.0, "per_core":[0.0], "temp_c":0, "freq_mhz":0, "watts":0.0 },
  "mem": { "used_bytes":0, "total_bytes":0, "percent":0.0,
           "swap_used_bytes":0, "swap_total_bytes":0 },
  "net": { "iface":"", "down_bytes_s":0, "up_bytes_s":0,
           "down_total_bytes":0, "up_total_bytes":0 },
  "gpu": [ { "name":"", "util_percent":0, "temp_c":0,
             "vram_used_bytes":0, "vram_total_bytes":0, "power_w":0.0, "fan_rpm":0,
             "core_clock_mhz":0, "mem_clock_mhz":0,
             "supported": { "util":true,"temp":true,"vram":true,"power":true,
                            "fan":false,"core_clock":true,"mem_clock":true } } ] }
```

Windows note: parse the **full** LHM sensor dump (`FetchLHMValues()`), not just
what btop4win's TUI keeps — recovers GPU **power + fan** that btop4win discards.

**Render — btop's aesthetic via `tiny-skia`** (pure-Rust AA path fill + native
`LinearGradient`) + `ab_glyph` for the value text → PNG → `set_image`. Reproduce
btop's *gradient-filled area sparkline*: bottom-anchored polygon, vertical
`LinearGradient` using the metric's btop stops; big value colored by the same
gradient at its magnitude; dark `#000` bg; slim rounded accent frame optional.
**btop default-theme gradient stops** (start → mid → end), reused with attribution:

| metric | start | mid | end |
| --- | --- | --- | --- |
| cpu | `#77ca9b` | `#cbc06c` | `#dc4c4c` |
| temp | `#4897d4` | `#5474e8` | `#ff40b6` |
| mem used | `#592b26` | `#d9626d` | `#ff4769` |
| download | `#291f75` | `#4f43a3` | `#b0a9de` |
| upload | `#620665` | `#7d4180` | `#dcafde` |

Gradient = linear RGB lerp, two-segment when a mid stop exists; precompute a
101-entry LUT (matches btop `generateGradients()`). Dials get a wide scrolling
strip (and net = mirrored up/down dual graph). Render host-side with QPainter is a
fallback, but plugin-side tiny-skia keeps the monitor self-contained.

## 2. The metric surface (80/20, from ecosystem survey)

**Tier 1 — the core users actually bind:** CPU total %, CPU temp, RAM used %,
GPU util %, GPU temp, Disk usage %, Net down/up. Plus the two rendering
features that matter more than any extra metric: **color thresholds**
(green/amber/red) and a **sparkline/mini-graph**.

**Tier 2:** VRAM used (MB + %), GPU power, CPU power, RAM used (GB), per-core %,
per-interface net + ping/latency, a proper **dial** experience.

**Tier 3:** motherboard temps / fan RPM / voltages (HWiNFO-style sensor picker),
battery %, uptime, per-drive free space + I/O.

**Out of scope v1:** top-processes-by-CPU/RAM, FPS/frametime (not native to the
Stream Deck monitor ecosystem; belong to TUI monitors / RTSS-PresentMon).

## 3. Sensor collection — per-OS source matrix

`sysinfo` handles the unmarked rows. Rows marked **[btop]** are where we port
btop/btop4win access code (Apache-2.0, with NOTICE attribution).

| Metric | Linux | macOS | Windows |
| --- | --- | --- | --- |
| CPU % / per-core | `sysinfo` (`/proc/stat`) | `sysinfo` (Mach) | `sysinfo` (NtQuery) |
| RAM / swap | `sysinfo` | `sysinfo` | `sysinfo` |
| Disk usage / I/O | `sysinfo` | `sysinfo` | `sysinfo` |
| Network up/down | `sysinfo` | `sysinfo` | `sysinfo` |
| Battery / uptime | `sysinfo` | `sysinfo` | `sysinfo` |
| **CPU temp** | `sysinfo`/sysfs hwmon (no priv) | **[btop]** SMC (Intel) / IOHID (Apple Silicon) | **[btop]** LibreHardwareMonitor DLL **+ admin** — else omit (basic tier) |
| **GPU util/temp/VRAM/power** | **[btop]** NVML (`dlopen libnvidia-ml`), ROCm SMI, Intel | **[btop]** IOReport/IOHID (Apple Silicon; private API) | **[btop]** LibreHardwareMonitor **+ admin** |

**The three hard-truth caveats (design around them, don't pretend):**

1. **Windows CPU-temp & GPU need LibreHardwareMonitor (MPL-2.0) + Administrator**
   (LHM loads a signed kernel driver for MSR/SMBus). Mirror btop4win's split: a
   **basic tier** (no CPU temp, no GPU) that runs with no privileges, and an
   **advanced tier** that bundles LHM and asks for elevation. MPL-2.0 is
   per-file copyleft on the LHM files (shipped as a separate DLL) — fine for us,
   but it's a distinct redistribution obligation, kept out of the plugin's own
   Apache/MIT code.
2. **macOS Apple-Silicon GPU/temp use private, undocumented frameworks**
   (IOReport/IOHIDEventSystem). Works unsandboxed; flag as best-effort and
   fragile across OS releases.
3. **AMD/Intel GPU on Linux** is the market gap (incumbents are NVIDIA/CUDA-only)
   — ROCm is often absent, so use the `/sys/class/drm/card*/device` sysfs
   fallback btop uses. This is our leapfrog opportunity.

## 4. Rendering & interaction

- **Key render:** action image composed in the plugin — value + unit, optional
  title, **threshold color band**, and a **sparkline** of recent history behind
  the value. Push via `setImage`/`setTitle`. Display modes: `text | sparkline |
  ring/bar`.
- **Dial / touch-strip (Stream Deck +):** drive the encoder feedback layout via
  `setFeedback` (the app's `encoder_layout_renderer` already renders
  `$X1/$A0/$A1/$B1/$B2/$C1` = title/value/icon/indicator). One monitor cell per
  dial with **rotate-to-switch-metric** and **press-to-cycle**, and a full-width
  touch-strip sparkline. Incumbents underserve dials — this is where we win.
- **Refresh:** ~1 Hz default (configurable); the persistent-handle render path
  sustains it. No per-keystroke rescans (constitution IV).

## 5. Action surface to ship

1. **System Monitor (key)** — one action, PI metric picker over the Tier-1 set;
   config: target/instance selector, units (%, °C/°F, MB/GB, Mbps), refresh,
   title, threshold colors, display mode. *(Matches HWiNFO + easy-sysinfo + most
   of System Vitals in a single action.)*
2. **GPU Monitor (key)** — GPU-specialized: util/temp/VRAM/clock/power/fan with
   explicit **NVIDIA + AMD + Intel** backends.
3. **System Monitor (dial)** — same engine on an encoder; rotate/press/touch-strip.
4. **Network Monitor** — per-interface down/up + optional ping to a configurable
   host.
5. **(Tier 3)** Sensor picker (any exposed sensor), Battery/Uptime, Disk detail.

## 6. Integration points in this app (verified file paths)

- **Vehicle proof:** `me.amankhanna.oasystem.sdPlugin` and
  `com.amansprojects.starterpack.sdPlugin` ship per-target-triple native
  binaries and run on Linux now.
- **Manifest / CodePath resolution:** `src/app/src/plugin_manifest.cpp`
  (`CodePaths` target-triple map + `CodePathLin`; `resolveEffectiveCodePath()`).
- **Spawn dispatch (native binary branch):** `src/app/src/plugin_manager.cpp`.
- **Device I/O (`setImage`/`setTitle`/`setFeedback`):**
  `src/app/src/plugin_device_bridge.cpp`.
- **Encoder feedback render:** `src/app/src/encoder_layout_renderer.cpp`
  (`renderEncoderLayout`).
- **Bundling (new):** add `resources/bundled-plugins/<uuid>.sdPlugin/` and
  install it into the plugins dir via CPack rules (`.deb`/`.rpm`/`.flatpak`) and
  the Flatpak manifest payload, so it's present in the action list on first run
  on all three OSes.
- **Constraints:** the plugin is external → **no COD-031 nlohmann concern**; the
  **mirajazz sidecar is untouched** (stream-dock transport is orthogonal —
  monitor I/O flows through `PluginDeviceBridge`).

## 7. Licensing

- **btop / btop4win: Apache-2.0** — code is reusable (copy + adapt), not merely
  reference. Obligations: preserve Apache headers on any lifted file, add a
  `THIRD_PARTY_NOTICES`/`NOTICE` crediting btop, state changes. Its GPL ancestors
  (bashtop/bpytop) are NOT involved.
- **LibreHardwareMonitor: MPL-2.0** (Windows temp/GPU path only) — per-file
  copyleft; ship as a separate DLL, keep it out of our Apache/MIT plugin code.
- **`sysinfo` crate: MIT** — permissive.
- The plugin ships under a permissive license; nothing here forces copyleft on
  the app.

## 8. Phased plan (methodical; verify each phase live)

- **Phase 0 — `btop-metrics-helper` (Linux, highest-uncertainty first).** ✅ DONE
  (`98bc188f`). Compile btop's Linux collectors into a standalone binary (5 btop
  TUs + shim + fmt + -ldl), init `Shared::init()`, emit the unified JSON schema
  (CPU% first, then mem/net/gpu) on stdout. Verified real numbers out.
- **Phase 1 — Rust `openaction` plugin consuming the helper + btop-style render.**
  ✅ DONE (`47e0f95f`). Plugin skeleton (verified API), spawns the helper, one
  `CPU %` action rendering a btop-style gradient sparkline PNG via tiny-skia,
  bundled + discovered. Live-verified on the real AKP05E (bind → `willAppear` →
  live tile), then the parameterized **Monitor** action + PI metric picker.
- **Phase 2 — Rendering differentiators.** ✅ DONE (`62018958`). Threshold colors
  + sparkline; per-metric gradients/scales/thresholds via the PI.
- **Phase 3 — GPU (btop donor).** ✅ DONE (`b2727c2a`). NVML (dlopen) verified
  exact vs nvidia-smi on an RTX 2080 SUPER; ROCm SMI + amdgpu-sysfs + Intel PMU
  compiled in; **GPU Monitor** action + gpu/gpu_temp/vram picker metrics.
  macOS Apple-Silicon best-effort remains open. NOTE: NVML v1
  `nvmlDeviceGetMemoryInfo` counts driver-reserved VRAM (helper "used" ≈
  nvidia-smi used + reserved — verified via `nvidia-smi -q -d MEMORY`).
- **Phase 4 — Dial / touch-strip.** ✅ DONE (`bbf7e134` + app `ec36b878`).
  Rotate-to-switch-metric, press-to-cycle, 128 px strip sparkline tile.
  **Design deviation from this plan:** `openaction` (2.6.0, latest as of
  2026-07-02) exposes NO `set_feedback` and does not route `touchTap`, so the
  strip visual is the plugin's own tile pushed via `setImage` on the Encoder
  instance; the host routes Encoder-context `setImage` to the dial's strip zone
  (and mirrors it to the OpenDeck web UI slider). The host's `$A1/$B1/...`
  feedback-layout renderer stays available for plugins that can emit
  `setFeedback`; revisit when the crate grows the API.
- **Phase 5 — Windows temp/GPU tier.** ⛔ HARDWARE-GATED (2026-07-02). LHM DLL +
  elevation (advanced tier); basic tier stays privilege-free. Constitution
  Principle V requires live verification and no Windows box is available to this
  effort; the btop4win tree is also a separate vendor drop. Do NOT implement
  blind — pick this up on a Windows machine (build btop4win collectors, parse
  the FULL `FetchLHMValues()` dump, split basic/advanced tiers).
- **Phase 6 — Tier-2/3 + polish.** ▶ IN PROGRESS. Done: Tier-2 metrics
  (disk %, GPU power), network up/down (btop `Net::collect` in the helper),
  **ping** (TCP connect-time to 1.1.1.1:443 sampled every 2 s in the plugin —
  SYN/ACK timing, no raw-socket privilege; timeout pins 1500 ms so outages
  spike the graph), **battery %** (/sys/class/power_supply, skipped without
  one) and **uptime** (/proc/uptime, compact "3d 4h" label) — all in the
  Metric enum + PI picker, live-verified on the AKP05E (2026-07-02).
  Bundling into the app build/installers shipped as Phase 6c
  (plugins/CMakeLists.txt + seedBundledPlugins). Still open: per-sensor
  picker (which GPU / which NIC / which temp probe), user docs, CI
  cross-compile matrix.

## 9. Open decisions (for the owner)

1. **Sensor language for the hard paths:** reimplement btop's GPU/temp access in
   **Rust** (keeps one language in the plugin) vs FFI from the Rust plugin into a
   small **C++ collector lifted from btop verbatim** (less work, but cross-language
   build). Recommendation: **Rust reimplementation** for Linux (sysfs + `dlopen`
   NVML/ROCm are trivial in Rust) and macOS-Intel SMC; consider a thin C shim only
   for the genuinely hard macOS Apple-Silicon IOHID / Windows LHM paths.
2. **Windows advanced tier now or later:** ship basic-tier (no Win CPU-temp/GPU)
   first, add LHM+elevation in Phase 5 — or gate the whole GPU story on it.
3. **New plugin UUID + repo layout:** in-tree `plugins/ajazz-system-monitor/`
   (Rust crate) building into `resources/bundled-plugins/<uuid>.sdPlugin/`.

______________________________________________________________________

## Appendix — sources

- Research reports (2026-07-01): btop/btop4win collector deep-dive
  (`Aristocratos/btop` `src/{linux,osx,freebsd}/btop_collect.cpp`,
  `src/osx/smc.*`, `src/osx/sensors.*`; `aristocratos/btop4win`
  `src/btop_collect.cpp` + LHM DLL), monitor-plugin action-surface survey
  (HWiNFO, System Vitals, StreamDeckGpu, easy-sysinfo, Win Tools, BarRaider),
  and this app's plugin-integration model.
- Companion doc: `docs/architecture/WINDOWS-PLUGIN-COMPAT.md` (why running the
  Windows binaries without Wine is infeasible for the hard cases).
- Proven vehicle in-repo: `me.amankhanna.oasystem.sdPlugin`,
  `com.amansprojects.starterpack.sdPlugin`.
