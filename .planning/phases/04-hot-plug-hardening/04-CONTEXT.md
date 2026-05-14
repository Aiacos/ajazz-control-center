---
phase: 4
phase_slug: hot-plug-hardening
gathered: 2026-05-14
status: Ready for planning
mode: autonomous-interactive
---

# Phase 4: Hot-plug Hardening — Context

**Gathered:** 2026-05-14 via `/gsd-autonomous --only 4 --interactive`
**Source for recommendations:** `.planning/research/PITFALLS.md` (Pitfalls 1, 3, 11, 15) + `.planning/phases/03-architectural-decisions/ARCH-02-mock-seam.md` + `ARCH-03-ownership-migration.md` + `.planning/REQUIREMENTS.md` HOTPLUG-01..07.

<domain>
## Phase Boundary

A user can disconnect, reconnect, and shuffle devices while interacting with the app without crashes, focus loss, sidebar reordering, or toast floods.

Concretely, the phase delivers:

1. **Ownership migration:** `DevicePtr` flips `unique_ptr<IDevice>` → `shared_ptr<IDevice>` (ARCH-03), atomic single commit across `core/`, `devices/streamdeck/`, `devices/keyboard/`, `devices/mouse/`, and any `app/` consumer.
1. **Registry cache:** `DeviceRegistry::open()` becomes a `weak_ptr`-cached flyweight so multi-consumer scenarios (Phase 5 `TimeSyncService` + KeyDesigner) share one backend / one HID handle.
1. **Coalescing debounce:** `Application::onHotplug` debounces by `(vid, pid, serial)` with a 300 ms trailing-edge window before any consumer sees the event.
1. **Test seam:** `HotplugMonitor::injectEvent(HotplugEvent const&)` shim under `#ifdef AJAZZ_TESTING` (ARCH-02), plus constructor-injectable `HidEnumerator` on `DeviceRegistry` for the `MockHidEnumerator` half.
1. **Sidebar UX:** `DeviceModel` switches from `beginResetModel`/`endResetModel` to fine-grained `dataChanged(idx, idx, {ConnectedRole})`; rows stay in stable lex `(deviceClass, codename)` order; selection survives disconnect/reconnect; offline badge is the only visible state change.
1. **Multi-device test harness** exercises disconnect-during-use, reconnect, and device-shuffle scenarios on Linux; smoke build on Windows for the `WM_DEVICECHANGE` path.
1. **Documentation:** the 2026-05-12/13 hot-plug fix story lives as a phase artefact (HOTPLUG-07).

Maps to requirements: HOTPLUG-01 .. HOTPLUG-07 (full text in `.planning/REQUIREMENTS.md`).

</domain>

<decisions>
## Implementation Decisions (locked)

### D-01 — Toast policy: silent badge-only

Hot-plug arrive/depart events produce **no toasts**. The sidebar `ConnectedRole` flip (and any future `MaturityRole` badge from Phase 8) is the only visible feedback. Matches the OSS convergent UX ("silent reconnect") and reserves the toast surface for Phase 5's user-initiated "Sync now" feedback.

**Rationale:** REQUIREMENTS Out-of-Scope explicitly lists modal "device disconnected" dialogs, removal sounds, and full QML reload as anti-features. Pitfall 3 caps the toast queue and reserves toasts for user-initiated actions. Two composite-USB events per Stream Dock arrival × N devices × repeated cable bounces = a toast flood without this rule.

### D-02 — Hot-plug error surface: log only

Hard failures during enumeration (factory returns `nullptr`, `hid_open` returns permission-denied, `hid_set_nonblocking` fails) are WARN-logged and produce **no UI surface**. The user sees the device's absence; the logs explain why. No third "error" badge state in v1.1.

**Rationale:** Errors during enumeration are not user-actionable from inside the app (permission errors require terminal-side `udev` rule install or group membership; missing factories require a code change). Adding a UI surface would just add noise. Defer "actionable error" surfacing to whatever phase introduces device pairing/setup.

