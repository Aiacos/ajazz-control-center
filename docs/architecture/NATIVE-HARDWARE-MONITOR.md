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
| **Core sensor source** | Rust **`sysinfo`** crate | Covers CPU%, per-core, RAM/swap, disk usage+I/O, network, battery, uptime, load — and temperatures on Linux/macOS — cross-platform, steady-state-allocation-free. |
| **Hard sensor source** | **btop / btop4win techniques** (Apache-2.0), reimplemented in Rust | `sysinfo` does NOT cover GPU, and Windows CPU-temp is a special case. btop is the reference-grade donor: NVML/ROCm/Intel for GPU, SMC/IOHID for macOS temps, LibreHardwareMonitor for Windows temps. |
| **Action model** | **One parameterized "Monitor" action** + a few specialized ones | The market (HWiNFO, System Vitals) converged on a metric-picker action, not dozens of fixed actions. |
| **Differentiators** | **Threshold colors + sparkline rendering**, and **first-class dial/touch-strip** | These are what users actually want and where incumbents are weakest. |

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

- **Phase 0 — Scaffold & prove the pipe.** Rust `openaction` plugin skeleton,
  one `CPU %` action, per-triple build, bundled + discovered. Live-verify via the
  debug channel: bind → `willAppear` → live `setTitle`, render on the real AKP05E.
  *(Reuses the exact flow already verified for `oasystem`.)*
- **Phase 1 — Tier-1 metrics via `sysinfo`.** CPU%, RAM%, disk%, net up/down +
  Linux/macOS CPU temp. The parameterized **System Monitor (key)** action + PI
  metric picker.
- **Phase 2 — Rendering differentiators.** Threshold colors + sparkline; display
  modes text/sparkline/ring.
- **Phase 3 — GPU (btop donor).** NVML/ROCm/Intel on Linux; the **GPU Monitor**
  action. macOS Apple-Silicon best-effort.
- **Phase 4 — Dial / touch-strip.** `setFeedback` layouts, rotate-to-switch,
  press-to-cycle, strip sparkline.
- **Phase 5 — Windows temp/GPU tier.** LHM DLL + elevation (advanced tier);
  basic tier stays privilege-free.
- **Phase 6 — Tier-2/3 + polish.** Network+ping, sensor picker, battery/uptime,
  bundling into all three installers, docs, CI cross-compile matrix.

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
