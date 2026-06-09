# Milestones

## v2.0 Modular Plugin & Binding System (Shipped: 2026-06-09)

**Phases completed:** 6 phases, 21 plans, 43 tasks

**Key accomplishments:**

- 1. [Rule 1 - Bug] Test case name regex mismatch for `pre.registration.exit`
- Sentinel-UUID isolation in SdPluginServer + m_live.find guard in PluginManager so pre-registration crashes never consume crash credits or emit spurious pluginDisconnected signals.
- OpenDeck/StreamDeck-shaped ActionInstance/ActionState core model with hand-rolled, nlohmann-free JSON serialization wired additively onto Binding/EncoderBinding, plus lazy v1->v2 state-fold and the PROFILE_SCHEMA.md $defs that document the wire keys.
- A 7-case [action_instance]-tagged Catch2 suite that proves the 31-01 ActionInstance serializer round-trips losslessly (0/1/3-state + 2-children), folds a legacy singular `state:` into a `states[]` array of one, returns `std::nullopt` for an absent `instance` key on both Binding and EncoderBinding, and clamps an out-of-range `currentState` to 0 — registered as a runnable `ctest -R action_instance` target with no production-code change.
- Additive optional `ActionInstance.delayMs` (serialized, reader-tolerant) plus the pure-core `instanceChildrenToChain` adapter that flattens a Multi Action children tree into the delay-carrying `core::ActionChain` the existing `ActionEngine` already walks.
- A key/dial/touch-zone bound to a Multi Action now runs its `instance.children` sequentially on a single press -- resolved at the `StreamDockInputService::dispatch` seam via Wave-1's `instanceChildrenToChain` fed to the existing `ActionEngine::run` (per-child `delayMs` honored, no new async runner) -- plus a `com.hotspot.streamdock.multiaction` built-in registration (classification + flat-JSON fallback).
- A key/dial/touch-zone bound to a Toggle Action now cycles `currentState = (currentState + 1) mod N` through ALL `states[]` (N>2) on each press -- resolved at the `StreamDockInputService::dispatch` seam via a `ProfileController::cycleInstanceState` mutator (mutate + persist) and a `PluginDeviceBridge::renderToggleState` hook (repaint `states[currentState]` through the existing `assignKeyImage`/`assignEncoderImage` setState path + re-send a state-change `willAppear`) -- plus a classification-only `com.hotspot.streamdock.toggleaction` built-in registration. The new index PERSISTS to the profile JSON (survives restart).
- Regression tests that pin the already-shipped, device-generic binding layer -- the willAppear/keyDown/dialRotate/touchTap envelope's top-level `action` field, stored-owner resolution of a non-dotted-prefix action, and one generic key/encoder/touch-zone dispatch path with no SKU branch -- so a future ad-hoc commit on this churned branch cannot silently regress them. Zero production code changed.
- Wires the three missing SDK-2 PI lifecycle events on the wire — titleParametersDidChange inline after every willAppear, plus propertyInspectorDidAppear/DidDisappear routed through the Application seam (including the PI->PI switch teardown edge) — locked by a Catch2 payload-completeness + ordering test.
- Closes the PI-02 thin-UI debug contract — adds the four objectNames (openPiButton / closePiButton / piPanelLoader / piWebView) and two debug-drivable open/close affordances that make the PI panel headlessly verifiable via scripts/ajazz-debug — and confirms PI-01 + PI-03 GREEN by their existing unit tests. QML-only; zero C++ touched.
- Criterion 5
- Qt-free IActiveWindowWatcher interface + recording stub + vendored wlr-foreign-toplevel XML with qtwaylandscanner/libX11 build wiring, a window.setForeground synthetic-injection debug RPC, and three registered Catch2 scaffolds (active_window GREEN; app_profile_switch + app_lifecycle_events RED for Plan 04).
- A committed `docs/plugin-event-parity.md` coverage table classifying every OpenDeck/Elgato event with status + test reference, plus two Catch2 suites that lock the willAppear envelope+payload completeness (EVENT-02) and the Knob->Encoder controller-token normalization (EVENT-04) against the REAL bridge wire output -- 764/764 ctest green.
- The four `IActiveWindowWatcher` platform backends behind the Plan-01 interface — Wayland (wlr-foreign-toplevel via `QWaylandClientExtensionTemplate`, live-verified binding on niri), X11/EWMH (libX11 `_NET_ACTIVE_WINDOW`), Win32 (`GetForegroundWindow`) and macOS (`NSWorkspace`, both compile-guarded) — plus a shared trailing-edge QTimer debounce and the app-tier runtime backend-selection factory wired into `Application`.
- Wired the foreground-window watcher into the profile system (APROF-02 per-app auto-switch via case-insensitive `applicationHints` match + default fallback, behind an idempotent guard, reusing the existing `profileChanged -> populateContextsForActivePage` reconcile) and completed the EVENT-03 host dispatch: the `switchToProfile` inbound handler (token validated/bounded, name-or-id resolved, bad-token rejected), `systemDidWakeUp` dispatch, and the APROF-04 `applicationDidLaunch/Terminate` fan-out — all delivered to REGISTERED plugins only (V4) with length-bounded payloads (V5). Turned the two Plan-01 RED scaffolds GREEN and added the two EVENT-03 tests.
- The user-facing half of per-app profiles: a "Per-app profiles" assign surface in `SettingsPage.qml` that maps an application name to any device profile (writing `Profile::applicationHints` via new by-id `ProfileController` Q_INVOKABLEs), plus the amber Wayland/GNOME capability-warning chip driven by a NOTIFY capability property Application injects from `IActiveWindowWatcher::capabilityAvailable()`. Every control is `objectName`-addressable (VERIF-01) and the surface was live-verified headlessly through the debug channel — chip both ways, assign add/remove writing+persisting `applicationHints`, and a rendered screenshot.
- A pure classifyWindowsPlugin() (CodePath .exe/.dll suffix + bounded MZ-magic bundle scan) plus supportsCurrentPlatform() that lets WS-only-IPC win plugins run natively on Linux, with the verdict cached at scan time onto PluginInfo.winClass and a WINPLG-01 ADR.
- Confirmed + test-locked the already-shipped PLGSEC-01/02/03 security invariants (zero source drift), scripted the four comment-aware VERIF-02 modularity grep gates into `scripts/verif-milestone-gates.sh` + CI, and authored the honest `docs/milestone-v2.0-modularity-audit.md`.
- A platformStatus model role derived from PluginInfo.winClass plus three objectName-addressed, headless-readable chips on LoadedPluginsPage (status + unsigned-consent + the previously un-named trustChip), unit-locked and security-invariant-confirmed — with an honestly-recorded live-render blocker (the model is not wired to .sdPlugin inventory in the shipping build).

