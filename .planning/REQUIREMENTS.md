# Requirements: AJAZZ Control Center

**Defined:** 2026-05-13
**Milestone:** v1.1 — Device lifecycle hardening + scaffolding-to-functional
**Core Value:** Modern open cross-platform control center for AJAZZ devices with Qt 6 / QML UI + Python plugin system.

## v1.1 Requirements

Requirements for this milestone. Each maps to one roadmap phase (Phases 3-8). Carried-over numbering: v1.0 used no REQ-IDs (pre-GSD retro), so v1.1 starts each category at `-01`.

### Architectural Decisions

Phase 3 deliverables — written rationale gating Phases 4-7.

- [x] **ARCH-01**: Project records WR-01 trust-roots parser choice (`nlohmann::json` PRIVATE-linked to `ajazz_plugins` vs in-tree 5-state scanner) with written rationale and explicit threat-model framing.
- [x] **ARCH-02**: Project records `HotplugMonitor` mock-seam approach (test-only `injectEvent` shim vs subclassable interface vs other) with rationale.
- [x] **ARCH-03**: Project confirms `DeviceRegistry` slot ownership migration (`unique_ptr<IDevice>` → `shared_ptr<IDevice>`) is in-scope and lands before Phase 5.

### Hot-Plug Hardening

Phase 4 deliverables — disconnect-during-use safety, multi-device UX, test harness.

- [x] **HOTPLUG-01**: User can disconnect a device while interacting with it without crashing the app (Pitfall 1 UAF closed via `shared_ptr<IDevice>` migration).
- [x] **HOTPLUG-02**: Disconnected device's sidebar row stays in place with an offline badge; the row does not disappear, and a subsequent reconnect re-binds silently.
- [x] **HOTPLUG-03**: User's sidebar selection focus is retained across a disconnect/reconnect cycle.
- [x] **HOTPLUG-04**: Sidebar device rows are sorted by `(deviceClass, codename)` lexicographically — stable across hot-plug events (no reorder-by-recency).
- [x] **HOTPLUG-05**: Hot-plug events are coalesced by `(vid, pid, serial)` with 250-500ms trailing-edge debounce in `Application::onHotplug` before consumers see them.
- [x] **HOTPLUG-06**: Multi-device integration tests cover disconnect-during-use, reconnect, and device-shuffle scenarios via `MockHidEnumerator` + `HotplugMonitor::injectEvent` shim.
- [x] **HOTPLUG-07**: 2026-05-12/13 hot-plug debugging fix is documented in a phase artefact (what was broken, what changed, why 3 devices now work).

### Time-Sync Scaffolding

Phase 5 deliverables — capability flag pattern, 5-layer slice top→bottom.

- [x] **TIMESYNC-01**: User can click a per-device "Sync now" button on devices that advertise `hasClock` capability; the button is hidden on devices without it.
- [x] **TIMESYNC-02**: All 4 functional backends (AKP153, AKP03, AKP05, AKB980 PRO) implement `IClockCapable::setTime()` as stubs returning `Result::NotImplemented` (no crashes, no false success).
- [x] **TIMESYNC-03**: User can enable a global "auto-sync time on device connect" toggle on the Settings page (`QSettings`-persisted).
- [x] **TIMESYNC-04**: Auto-sync fires 300ms after a stable device arrival, re-validates capability + connectedness at firing time, and gracefully handles `nullptr` from `dynamic_cast<IClockCapable*>` (Pitfall 2 mitigation).
- [x] **TIMESYNC-05**: `NotImplemented` results surface as an exclamation glyph + tooltip on the device row, not a success toast. Backend WARN logs are `std::once_flag`-gated to avoid spam (Pitfall 14).
- [x] **TIMESYNC-06**: `TimeSyncService` is registered as a QML singleton via `qmlRegisterSingletonInstance` with `static_assert(!std::is_default_constructible_v<TimeSyncService>)` build-break (Pitfall 4 mitigation).

### Win32 Plugin Host (CR-01)

Phase 6 deliverables — Win32-only fix, Windows CI required.

