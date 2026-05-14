---
phase: 5
phase_slug: time-sync-scaffolding
gathered: 2026-05-14
status: Ready for planning
mode: autonomous-interactive
---

# Phase 5: Time-Sync Scaffolding — Context

**Gathered:** 2026-05-14 via `/gsd-autonomous --interactive`
**Source for recommendations:** `docs/superpowers/specs/2026-05-13-time-sync-design.md` (5-layer design, all 5 open questions resolved) + `docs/superpowers/plans/2026-05-13-time-sync.md` (8-task pre-written plan) + Phase 4 D-01..D-06 + Phase 4 catalog work in `62da68c`.

<domain>
## Phase Boundary

A user can issue a per-device "Sync now" or enable global auto-sync, and the system honestly surfaces `NotImplemented` for the four functional backends without lying via success toasts. Five-layer slice top→bottom: QML UI → `TimeSyncService` (Qt singleton) → `IClockCapable` capability mix-in → device backend stubs → docs + capability advertisement.

**Pre-emptive scaffolding:** no AJAZZ device firmware exposes a host-settable RTC over HID today (vendor recon `d5616ef`). All backend `setTime()` bodies log WARN once and return `Result::NotImplemented`. The day a real wire format surfaces, only the backend method body changes — UI / service / capability / docs are already in place.

Maps to requirements: TIMESYNC-01 .. TIMESYNC-06 (full text in `.planning/REQUIREMENTS.md`).

</domain>

\<spec_lock>

## Requirements (locked via design doc + adopted plan)

**The design is fully approved with all 5 open questions resolved.** See `docs/superpowers/specs/2026-05-13-time-sync-design.md` for the full design. The 8-task implementation plan at `docs/superpowers/plans/2026-05-13-time-sync.md` is adopted as the Phase 5 plan spine, with three amendments captured in D-01..D-03 below. Downstream agents (planner, executor) MUST read both docs before producing tasks.

**In scope (from design doc):**

- `Capability::Clock` bit flag in `enum class Capability` (`1u << 15` per design doc — verify bit free at planning time per Pitfall 13).
- `IClockCapable` mix-in interface with `Result setTime(std::chrono::system_clock::time_point)`.
- `bool hasClock` field on `DeviceDescriptor` (cheap UI gating, mirrors `hasRgb` / `hasTouchStrip`).
- `TimeSyncService` QML singleton (`qmlRegisterSingletonInstance` + `static_assert(!is_default_constructible_v<>)` per the v1.0 sweep).
- Manual `setSystemTimeOn(deviceId)` + auto-sync hook on `HotplugMonitor::deviceArrived` with 300 ms QTimer debounce.
- Persistent `autoSync` flag in `QSettings("Time/AutoSync")` with load-time capability validation (Pitfall 13).
- QML UI: SettingsPage `SwitchDelegate` "Auto-sync time on device connect" + per-device-row `ToolButton` "Sync time" (visible iff `hasClock` role is true).
- `DeviceModel::HasClockRole` reading `descriptor.hasClock`.
- Backend stubs across all Stream Dock variants (per D-03) and AKB980 PRO keyboard, returning `Result::NotImplemented` with `std::once_flag`-gated WARN log (Pitfall 14).
- Per-protocol-doc "## Time sync" section noting "TBD wire format".
- `devices.yaml` `clock` capability flag on every entry that gets `Capability::Clock`.
- Unit tests + integration tests per Task 4 / Task 8 of the plan.

**Out of scope (anti-features locked in REQUIREMENTS.md):**

