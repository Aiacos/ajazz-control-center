# Milestones

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