**Includes the "catalog-referenced, no backend" case** (recheck 2026-05-14): the AKP815 is referenced 17 times across `PluginCatalogModel`, `StreamdockCatalogFetcher`, and `resources/streamdock-fallback.json` but has **no backend in `register.cpp`**. When such a device is plugged in, hidapi sees `0x5548:0x6672` but `DeviceRegistry::open()` returns `nullptr` (no factory match). D-02 covers this: WARN-log only, no toast, the device simply doesn't appear in the sidebar (because `enumerate()` never returned a row for it). Fixing the AKP815 backend gap is **Phase 8** scope, not Phase 4 — Phase 4's contract is that hot-plug stays stable while Phase 8 fills in the backends.

### D-03 — DeviceModel propagation: fine-grained dataChanged

`DeviceModel::refresh()` no longer calls `beginResetModel`/`endResetModel`. Instead it diffs old vs new `m_connected` set and emits `dataChanged(idx, idx, {ConnectedRole})` for each row whose state flipped. Selection and scroll position survive automatically because indices don't move.

**Rationale:** The registered-backend list is **static** — it grows only at `registerDevice()` time, never during hot-plug. So fine-grained updates are essentially free. Closes Pitfall 15 properly without the QML-side codename-restore hack. ~30 LoC delta in `DeviceModel::refresh()`. Any future role added (e.g. `MaturityRole` in Phase 8) folds into the same `dataChanged` call.

### D-04 — Sidebar row identity: one row per backend (codename)

DeviceModel rows continue to represent **registered backends** (one row per codename). Two physical AKP153s collapse to one sidebar row whose `ConnectedRole` is true if **any** unit is plugged in. Per-unit identity (codename + serial in the sidebar) is deferred to v1.2.

**Rationale:** Multi-physical-unit UX requires ProfileController, KeyDesigner, and every per-codename surface to become per-unit too — not a Phase 4-sized change. The event-coalescing layer in `Application::onHotplug` *does* track `(vid, pid, serial)` (per HOTPLUG-05); the model layer just doesn't need to surface that yet.

**Rebadge clarification** (recheck 2026-05-14): per Finding 16 in `docs/research/vendor-protocol-notes.md`, the AKP03 protocol is sold under **at least 8 different VID/PID pairs** (AJAZZ AKP03/E/R/R-rev2, Mirabox N3/N3-rev3/N3EN, Soomfon SE, Mars Gaming MSD-TWO, TreasLin N3, Redragon SS-551). Each rebadge gets its own `registerDevice(descriptor, factory)` call (different VID/PID, **same factory**) but **all of them share one `codename` = "akp03"** (or whichever marketing name we settle on). D-04 means: 8 rebadges × 1 connected unit each = **one** sidebar row labelled by codename, with `ConnectedRole=true`. The fine-grained `dataChanged` (D-03) walks rows, not VID/PID pairs — selection survives because the codename-keyed row index is stable across hot-plug. Planner must not introduce per-VID/PID rows; row identity is `codename`, period.

### D-05 — Debounce window: 300 ms

`Application::onHotplug` coalesces events keyed by `(vid, pid, serial)` with a **300 ms trailing-edge debounce**. One `QTimer` per key, restarted on every event, fires once.

**Rationale:** Comfortable middle of the ROADMAP-allowed 250-500 ms range. Catches composite-USB pairs (~10 ms apart on Linux), absorbs flaky-cable bounces (typically \<200 ms), keeps first-paint latency under perceptible threshold (~300 ms is the typical "responsive feel" budget). Symmetrical with Phase 5's own 300 ms time-sync debounce — easier to reason about the stacked-timer budget (300 + 300 = 600 ms from plug-in to auto-sync fire).

### D-06 — DeviceRegistry::open() caching: weak_ptr cache

`DeviceRegistry` gains a `std::map<DeviceId, std::weak_ptr<IDevice>> m_open_devices` member. `open(id)` first locks the weak_ptr; if expired or absent, constructs a fresh `shared_ptr<IDevice>` via the factory and stores a weak_ptr to it. All consumers calling `open(id)` for the same id share one backend instance and one HID handle.

**Lifecycle contract:**

- Cache eviction is **passive** — no proactive removal on hot-plug Removed events. The weak_ptr expires when the last consumer's shared_ptr drops.
- A cached `shared_ptr<IDevice>` whose underlying USB device has been removed is a **zombie**: methods on it must return a sentinel (`Result::DeviceGone` or equivalent) rather than UB. The `IDevice` API contract is updated to make this explicit in doc comments. Per-backend implementation: each `IDevice` method checks an `m_alive` flag (or equivalent hidapi handle validity) before issuing HID I/O.
- `DeviceRegistry::open()` gains a mutex (separate from `m_mutex` guarding `m_entries`) for cache access — `m_open_devices` mutex distinct so `enumerate()` and `open()` don't contend.