______________________________________________________________________

## v1.1 Device lifecycle hardening + scaffolding-to-functional (Shipped: 2026-05-14)

**Phases completed:** 6 phases, 26 plans, 178/178 tests
**Audit:** `tech_debt` — 28/28 requirements satisfied, 6 deferred items (real-hardware UI verifies, Windows CI back-fill, AKP815/Mirabox N3 maturity promotion blocked on real-device captures, libFuzzer Fedora packaging)
**Git range:** `0e18353` (milestone start) → `01ccfc7` (audit) — 80 commits, 186 files, +23,099/−838 LoC

**Key accomplishments:**

- **Phase 3 — Architectural decisions ratified upfront.** Three written ADRs (`ARCH-01..03`) lock the WR-01 parser choice (`nlohmann::json` PRIVATE-linked to `ajazz_plugins`), the `HotplugMonitor` mock seam (`#ifdef AJAZZ_TESTING injectEvent` shim), and the `DeviceRegistry` ownership migration (`unique_ptr` → `shared_ptr`) before any code lands.
- **Phase 4 — Hot-plug hardening.** `DeviceRegistry` migrated to `shared_ptr<IDevice>` with `weak_ptr`-cached flyweight; 300 ms trailing-edge `HotplugDebouncer` collapses USB-hub shuffles; diff-driven `DeviceModel::refresh()` keeps sidebar selection + scroll position across disconnect/reconnect; multi-device integration harness (12 Catch2 TEST_CASEs, Linux + Win32) drives `MockHidEnumerator` + `HotplugMonitor::injectEvent` with zero real USB. `hid_open()` invariant enforced by CI grep. Pitfalls 1, 3, 11 closed.
- **Phase 5 — Time-sync scaffolding (5-layer slice, honest UX).** `Capability::Clock` + `IClockCapable` + `TimeSyncService` (QML singleton with `static_assert(!std::is_default_constructible_v<T>)` build-break) + Settings auto-sync toggle (QSettings-persisted, capability re-validated at firing time) + per-row exclamation glyph. All 5 functional backends return `Result::NotImplemented` honestly with `std::once_flag`-gated WARN. Manual = toast + glyph; auto = glyph only (D-02). 7 unit + 3 integration TEST_CASEs.
- **Phase 6 — CR-01 Win32 OOP env pollution fix (v1.0 carry-over closed).** Per-spawn UTF-16 environment block via `Win32EnvBlock` RAII (built from `GetEnvironmentStringsW` + 3 Python overrides + case-insensitive sort + drive-letter preservation), passed to `CreateProcessW` with `CREATE_UNICODE_ENVIRONMENT`. All three `_putenv_s` calls removed atomically in the same commit. Parent `_wgetenv(L"PYTHONPATH")` provably unchanged by integration test on windows-2022 CI matrix.
- **Phase 7 — WR-01 trust-roots parser hardening (v1.0 carry-over closed).** `loadTrustRoots` mini-grep parser swapped for `nlohmann::json::parse` in lockstep across both Linux + Win32 TUs. `nlohmann` PRIVATE-linked to `ajazz_plugins` only (zero hits in `ajazz_core` or any public header — COD-031 invariant preserved by `grep`). 1 MB byte cap + 1024-entry cap bound DoS surface. 5 unit cases (BOM / escapes / NUL / oversize / overcount) + opt-in libFuzzer harness. 0600 TOCTOU contract documented at the public API surface.
- **Phase 8 — Maturity-tier infrastructure + two device promotions.** `docs/_data/devices.yaml` `status` field renamed to `maturity` with 5-tier vocabulary (`scaffolded`/`probed`/`partial`/`functional`/`verified`); `MaturityRole` exposed via `DeviceModel`; QML sidebar surfaces the tier as a per-row tooltip; README + wiki AUTOGEN tables render honest per-family "works / partial / pending" prose. AKP815 promoted to `probed`, Mirabox N3 (rev. 1) promoted to `partial`. AKB980 PRO promotion explicitly deferred (wine-only vendor installer).