- [x] **WIN32-01**: Win32 OOP plugin host spawns Python child processes with a per-spawn UTF-16 environment block (`CREATE_UNICODE_ENVIRONMENT`), built from `GetEnvironmentStringsW` + 3 Python overrides + `\0\0` terminator + case-insensitive sort + preserved `=`-prefixed drive-letter entries.
- [x] **WIN32-02**: All three `_putenv_s` calls in `out_of_process_plugin_host_win32.cpp:463-467` are removed in the same commit as the env-block fix (no belt-and-braces leftover — Pitfall 6).
- [x] **WIN32-03**: Windows CI exercises the OOP host child spawn and asserts via `_wgetenv(L"PYTHONPATH")` that the parent process's env is unchanged after a child completes.
- [x] **WIN32-04**: A CI smoke test resolves the duplicate-key precedence question (first-wins vs last-wins on inherited `PYTHONPATH`) and the result is documented in the phase artefact.

### Trust-Roots Parser (WR-01)

Phase 7 deliverables — implementation of ARCH-01 choice, lockstep across two TUs.

- [x] **TRUST-01**: `loadTrustRoots` uses the parser chosen in ARCH-01. The mini-grep parser is fully removed from both `manifest_signer.cpp` and `manifest_signer_win32.cpp` in lockstep (drift re-introduces WR-01).
- [x] **TRUST-02**: `loadTrustRoots` enforces a 1 MB byte-cap (fails closed) and a 1024-entry cap on input to bound parser DoS (Pitfall 7).
- [x] **TRUST-03**: Test suite covers BOM, escape sequences, nested structures, and embedded NUL bytes in trust-roots input. Fuzz corpus runs \<1s on 100 KB inputs.
- [x] **TRUST-04**: Public-API header for `loadTrustRoots` documents the 0600 file-permissions assumption between read and verify (Pitfall 8 TOCTOU framing).

### Scaffolded-Device Wiring

Phase 8 deliverables — maturity-tier infrastructure + opportunistic promotion.

- [x] **DEVICES-01**: `docs/_data/devices.yaml` gains a `maturity` field per device with tiers: Scaffolded / Probed / Partial / Functional / Verified.
- [x] **DEVICES-02**: `DeviceModel` exposes a `MaturityRole`; QML sidebar surfaces the tier (badge or tooltip).
- [x] **DEVICES-03**: README regeneration includes per-family "what works / what doesn't" tooltips populated from the YAML.
- [x] **DEVICES-04**: 1-2 scaffolded stream-dock-family devices (siblings of AKP153/AKP03/AKP05) are promoted Tier 0 (Scaffolded) → Tier 2 (Partial). Specific candidates picked during phase planning.

## Out of Scope

Explicitly excluded for v1.1. Documented to prevent scope creep — sourced from FEATURES.md anti-feature inventory.