**Rationale:** Phase 5's `TimeSyncService` is the first multi-consumer scenario — without caching, calling `open(akp153)` from both KeyDesigner and TimeSyncService creates two backend instances; on Windows hidapi's exclusive-open returns nullptr for the second one. The cache is the textbook flyweight pattern with weak ownership and natural cleanup. Same atomic commit as the unique→shared migration; ~25 additional LoC. Closes the Windows-exclusive-open bug **before Phase 5 hits it** rather than as a derived constraint discovered live.

### Claude's Discretion

- **File layout** under `.planning/phases/04-hot-plug-hardening/`: per-decision artefacts (`04-HOTPLUG-RETRO.md` for the 2026-05-12/13 narrative per HOTPLUG-07, `04-PLAN.md` from gsd-planner, `04-SUMMARY.md` post-execute, etc.) follow Phase 3's convention.
- **HOTPLUG-07 retro placement:** narrative lives at `.planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md` (phase-local). If the architectural insight generalises, gsd-extract-learnings can promote it to `docs/architecture/HOTPLUG.md` later.
- **Multi-device test scenario matrix:** the planner derives concrete test cases from HOTPLUG-06 + Pitfall 1/3 — at minimum: (a) disconnect-during-mid-`setTime` (UAF stress); (b) hub-yank with N devices simultaneously; (c) round-robin same-device replug at 1Hz; (d) two same-VID-PID-different-serial units in parallel.
- **Windows CI smoke scope:** smoke-build on Windows that compiles the `_win32.cpp` hot-plug path and runs a non-USB-touching unit test exercising `HotplugMonitor::injectEvent` against the `WM_DEVICECHANGE` translation layer. **Not** a real-device CI job (out of scope until Phase 6 lands a Windows runner with USB capacity).
- **`hid_open` invariant enforcement (Pitfall 11):** add a CI grep check `grep -r "hid_open(" src/ | grep -v hid_transport.cpp | grep -v "hid_open_path("` must return zero hits. Ensures the non-blocking-mode pairing isn't bypassed.

</decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 3 architectural decisions (locked, MUST-read)

- `.planning/phases/03-architectural-decisions/ARCH-02-mock-seam.md` — `HotplugMonitor::injectEvent` shim contract.
- `.planning/phases/03-architectural-decisions/ARCH-03-ownership-migration.md` — `unique_ptr` → `shared_ptr` migration scope and atomicity rule.

### Requirements & roadmap

- `.planning/REQUIREMENTS.md` — HOTPLUG-01..07 verbatim, plus Out-of-Scope anti-features (modal dialogs, auto-retry hammer, removal sound, full QML reload, sidebar recency-reorder).
- `.planning/ROADMAP.md` Phase 4 success criteria — the six contractual SC1..SC6.

### Research (informs planning)

- `.planning/research/PITFALLS.md` Pitfall 1 — UAF risk that drives ARCH-03 + the Phase 4 ownership migration.
- `.planning/research/PITFALLS.md` Pitfall 3 — toast-flood / stuck-toast UX, drives D-01 silent-badge policy.
- `.planning/research/PITFALLS.md` Pitfall 11 — `hid_open` / `hid_set_nonblocking` pairing invariant.
- `.planning/research/PITFALLS.md` Pitfall 15 — `DeviceModel::refresh()` thrash, drives D-03 fine-grained dataChanged.
- `.planning/research/PITFALLS.md` Cross-Cutting — Linux-CI-blind Windows breakage; mandates the Windows smoke build for the `WM_DEVICECHANGE` path.
- `.planning/research/SUMMARY.md` §1 — Executive Summary, ordering constraint flag (Phase 4 → Phase 5 load-bearing).

### Existing code (touched by this phase)

