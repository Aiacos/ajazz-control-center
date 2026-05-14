# Roadmap: AJAZZ Control Center

## Milestones

- ✅ **v1.0 milestone** — Phases 1-2, retro-fit catalogue (shipped 2026-05-13). See [milestones/v1.0-ROADMAP.md](milestones/v1.0-ROADMAP.md).
- 🚧 **v1.1** — Device lifecycle hardening + scaffolding-to-functional (Phases 3-8). Active.

## Phases

<details>
<summary>✅ v1.0 milestone (Phases 1-2) — SHIPPED 2026-05-13</summary>

- [x] Phase 1: SEC-003 Plugin Host — completed 2026-05-03
- [x] Phase 2: QML Singleton Sweep — completed 2026-05-04

Audit: `tech_debt` — 7/7 success criteria PASSED; CR-01 (Win32 env pollution) and WR-01 (trust-roots parser) deferred for the next milestone touching those areas.

</details>

### 🚧 v1.1 milestone (Phases 3-8)

- [x] **Phase 3: Architectural Decisions** — Decision-doc phase recording WR-01 parser choice, HotplugMonitor mock seam, and DeviceRegistry ownership migration commit. (completed 2026-05-14)
- [ ] **Phase 4: Hot-plug Hardening** — Migrate registry to `shared_ptr<IDevice>`, add coalescing debounce, multi-device test harness, offline-badge UX.
- [ ] **Phase 5: Time-Sync Scaffolding** — Five-layer slice from `hasClock` capability flag through `IClockCapable` mix-in, `TimeSyncService`, QML UI, and honest `NotImplemented` reporting.
- [ ] **Phase 6: CR-01 Win32 OOP Env Pollution Fix** — Per-spawn UTF-16 env block + `CREATE_UNICODE_ENVIRONMENT`; atomic removal of `_putenv_s` calls; Windows CI exercise.
- [ ] **Phase 7: WR-01 Trust-Roots Parser Hardening** — Lockstep parser replacement across `manifest_signer.cpp` + `manifest_signer_win32.cpp` per Phase 3 decision, with size caps and fuzz corpus.
- [ ] **Phase 8: Scaffolded-Device Wiring** — `maturity` tier infrastructure (YAML + `MaturityRole` + README); opportunistic Tier 0 → Tier 2 promotion of 1-2 stream-dock siblings.

## Phase Details

### Phase 3: Architectural Decisions

**Goal**: Three written architectural decisions are recorded that unblock downstream phases — WR-01 parser choice, HotplugMonitor mock seam, and registry ownership migration scope.
**Depends on**: Nothing (first phase of v1.1; v1.0 audit closed independently).
**Requirements**: ARCH-01, ARCH-02, ARCH-03
**Success Criteria** (what must be TRUE):

1. A phase artefact records the WR-01 parser choice (`nlohmann::json` PRIVATE-linked to `ajazz_plugins` vs in-tree 5-state scanner) with explicit threat-model framing of where `trust_roots.json` parsing sits relative to the plugin sandbox boundary.
1. A phase artefact records the `HotplugMonitor` mock-seam approach (test-only `injectEvent` shim vs subclassable interface vs other) with rationale that justifies the call against the `FakeAsyncExecutor` precedent.
1. A phase artefact confirms that `DeviceRegistry` slot ownership migrates from `unique_ptr<IDevice>` to `shared_ptr<IDevice>` in Phase 4, with explicit acknowledgement that no v1.1 code outside Phase 4 holds an `IDevice*` across an event-loop turn until that migration ships.
1. No code lands in Phase 3 — only decision artefacts under `.planning/phases/03-architectural-decisions/`.
   **Plans**: TBD

### Phase 4: Hot-plug Hardening

**Goal**: A user can disconnect, reconnect, and shuffle devices while interacting with the app without crashes, focus loss, sidebar reordering, or toast floods.
**Depends on**: Phase 3 (ARCH-02 mock seam, ARCH-03 ownership migration confirmed).
**Requirements**: HOTPLUG-01, HOTPLUG-02, HOTPLUG-03, HOTPLUG-04, HOTPLUG-05, HOTPLUG-06, HOTPLUG-07
**Success Criteria** (what must be TRUE):