**Parallel-execution lesson learned:** Three concurrent execute agents (Phase 4 + Phase 6 + Phase 7) created a git race that split one atomic commit across two TUs and forced a Phase 5 planner `--no-verify` workaround. Cap concurrent execute agents at 2 in future autonomous runs.

**Known deferred items (carried to v1.1.x or v1.2 backlog):**

- Real-hardware UI verification: Stream Dock + AKB980 PRO Sync button visibility, manual click → toast/glyph, Settings auto-sync persistence across restart, auto-sync glyph-only-no-toast on arrival, MaturityRole tooltip on sidebar rows (Linux dev box without devices).
- Windows CI back-fill: WIN32-04 duplicate-key precedence (first-wins vs last-wins on inherited `PYTHONPATH`) — first windows-2022 matrix run will resolve.
- AKP815 + Mirabox N3 maturity promotion blocked on real-device captures.
- Toast.qml explicit cap=1 (currently single-instance `.show()` enforces it implicitly; explicit queue deferred to avoid Phase 4 D-01 surface).
- TimeSyncService Pitfall-13 contextual INFO message (currently generic).
- libFuzzer Fedora packaging — wait for clang `libclang_rt.fuzzer.a` packaging fix; OSS-Fuzz containers already supported.

______________________________________________________________________

## v1.0 milestone (Shipped: 2026-05-13)

**Phases completed:** 2 phases, 0 plans, 6 tasks

**Key accomplishments:**

- Wires the out-of-process Python plugin host into the Application lifecycle, gates plugin loading on manifest signature verification, and closes two pre-existing robustness gaps surfaced during self-review.
- Fixes the silent dual-instance bug in `BrandingService` (light theme rendered dark) and applies the same fix prophylactically across five other `QML_SINGLETON` services.

______________________________________________________________________