| Feature                                             | Reason                                                                                                                                                      |
| --------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Modal "Device disconnected, retry?" dialog          | Anti-feature — preferred UX per all 5 OSS competitor apps is silent reconnect; modal interrupts workflow.                                                   |
| Auto-retry loop hammering `hid_open` on disconnect  | Anti-feature — wastes CPU; coalescing + offline badge is the correct shape.                                                                                 |
| Prompt-user-which-profile on reconnect              | Anti-feature — silent rebind to last profile is the convergent UX.                                                                                          |
| Sidebar reorder by recency / arrival time           | Anti-feature — destroys focus retention; stable lexicographic sort is HOTPLUG-04.                                                                           |
| Removal sound                                       | Anti-feature — noisy; offline badge sufficient.                                                                                                             |
| Full QML scene reload on disconnect                 | Anti-feature — flickers + loses state; targeted role update only.                                                                                           |
| Big-bang "promote all 7 scaffolded devices"         | Too high variance for v1.1; defer 5-6 promotions to v1.1.x or v1.2.                                                                                         |
| AKB980 PRO promotion                                | Vendor driver is Delphi installer requiring `wine`/`innoextract`; not in dev env. Defer.                                                                    |
| Real `IClockCapable::setTime` wire formats          | Blocked on firmware support — no AJAZZ device exposes a host-settable RTC per vendor recon (`d5616ef`). Defer to v1.2+ when a backend reverse-engineers it. |
| Interval-based time re-sync timer                   | Anti-feature — drift-detection-then-resync, not time-keeping. Defer to v1.2+.                                                                               |
| Device→host time read-back                          | Anti-feature for v1.1 — read-back is a different surface (firmware-dependent); not needed for one-shot set.                                                 |
| Per-device timezone offset                          | Anti-feature — UTC at the interface boundary is the simpler primitive.                                                                                      |
| World-clock / NTP client / sync-history log surface | Anti-feature — out-of-scope for control-center; user has system clock for these.                                                                            |
| Render-time-on-keyface clock widget                 | Different feature (image-upload render path); not time-sync scaffolding.                                                                                    |
| "Time synced" toast on `NotImplemented` result      | Anti-feature — lying success UX. Use exclamation glyph + tooltip (TIMESYNC-05).                                                                             |
| Trompeloeil / FakeIt mocking frameworks             | In-tree mock seam (HOTPLUG-06) is sufficient and matches `FakeAsyncExecutor` precedent.                                                                     |
| simdjson / RapidJSON / RapidYAML for WR-01          | Use-case mismatch / upstream stalled / not JSON. nlohmann or in-tree scanner per ARCH-01.                                                                   |
| `nlohmann::json` in `ajazz_core` or any header      | Crosses COD-031 boundary — `ajazz_plugins`-private if ARCH-01 picks nlohmann at all.                                                                        |
| Boost.Process for CR-01                             | Wildly disproportionate; pure `processthreadsapi.h` is ~40 LoC self-contained.                                                                              |
| Vendor-driver bundling / wine'd installers          | Not in scope — clean-room reverse engineering only.                                                                                                         |
| Telemetry / device usage metrics                    | Anti-feature — privacy + scope creep.                                                                                                                       |

## Traceability

Filled by roadmapper during Phase 3-8 mapping. Each requirement maps to exactly one phase.

| Requirement | Phase   | Status   |
| ----------- | ------- | -------- |
| ARCH-01     | Phase 3 | Locked   |
| ARCH-02     | Phase 3 | Locked   |
| ARCH-03     | Phase 3 | Locked   |
| HOTPLUG-01  | Phase 4 | Complete |
| HOTPLUG-02  | Phase 4 | Complete |
| HOTPLUG-03  | Phase 4 | Complete |
| HOTPLUG-04  | Phase 4 | Complete |
| HOTPLUG-05  | Phase 4 | Complete |
| HOTPLUG-06  | Phase 4 | Complete |
| HOTPLUG-07  | Phase 4 | Complete |
| TIMESYNC-01 | Phase 5 | Complete |
| TIMESYNC-02 | Phase 5 | Complete |
| TIMESYNC-03 | Phase 5 | Complete |
| TIMESYNC-04 | Phase 5 | Complete |
| TIMESYNC-05 | Phase 5 | Complete |
| TIMESYNC-06 | Phase 5 | Complete |
| WIN32-01    | Phase 6 | Complete |
| WIN32-02    | Phase 6 | Complete |
| WIN32-03    | Phase 6 | Complete |
| WIN32-04    | Phase 6 | Complete |
| TRUST-01    | Phase 7 | Complete |
| TRUST-02    | Phase 7 | Complete |
| TRUST-03    | Phase 7 | Complete |
| TRUST-04    | Phase 7 | Complete |
| DEVICES-01  | Phase 8 | Complete |
| DEVICES-02  | Phase 8 | Complete |
| DEVICES-03  | Phase 8 | Complete |
| DEVICES-04  | Phase 8 | Complete |

**Coverage:**

- v1.1 requirements: 28 total
- Mapped to phases: 28
- Unmapped: 0 ✓

______________________________________________________________________

*Requirements defined: 2026-05-13*
*Last updated: 2026-05-13 after `/gsd-new-milestone` (v1.1 bootstrap)*