1. User can yank a USB cable mid-interaction with no crash — `DeviceRegistry` holds `shared_ptr<IDevice>` slots so any in-flight `IDevice*` survives concurrent slot reset (Pitfall 1 closed).
1. User sees the disconnected device's sidebar row stay in place with an offline badge, retains their selection focus across the disconnect/reconnect cycle, and never sees the row jump position.
1. Sidebar rows are sorted lexicographically by `(deviceClass, codename)` and the order is stable across arrival, departure, and re-arrival events (never reordered by recency).
1. Hot-plug events are coalesced by `(vid, pid, serial)` with a 250-500 ms trailing-edge debounce in `Application::onHotplug` before any consumer sees them, so a USB-hub shuffle produces at most one user-visible event per device (Pitfall 3 closed).
1. The multi-device integration test exercises disconnect-during-use, reconnect, and device-shuffle scenarios using `MockHidEnumerator` + `HotplugMonitor::injectEvent`, and the same harness is exercised by a Windows CI smoke run for the `WM_DEVICECHANGE` path.
1. A phase artefact documents the 2026-05-12/13 hot-plug debugging fix (what was broken, what changed, why 3 devices now work) so the institutional knowledge isn't lost.
   **Plans**: 7 plans in 3 waves
   - [x] 04-01-PLAN.md — Atomic ownership migration (`unique_ptr` → `shared_ptr`) + `weak_ptr` cache (ARCH-03 / D-06 / HOTPLUG-01) — Wave 1
   - [ ] 04-02-PLAN.md — Test seam: `HotplugMonitor::injectEvent` shim + constructor-injectable `HidEnumerator` (ARCH-02 / HOTPLUG-06) — Wave 2 *(depends on 04-01)*
   - [ ] 04-03-PLAN.md — `HotplugDebouncer` (300ms trailing-edge, per-key) + `Application::onHotplug` wiring (D-05 / HOTPLUG-05) — Wave 1
   - [ ] 04-04-PLAN.md — DeviceModel diff-driven `dataChanged` + lex sort + codename collapse + QML offline badge (D-03 / D-04 / HOTPLUG-02..04) — Wave 1
   - [ ] 04-05-PLAN.md — Multi-device integration test harness (HOTPLUG-06) — Wave 3 *(depends on 04-01, 04-02, 04-03)*
   - [ ] 04-06-PLAN.md — Windows `WM_DEVICECHANGE` smoke test + CI gate (HOTPLUG-06 cross-cutting) — Wave 3 *(depends on 04-02)*
   - [ ] 04-07-PLAN.md — `04-HOTPLUG-RETRO.md` narrative + `hid_open` invariant CI grep (HOTPLUG-07 / Pitfall 11) — Wave 1
     **UI hint**: yes

### Phase 5: Time-Sync Scaffolding

**Goal**: A user can issue a per-device "Sync now" or enable global auto-sync, and the system honestly surfaces `NotImplemented` for the four functional backends without lying via success toasts.
**Depends on**: Phase 4 (`shared_ptr<IDevice>` registry migration required before any debounced `dynamic_cast<IClockCapable*>` use across event-loop turns).
**Requirements**: TIMESYNC-01, TIMESYNC-02, TIMESYNC-03, TIMESYNC-04, TIMESYNC-05, TIMESYNC-06
**Success Criteria** (what must be TRUE):

1. User sees a "Sync now" button on devices whose static `DeviceDescriptor.hasClock` flag is true, and the button is hidden on devices without it — gating is done via the existing `hasX`-flag + `dynamic_cast<IClockCapable*>` pattern, NOT via inventing a new `IDevice::capabilities()` method (Architecture Anti-pattern 1 avoided).
1. All four functional backends (AKP153, AKP03, AKP05, AKB980 PRO) implement `IClockCapable::setTime()` as stubs returning `Result::NotImplemented` — no crashes, no false success, and backend WARN logs are `std::once_flag`-gated to avoid spam (Pitfall 14).
1. User can toggle a Settings page "auto-sync time on device connect" switch (`QSettings`-persisted with load-time validation); when on, auto-sync fires 300 ms after a stable arrival, re-validates capability AND connectedness at firing time, and treats a `nullptr` from `dynamic_cast<IClockCapable*>` as a quiet no-op (Pitfall 2 closed).
1. A `NotImplemented` result surfaces as an exclamation glyph + tooltip on the device row — never as a success toast — preserving honest UX.
1. `TimeSyncService` is registered as a QML singleton via `qmlRegisterSingletonInstance` with `static_assert(!std::is_default_constructible_v<TimeSyncService>)` co-located with the `QML_SINGLETON` declaration, converting the invariant into a build break (Pitfall 4 closed via the v1.0 canonical pattern).
1. The implementation adopts the 8-task plan at `docs/superpowers/plans/2026-05-13-time-sync.md` as the Phase 5 plan rather than re-decomposing the work.
   **Plans**: Adopt `docs/superpowers/plans/2026-05-13-time-sync.md` (8 atomic tasks).
   **UI hint**: yes

### Phase 6: CR-01 Win32 OOP Env Pollution Fix

**Goal**: A user spawning the OOP plugin host on Windows no longer pollutes the parent process's environment, and Windows CI proves it.
**Depends on**: Nothing (parallel-independent of Phases 4, 5, 7, 8 — single-file ~20-line change once Windows CI capacity is confirmed).
**Requirements**: WIN32-01, WIN32-02, WIN32-03, WIN32-04
**Success Criteria** (what must be TRUE):