- `src/core/include/ajazz/core/device.hpp:173` — `using DevicePtr = std::unique_ptr<IDevice>;` — the alias to flip.
- `src/core/include/ajazz/core/device.hpp:181` — `DeviceFactory` signature (returns `DevicePtr`).
- `src/core/include/ajazz/core/device_registry.hpp:111` — `open()` return type signature.
- `src/core/src/device_registry.cpp:87` — `open()` implementation, gets the weak_ptr cache.
- `src/core/include/ajazz/core/hotplug_monitor.hpp` + `src/core/src/hotplug_monitor.cpp` — `injectEvent` shim destination.
- `src/app/src/application.cpp:180-195` — hot-plug callback marshalling, gets the per-key debounce.
- `src/app/src/device_model.cpp:105-110` — `refresh()` switches to fine-grained `dataChanged`.
- `src/app/src/device_model.hpp:48-148` — DeviceModel role enum, may add badge role.
- `src/devices/streamdeck/src/akp153.cpp:408`, `akp03.cpp:426`, `akp05.cpp:512`, `keyboard/src/proprietary_keyboard.cpp:420`, `keyboard/src/via_keyboard.cpp:217`, `mouse/src/aj_series.cpp:268` — six factory functions migrated to return `shared_ptr<IDevice>`.

### Test infrastructure (precedent)

- `tests/unit/test_action_engine.cpp:119` — `FakeAsyncExecutor` precedent for the `injectEvent` test pattern (cited by ARCH-02).

### Stream Dock catalog research (added 2026-05-14 via quick task 260514-h0w)

- `docs/research/vendor-protocol-notes.md` Finding 16 (`§16.A..§16.D`) — Stream Dock device-catalog reconciliation. **Key fact for Phase 4 D-04:** at least 8 different VID/PID pairs map to the same `akp03`-protocol backend (rebadging across 7 third-party brands). Phase 4 row identity is `codename`, not `(vid, pid)` — see D-04 rebadge clarification.
- `docs/protocols/streamdeck/_research-sources.md` — citation index for the four Stream Dock protocol docs. Phase 4 doesn't read protocol bytes, but planner should know this index exists so it can trace back claims if a registry-shaped question crosses into protocol territory.
- `docs/protocols/streamdeck/akp815.md` — new device with **no backend in `register.cpp`**. Confirms the D-02 "log-only, no UI surface" policy is the right fit for catalog-referenced-but-unregistered devices. Backend gap itself is **Phase 8** scope.

\</canonical_refs>

\<code_context>

## Existing Code Insights

### Reusable Assets