- Real `IClockCapable::setTime` wire formats (firmware doesn't support it).
- Interval-based time re-sync timer (drift-detection ≠ time-keeping).
- Device → host time read-back.
- Per-device timezone offset (UTC at the interface).
- World-clock / NTP client / sync-history log.
- Render-time-on-keyface clock widget (separate "Clock Widget" feature, future).
- Modal "Sync failed" dialog ("Time synced" toast on `NotImplemented` is explicitly the lying-success UX we're avoiding).

\</spec_lock>

<decisions>
## Implementation Decisions (locked)

### D-01 — Adopt 8-task plan + amend for three post-2026-05-13 deltas

The 8-task plan at `docs/superpowers/plans/2026-05-13-time-sync.md` is the spine of Phase 5. Three small amendments handle changes since the plan was written:

**Delta 1 — AKP815 backend now exists (`62da68c`).** Task 2 ("Stream Dock backend stubs") extends to `Akp815Device` too. ~5 lines: add `IClockCapable` to inheritance list, advertise `Capability::Clock` in `capabilities()`, stub `setTime()` returning `NotImplemented` with `std::once_flag` WARN. Mirror of the AKP153 / AKP03 / AKP05 stubs.

**Delta 2 — 7 AKP03 rebadges + canonical AKP153 PIDs registered (`62da68c`).** Task 2's `register.cpp` modifications now cover all 17 streamdeck rows, not just the original 5. Per Phase 4 D-04, rebadges share a backend factory — so they share the `Akp03Device` stub automatically. The `register.cpp` changes set `.hasClock = true` on every streamdeck descriptor (since they all share backends that inherit `IClockCapable`); no per-rebadge branching needed.

**Delta 3 — Phase 4 D-06 `weak_ptr` cache in `DeviceRegistry::open()`.** Task 7 ("Application wires TimeSyncService") originally specified raw `IDevice*` lookup. Post-Phase-4, `open()` returns `std::shared_ptr<IDevice>` from the cache. `TimeSyncService::setSystemTimeOn(deviceId)` captures the `shared_ptr` for the duration of the call (eliminating the UAF window from Pitfall 1) and `dynamic_cast`s on `.get()`. The 300 ms auto-sync `QTimer::singleShot` lambda captures the `DeviceId` by value (not the `shared_ptr`) and re-resolves at firing time per Pitfall 2 — re-validating capability + connectedness then.

**No re-decomposition.** The existing 8-task plan is high-quality and matches the current architecture. Estimated total amendment surface: ~15-20 lines across Tasks 2, 5, and 7.

### D-02 — Manual sync UX: toast; auto-sync UX: glyph only

**User-initiated** sync (clicking "Sync now") surfaces success/failure as a **toast** plus the per-row glyph. Success toast: "Time synced to <model>"; failure toast: "Time sync not yet supported on this device" (for `NotImplemented`) or "Time sync failed: <error>" (for `IoError`). Toast policy: 1-2 cap per Pitfall 3; non-blocking; auto-dismiss after 4s for success, 6s for errors.

**Auto-sync** (fired by `HotplugMonitor::deviceArrived` hook) surfaces only via the per-row glyph (checkmark / exclamation), **never as a toast**. Auto-sync also doesn't retry on `IoError` (per design doc) — next `Arrived` event is the natural retry.

**Rationale:** Phase 4 D-01's silent-toast policy was specifically about hot-plug arrive/depart events — system-initiated, frequent (composite USB = 2 events per connect), and not user-actionable. User-initiated sync is the opposite: it's a discrete user action that demands feedback. The toast surface is reserved for *things the user did* in this codebase. Phase 5 doesn't change Phase 4 D-01; it occupies a different lane on the same surface.

### D-03 — Symmetric backend coverage: every Stream Dock variant + AKB980 PRO

Every Stream Dock backend variant (Akp153Device, Akp03Device, Akp05Device, **Akp815Device**) inherits `IClockCapable` and advertises `Capability::Clock`. `register.cpp` sets `.hasClock = true` on **all 17 streamdeck rows** — including the 7 AKP03 rebadges (Mirabox N3 / N3-rev3 / N3EN, Soomfon SE, Mars Gaming MSD-TWO, TreasLin N3, Redragon SS-551), the AKP153 canonical Mirabox V1 entry, AKP153R, the AKP153E V2 firmware entry, AKP815, and the legacy 0x3001/0x5001 placeholders. The Mirabox N4 (`0x6603:0x1007`) maps to `Akp05Device` and inherits the same stub.

The AKB980 PRO keyboard (`0x0c45:0x8009`) gets the stub treatment too per the original design (`ProprietaryKeyboard` inherits `IClockCapable`, advertises `Capability::Clock`, stub returns `NotImplemented`). VIA-protocol keyboards do **not** get `IClockCapable` — they're QMK-style, no vendor-specific clock surface.

Mouse backends (`AjSeries`) do **not** get `IClockCapable` — mice don't render time, no use case.

**Rationale:** D-04 from Phase 4 said row identity is codename, not VID/PID. Symmetrically, capability advertisement is per-backend, not per-VID/PID — so all 8 AKP03-protocol VID/PID pairs share one stub via the `Akp03Device` factory. Total stub count: 5 backend classes (`Akp153Device`, `Akp03Device`, `Akp05Device`, `Akp815Device`, `ProprietaryKeyboard`) — same effort as the original 4-stub plan, +1 for AKP815. The 17 `register.cpp` rows automatically inherit the right behavior.

### Claude's Discretion

- **`Capability::Clock` bit number:** design doc says `1u << 15`. Planner verifies the bit isn't already used (grep `enum class Capability` for `1u << 15` and adjacent values — Pitfall 13: never renumber existing bits, always append). Add a `static_assert(static_cast<unsigned>(Capability::Clock) == (1u << 15))` lock per Pitfall 13.
- **`std::once_flag`-gated WARN log placement:** per-backend, file-static `std::once_flag`. Each backend (`Akp153Device`, `Akp03Device`, `Akp05Device`, `Akp815Device`, `ProprietaryKeyboard`) gets its own flag — first call emits "setTime() not yet implemented for <codename>", subsequent calls return `NotImplemented` silently. Consistent with Pitfall 14's "log once per process lifetime" rule.
- **Toast component used:** the existing `src/app/qml/components/Toast.qml` (which Phase 4 D-01 leaves alone for hot-plug). Phase 5 may extend it to support 1-2 cap if it doesn't already (Pitfall 3 mitigation lives wherever the toast queue is).
- **Where the QSettings key lives:** `QSettings("Time/AutoSync", false)`. Load-time validation: if `true` but no connected device advertises `Capability::Clock`, log INFO ("auto-sync persisted ON but no capable device") — do not silently disable, do not silently fire (Pitfall 13).
- **Integration test harness for hotplug-driven auto-sync:** uses `HotplugMonitor::injectEvent` (ARCH-02 shim, lands in Phase 4) + `MockHidEnumerator` to drive synthetic `deviceArrived` events. Pre-Phase-4 Test infrastructure isn't there yet — plan task 4/8 sequencing must respect this.

</decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Time Sync prior work (LOCKED design + adopted plan)

- `docs/superpowers/specs/2026-05-13-time-sync-design.md` — Five-layer design, all 5 open questions resolved (UTC at interface, host→device only, no read-back, no time-zones, AKB980 stub now). 350 lines.
- `docs/superpowers/plans/2026-05-13-time-sync.md` — 8-task atomic plan (1726 lines). Adopted as Phase 5 plan spine per ROADMAP. Three amendments per D-01.

### Phase 4 (load-bearing dependency)

- `.planning/phases/04-hot-plug-hardening/04-CONTEXT.md` — D-06 weak_ptr cache in `DeviceRegistry::open()`, consumed by `TimeSyncService::setSystemTimeOn` (D-01 amendment 3).
- `.planning/phases/03-architectural-decisions/ARCH-02-mock-seam.md` — `HotplugMonitor::injectEvent` shim, used by Phase 5 integration test harness.
- `.planning/phases/03-architectural-decisions/ARCH-03-ownership-migration.md` — `unique_ptr → shared_ptr` migration on which D-01 amendment 3 depends.

### Requirements & roadmap

- `.planning/REQUIREMENTS.md` — TIMESYNC-01..06 verbatim, plus Out-of-Scope anti-features (lying success toast, NTP, read-back, per-device timezone, world clock, etc.).
- `.planning/ROADMAP.md` Phase 5 success criteria — six contractual SC1..SC6 (the design's "approved" status maps cleanly onto these).

### Pitfalls research

- `.planning/research/PITFALLS.md` Pitfall 2 — `dynamic_cast<IClockCapable*>` nullptr silent no-op. Drives the null-check pattern in `TimeSyncService::setSystemTimeOn`.
- `.planning/research/PITFALLS.md` Pitfall 4 — `QML_SINGLETON` dual-instantiation. Drives the `static_assert(!is_default_constructible_v<TimeSyncService>)` pattern.
- `.planning/research/PITFALLS.md` Pitfall 12 — system-time-set permission red herring. Drives UI labels ("to device" / "device clock").
- `.planning/research/PITFALLS.md` Pitfall 13 — persisted setting outliving capability. Drives the load-time validation in `TimeSyncService` ctor.
- `.planning/research/PITFALLS.md` Pitfall 14 — `Result::NotImplemented` log spam. Drives `std::once_flag`-gated WARN in backend stubs.

### Existing code (touched by this phase)

- `src/core/include/ajazz/core/capabilities.hpp` — adds `Capability::Clock` + `IClockCapable` interface.
- `src/core/include/ajazz/core/device.hpp` — adds `bool hasClock{false}` to `DeviceDescriptor`.
- `src/devices/streamdeck/src/{akp153,akp03,akp05,akp815}.cpp` — 4 backends gain `IClockCapable` inheritance + stub `setTime()`.
- `src/devices/streamdeck/src/register.cpp` — sets `.hasClock = true` on all 17 streamdeck rows.
- `src/devices/keyboard/src/proprietary_keyboard.cpp` — `ProprietaryKeyboard` gains `IClockCapable` + stub.
- `src/devices/keyboard/src/register.cpp` — sets `.hasClock = true` on AKB980 PRO row.
- `src/app/src/time_sync_service.{hpp,cpp}` — NEW.
- `src/app/src/application.{hpp,cpp}` — owns `TimeSyncService`, registers QML singleton, routes hotplug arrivals.
- `src/app/src/device_model.{hpp,cpp}` — adds `HasClockRole`.
- `src/app/qml/SettingsPage.qml` — `SwitchDelegate` for auto-sync.
- `src/app/qml/components/DeviceRow.qml` — "Sync time" `ToolButton` + glyph state.
- `src/app/qml/Main.qml` — wire `syncTimeRequested` signal to `TimeSyncService`.

### Documentation deliverables

- `docs/protocols/streamdeck/{akp153,akp03,akp05,akp815}.md` — append "## Time sync" section (TBD wire format note).
- `docs/protocols/keyboard/proprietary.md` — same.
- `docs/_data/devices.yaml` — add `clock` capability flag on every entry that gets `Capability::Clock`.
- `CHANGELOG.md` — under "Added": "Time Sync infrastructure (capability + UI + service); device-side wire format is still TBD per family."

### Test infrastructure

- `tests/unit/test_time_sync_service.cpp` — NEW; mocks `IDevice` + `IClockCapable`.
- `tests/integration/test_time_sync_e2e.cpp` — NEW; uses Phase 4's `HotplugMonitor::injectEvent` + `MockHidEnumerator`.

\</canonical_refs>

\<code_context>

## Existing Code Insights

### Reusable Assets

- **`QML_SINGLETON` static_assert pattern** — `BrandingService`, `DeviceModel`, etc. already establish the pattern. `TimeSyncService` inherits the convention: explicit constructor with non-defaulted parameter + `static_assert(!is_default_constructible_v<TimeSyncService>)` immediately after the class declaration.
- **`HotplugMonitor::deviceArrived` signal** — exists in production; Phase 5 only adds a new subscriber (`TimeSyncService::onDeviceArrived`). The 300 ms QTimer debounce is per-deviceId and per-arrival.
- **`Result` enum** — already includes `Ok`, `IoError`, etc. (`src/core/include/ajazz/core/capabilities.hpp` or `result.hpp`). Plan task 1 confirms `NotImplemented` exists and adds it if missing.
- **`Toast` component** at `src/app/qml/components/Toast.qml` — built for profile-save events; Phase 5 reuses it for sync feedback. Phase 4 D-01 leaves it alone for hot-plug events; Phase 5 occupies the user-initiated lane.
- **`SwitchDelegate` precedent** — Settings page already has switches (theme, autostart, etc.); Phase 5 appends one more.
- **`std::once_flag`** — already used elsewhere for "warn once" logging patterns (grep precedent in plan task 2/3).

### Established Patterns

- **`QSettings` keyed by feature path** — e.g. `QSettings("Time/AutoSync", false)`. Conventional in this codebase.
- **`hasX` boolean fields on `DeviceDescriptor`** — `hasRgb`, `hasTouchStrip` already exist. `hasClock` follows the same pattern; cheap UI gating without runtime cast.
- **`dynamic_cast<I*Capable*>` paired with non-null check** — Pitfall 2's mandatory pattern. Every cast site must have an `if (!clk) { emit syncFailed(...); return; }` within 3 lines.
- **`qmlRegisterSingletonInstance` + factory `create(QQmlEngine*, QJSEngine*)`** — the canonical singleton registration that avoids QML_SINGLETON dual-instance bug (Pitfall 4).

### Integration Points

- **`Application::onHotplug` → `TimeSyncService::onDeviceArrived`** — wire up in `Application::Application()` constructor body. The hotplug callback is per Phase 4 already debounced (300 ms key-coalescing); Phase 5's auto-sync 300 ms debounce stacks on top → total 600 ms from plug-in to auto-sync fire (per Phase 4 D-05 rationale).
- **`DeviceRegistry::open(deviceId)` → `shared_ptr<IDevice>` from D-06 cache** — `TimeSyncService::setSystemTimeOn` calls `m_registry.open(deviceId)`, captures the `shared_ptr`, `dynamic_cast<IClockCapable*>(ptr.get())`, calls `setTime`. The `shared_ptr` lifetime extends across the call, eliminating the UAF window.
- **`DeviceModel::roleNames` / `data()`** — adds `HasClockRole` reading from `descriptor.hasClock`. Same pattern as existing `HasRgbRole` / `HasTouchStripRole`.

\</code_context>

<specifics>
## Specific Ideas / Anchor Artefacts

- **Per-decision artefact files under `.planning/phases/05-time-sync-scaffolding/`:**
  - `05-PLAN.md` (or multi-plan directory like `05-task-1.md`..`05-task-8.md`) — gsd-planner output. Adopts the existing 8 tasks with the 3 amendments from D-01.
  - `05-SUMMARY.md` — gsd-executor output.
- **Atomic commit boundaries** — each of the 8 tasks is its own commit per the design doc's build/test sequence. Task 1 (capability + interface + descriptor flag) lands first; Task 2 (Stream Dock stubs including AKP815) is one commit covering all 4 backends; Task 7 (Application wiring) reads from Phase 4 D-06 cache.
- **Plan execution order** — Tasks 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 per the original sequence. Task 4 (TimeSyncService unit tests) can run in parallel with Tasks 5/6 (DeviceModel role + QML UI) because they touch different files. Task 8 (docs + integration test) is the last commit.
- **Verification anchors:** the design doc's "Self-review" section (line 1699) and "Final verification" (line 1675) become the basis for VERIFICATION.md after execute.

</specifics>

<deferred>
## Deferred Ideas

- **Real wire-format implementations** — out of scope by design. Each per-protocol-doc "## Time sync" section says "TBD wire format pending vendor-app capture or firmware reverse-engineering". The day a backend implements `setTime()` for real, only that backend's stub body changes — UI / service / capability all stay.
- **Device → host time read-back** — explicit non-goal; out of scope.
- **Interval-based time re-sync timer** — drift-detection ≠ time-keeping. Defer until a real backend exists to validate need.
- **Per-device detail panel for Sync now** — design doc decided "DeviceRow only" because no detail panel exists yet. The day a panel ships, the Sync button can mirror there too.
- **Render-time-on-keyface clock widget** — separate "Clock Widget" feature that reuses the existing `IDisplayCapable::setKeyImage` path. Tracked under TODO.md.
- **AKB980 PRO real wire format** — Delphi installer in `~/MEGAsync/Ajazz/` requires `wine`/`innoextract` not in dev env. Stub stays until a static-analysis pass produces the byte-level spec.
- **Pre-2026-05-14 placeholder VID:PID retirement** — when captures confirm the AJAZZ-branded AKP05's real VID:PID (or a Mirabox N3 capture confirms the right hardware revision), delete the `0x0300:0x5001` and `0x0300:0x3001` placeholder rows from `register.cpp` + `devices.yaml`. Tracked in TODO.md.

</deferred>

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Context gathered: 2026-05-14*