1. The Win32 OOP plugin host spawns Python child processes with a per-spawn UTF-16 environment block built from `GetEnvironmentStringsW` + three Python overrides (`PYTHONPATH`, `PYTHONDONTWRITEBYTECODE`, `PYTHONUNBUFFERED`) + `\0\0` block terminator + case-insensitive sort + preserved `=`-prefixed drive-letter entries, passed to `CreateProcessW` with the `CREATE_UNICODE_ENVIRONMENT` flag (Pitfall 5 closed).
1. All three `_putenv_s` calls at `out_of_process_plugin_host_win32.cpp:463-467` are removed in the same commit as the env-block introduction — no belt-and-braces leftover (Pitfall 6 closed).
1. Windows CI exercises the OOP host child spawn (not just a compile) and asserts via `_wgetenv(L"PYTHONPATH")` that the parent process's environment is unchanged after a child completes.
1. A phase artefact documents the resolved duplicate-key precedence question (first-wins vs last-wins on inherited `PYTHONPATH`) using results from a CI smoke test, since MS docs and nullprogram disagree.
   **Plans**: TBD

### Phase 7: WR-01 Trust-Roots Parser Hardening

**Goal**: The `loadTrustRoots` parser is replaced (per ARCH-01) in lockstep across both translation units, with bounded DoS surface and honest TOCTOU framing.
**Depends on**: Phase 3 (ARCH-01 parser choice must be decided first). Independent of Phases 4, 5, 6, 8.
**Requirements**: TRUST-01, TRUST-02, TRUST-03, TRUST-04
**Success Criteria** (what must be TRUE):

1. `loadTrustRoots` uses the parser chosen in ARCH-01, and the mini-grep parser is fully removed from BOTH `manifest_signer.cpp` and `manifest_signer_win32.cpp` in the same commit — drift between the two TUs is what re-introduces WR-01.
1. `loadTrustRoots` enforces a 1 MB byte-cap (fails closed on oversize input) and a 1024-entry cap on parsed roots to bound parser DoS surface (Pitfall 7 closed).
1. The test suite covers BOM, escape sequences, nested structures, and embedded NUL bytes in trust-roots input; a fuzz corpus runs in under 1 second on 100 KB inputs.
1. The public-API header for `loadTrustRoots` documents the 0600 file-permissions assumption between the read step and the verify step (Pitfall 8 TOCTOU framing recorded in writing).
1. If ARCH-01 selected `nlohmann::json`, then `vcpkg.json` + `CMakeLists.txt` + CI pin manifests + COD-031 charter update all land in the same commit as the parser swap, and the dep is PRIVATE-linked to `ajazz_plugins` only (never appears in `ajazz_core` or any header).
   **Plans**: TBD

### Phase 8: Scaffolded-Device Wiring

**Goal**: Users can see honest maturity tiers per device in the sidebar and README, and 1-2 stream-dock-family siblings move from Tier 0 (Scaffolded) to Tier 2 (Partial) with documented protocol artefacts.
**Depends on**: Phase 5 (selected scaffolded backends inherit the `IClockCapable` stub interface). Independent of Phases 6, 7.
**Requirements**: DEVICES-01, DEVICES-02, DEVICES-03, DEVICES-04
**Success Criteria** (what must be TRUE):

1. `docs/_data/devices.yaml` has a `maturity` field per device with the tier vocabulary Scaffolded / Probed / Partial / Functional / Verified, and the field is populated for all 13 catalogued devices.
1. `DeviceModel` exposes a `MaturityRole` and the QML sidebar surfaces the tier as a badge or tooltip (consistent with the v1.0 styling vocabulary).
1. README regeneration produces per-family "what works / what doesn't" tooltips populated from the YAML, so users see honest capability advertisement at install time.
1. 1-2 scaffolded stream-dock-family devices (siblings of AKP153/AKP03/AKP05; AKB980 PRO explicitly excluded — vendor driver requires `wine`) are promoted Tier 0 → Tier 2 with their per-device `docs/protocols/<family>/<device>.md` documenting the byte-0 Report ID convention (Pitfall 9) and using byte-wise multi-byte field writes that pass clang-tidy `cppcoreguidelines-pro-type-reinterpret-cast` (Pitfall 10).
   **Plans**: TBD
   **UI hint**: yes

## Progress

| Phase                       | Milestone | Plans Complete | Status           | Completed  |
| --------------------------- | --------- | -------------- | ---------------- | ---------- |
| 1. SEC-003 Plugin Host      | v1.0      | 1/1            | Complete (retro) | 2026-05-03 |
| 2. QML Singleton Sweep      | v1.0      | 1/1            | Complete (retro) | 2026-05-04 |
| 3. Architectural Decisions  | v1.1      | 1/1            | Complete         | 2026-05-14 |
| 4. Hot-plug Hardening       | v1.1      | 0/7            | Planned          | —          |
| 5. Time-Sync Scaffolding    | v1.1      | 0/1            | Not started      | —          |
| 6. CR-01 Win32 Env Fix      | v1.1      | 0/?            | Not started      | —          |
| 7. WR-01 Trust-Roots Parser | v1.1      | 0/?            | Not started      | —          |
| 8. Scaffolded-Device Wiring | v1.1      | 0/?            | Not started      | —          |