- **`HotplugMonitor`** — the cross-platform hot-plug detector (Linux udev / Win32 `WM_DEVICECHANGE` / macOS IOKit) is already in place at `src/core/src/hotplug_monitor.cpp`. Phase 4 only **adds** the test shim; production paths stay untouched.
- **`DeviceRegistry::enumerateConnectedHidKeys()`** — already returns the live `(vid, pid)` set from `hid_enumerate(0, 0)`. Phase 4's coalescing layer **does not need to re-enumerate** — it just observes the events.
- **`QML_SINGLETON` static_assert pattern** — `DeviceModel` (`device_model.hpp:146`) already has `static_assert(!std::is_default_constructible_v<DeviceModel>)`. Any new singleton (none planned for Phase 4) inherits the convention.
- **`Q_OBJECT` GUI-thread marshalling** — `Application::onHotplug` already does `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to marshal to the GUI thread. The new debounce timer lives on the GUI thread (Qt's default `QTimer` thread) so this stays correct.

### Established Patterns

- **Constructor injection over Meyers singletons** — `DeviceRegistry` was already converted (audit finding A1); the deprecated `instance()` shim still compiles. Phase 4 must not regress to `instance()`-based code.
- **Mutex hygiene COD-004/005** — `DeviceRegistry::open()` already releases its mutex before invoking the factory. The new weak_ptr cache must follow the same pattern: copy weak_ptr out under lock, lock it / construct outside the lock.
- **Single-handle ownership in HidTransport** — `hid_open` lives only in `src/core/src/hid_transport.cpp:70`. Pitfall 11 invariant.
- **Byte-wise multi-byte writes** — protocol packets use `pkt[N] = (val >> shift) & 0xFF;` (e.g. `akp153.cpp`), not `reinterpret_cast`. Phase 4 doesn't touch protocol code, but any new wire-format helper inherits this rule.

### Integration Points

- **`Application::onHotplug` → debouncer → `m_deviceModel->refresh()`** — the debouncer is a new internal helper class (`HotplugDebouncer`?) owned by `Application`, holding a `QHash<HotplugKey, QTimer*>` map. Per-key timer fires the existing `refresh()` invocation marshalled to the GUI thread.
- **`MockHidEnumerator` constructor injection on `DeviceRegistry`** — `DeviceRegistry` ctor gains an optional `HidEnumerator = std::function<std::set<HidKey>()>` parameter (defaults to the real `hid_enumerate` walker). Production callers (Application bootstrap) keep the default; tests pass a mock that returns synthetic device sets.
- **`HotplugMonitor::injectEvent` → existing internal signal** — synthesised events go through the same emitter as real events, so all subscribers see them indistinguishably.

\</code_context>

<specifics>
## Specific Ideas / Anchor Artefacts

- **`HotplugDebouncer` helper class name** is suggestive — final naming is the planner's call. It must own the `QHash<HotplugKey, QTimer*>` and clean up timers either in its destructor or after each fire.
- **`HotplugKey` struct** = `{vid, pid, serial}` (`serial` may be empty). On Linux when `serial` is empty, the existing `HotplugEvent::serial` is empty too — so two cable replugs of the same serial-less device coalesce correctly across the same hub port. (No bus/port fallback needed; the udev path may differ but the event payload is what we key on.)
- **Per-decision artefact files under `.planning/phases/04-hot-plug-hardening/`:**
  - `04-PLAN.md` — gsd-planner output.
  - `04-HOTPLUG-RETRO.md` — narrative of the 2026-05-12/13 fix (HOTPLUG-07). What was broken (3 devices invisible despite registration), what changed, why it works now.
  - `04-SUMMARY.md` — gsd-executor output.
- **Atomic commit** for the `unique_ptr → shared_ptr` migration includes: `device.hpp` alias + `DeviceFactory` signature, `device_registry.hpp/cpp` (`open()` return type + cache plumbing), 6 factory implementations (`akp153.cpp`, `akp03.cpp`, `akp05.cpp`, `proprietary_keyboard.cpp`, `via_keyboard.cpp`, `aj_series.cpp`), and any `IDevice` ctor that captures `this` in a callback (audit during planning).

</specifics>

<deferred>
## Deferred Ideas

- **Per-unit sidebar rows (codename + serial)** — D-04 keeps row-per-backend in v1.1. Defer to v1.2 once ProfileController, KeyDesigner, and per-codename surfaces become unit-aware. Track as a v1.2 backlog item.
- **Real Windows CI runner with USB capacity** — Phase 4 ships a Windows smoke-build only. A real Windows runner with USB-pass-through is a Phase 6 (CR-01) prerequisite at the earliest, possibly v1.2.
- **Proactive cache eviction on Removed events** — D-06 chose passive eviction (weak_ptr expiry). If a future phase finds the zombie-`shared_ptr` window problematic (e.g. consumer holds it for minutes), revisit and add active invalidation via `enable_shared_from_this` + `weak_ptr` upgrade-fail signalling.
- **"Last seen N minutes ago" tooltip on offline rows** — would require a new role (`OfflineSinceRole`) and a per-row timestamp store. Could be a quality-of-life follow-up; not in scope for v1.1.
- **`std::map<DeviceId, std::shared_ptr<IDevice>>` strong cache** (alternative to weak_ptr) — would keep instances alive even when no consumer holds a ref, trading memory for "no transient hid_open on second consumer". Rejected for v1.1 per principle of least surprise; reconsider if Phase 5 measurements show measurable open() latency.
- **Stream Dock catalog corrections (Phase 8)** — added during recheck 2026-05-14: per `vendor-protocol-notes.md` Finding 16, the codebase has a known catalog gap (AKP815 backend missing despite 17 references; AKP153/AKP153E PIDs are wrong; AKP03 layout is wrong (6 keys + 1 encoder vs real 6 keys + 3 buttons + 3 encoders); AKP05/N4 is wrong (15 keys vs real 10 keys); `streamdock_catalog_fetcher.cpp:117` maps N4→akp815 instead of N4→akp05; rebadges across Mirabox / Soomfon / Mars Gaming / TreasLin / Redragon are unregistered). **All Phase 8 scope, not Phase 4.** Phase 4's contract is hot-plug stays stable while Phase 8 adds/corrects the backend registrations behind the curtain.

</deferred>

______________________________________________________________________

*Phase: 04-hot-plug-hardening*
*Context gathered: 2026-05-14*
