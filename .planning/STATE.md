---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Modular Plugin & Binding System
status: executing
stopped_at: Completed 35-01-PLAN.md (WINPLG-01/02 classifier + native-run gate + ADR; 798/798 green). Next 35-02 (status chip).
last_updated: '2026-06-08T19:30:25.180Z'
last_activity: 2026-06-08 -- Plan 35-01 executed
progress:
  total_phases: 6
  completed_phases: 5
  total_plans: 21
  completed_plans: 19
  percent: 83
---

# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-06-06)

**Core value:** Honest, capability-driven control of AJAZZ hardware with a sandboxed plugin
system — never lying about what a device can do, never crashing when a device is yanked, never
silently leaking host state into plugin children.

**Current focus:** Phase 35 — windows-plugin-support-security-hardening-milestone-verifica

## Current Position

Phase: 35 (windows-plugin-support-security-hardening-milestone-verifica) — EXECUTING

Phase: 34 (per-app-profiles-event-parity-audit) — COMPLETE + VERIFIED (human_needed; 7 live/hardware items deferred to 34-HUMAN-UAT.md). 5/5 must-haves (automated). All 8 reqs (APROF-01..04, EVENT-01..04) code-verified. Code review found + fixed a real BLOCKER (CR-01: X11 missing XSetErrorHandler → BadWindow exit) + 5 warnings (WR-01 manual-selection clobber, WR-02 ApplicationsToMonitor filter, WR-03/04/05). 785/785 ctest. User chose Continue-to-35 (live niri walk deferred).

Phase: 33 (Property Inspector E2E) — COMPLETE + VERIFIED (human_needed: PI render/lifecycle-on-open + criterion-5 deferred by user). 4/5 must-haves; PI-01/02/04 done, PI-03 partial. 728/728 + 17 qml.
Plan: 2 of 3
Status: Executing Phase 35 — Plan 35-01 DONE (WINPLG-01/02: classifyWindowsPlugin CodePath-suffix + bounded MZ-magic corroborator, supportsCurrentPlatform WS-only-IPC-runs-native / VendorDll Windows-only with Wine DEFERRED, PluginInfo.winClass plain-int cache stamped at scan time, WINPLG-01 ADR). 798/798 ctest green (+13 new [win-plugin-classification] cases). COD-031 clean; no Wine launcher; LOCKED linux-accept preserved.
Next: Plan 35-02 (status chip on LoadedPluginsModel/LoadedPluginsPage consuming PluginInfo.winClass, objectName-addressed per VERIF-01) + 35-03 (PLGSEC verify-locks + VERIF audit doc).
Last activity: 2026-06-08 -- Plan 35-01 executed (Windows plugin classification + native-run gate)
Tests: 798/798 ctest green (linux-release, incl 17 qml).
Stopped at: Completed 35-01-PLAN.md (WINPLG-01/02 classifier + native-run gate + ADR; 798/798 green).

### Progress bar

```
v2.0 [██████████████████████████    ] 5/6 phases (Phase 34 VERIFIED human_needed; Phase 35 next)
Phase 30 DONE
Phase 31 DONE (Plan 01 + 02)
Phase 32 DONE (5 plans; BIND-03/04/05/06/07 + EDIT-01; EDIT-01 canvas-collapse live-fixed; 2 Multi/Toggle live walks deferred to HUMAN-UAT)
Phase 33 DONE (3 plans; PI-01/02/04; PI-03 relay live-confirmed; CR WR-01 fixed; render/lifecycle-on-open + criterion-5 deferred to HUMAN-UAT)
Phase 34 DONE + VERIFIED (5/5 plans; APROF-01..04 + EVENT-01..04; CR-01 BLOCKER + 5 warnings fixed; 785/785; 7 live/hardware items in 34-HUMAN-UAT.md; logind wake-source deferred)
Phase 35 .... (NEXT — Windows plugins + security hardening + milestone verification; FINAL)
```

## Blockers / Concerns

- **RESOLVED (2026-06-07):** The 2026-06-06 Sonnet-quota pause is cleared — quota window reset
  (~18:00 Europe/Rome) and the autonomous run now sets `model_profile=quality` with every
  Sonnet-default sub-agent overridden to Opus in `.planning/config.json`, so there is no
  Sonnet-quota exposure across Phases 31–35.

- **Phase 30 advisory follow-ups (non-blocking):** (1) live debug-channel merged-inventory check of
  the unified host (`.sdPlugin` + Python via `plugin.list`) deferred during close-out — see
  30-VERIFICATION.md "Human Verification"; (2) Phase 30 code-review gate (gsd-code-review) was NOT
  run (Sonnet down) — optionally run `/gsd:code-review 30` after reset.

## mirajazz sidecar (experiment/mirajazz branch baseline)

The Stream Dock families (AKP03 / AKP05-N4 / AKP153) are driven by the mirajazz Rust sidecar
(`streamdock-host/`, proxied by `SidecarStreamDockDevice`). The removed C++ AKP wire backends
(`akp03/05/153.cpp` + `*_protocol.hpp` + `makeAkp03/05/153`) were removed in Slice D and must
NOT be reintroduced (VERIF-02). AKP815 keeps its custom C++ backend. The sidecar holds a
persistent HID handle (no per-interaction open/close, no wedge — hardware-confirmed 2026-06-01).

Sidecar encoder/touch input decode is **HARDWARE-GATED/PROVISIONAL** — the demo unit
(`0x0300:0x3004`) delivers zero input across all five verification methods. Routing pipeline tests
use synthetic `input.*` debug RPCs; wire values wait for a retail AKP05E.

## v2.0 Phase Dependency Map

```
Phase 30 (HOST, ADR)
    └── Phase 31 (BIND core model)
            ├── Phase 32 (BIND wire + Multi/Toggle + EDIT)
            │       └── Phase 34 (APROF + EVENT)
            └── Phase 33 (PI round-trip)
                    └── Phase 34 (APROF + EVENT)
                            └── Phase 35 (WINPLG + PLGSEC + VERIF)
```

Phase 33 depends on Phase 31 only (not Phase 32); Phases 32 and 33 can run concurrently
(under the 2-agent cap). Phase 34 depends on both 32 and 33.

## Key Architecture Decisions (v2.0 context)

- **Do NOT modify the mirajazz crate** — sidecar protocol changes go in `streamdock-host/`
- **COD-031** — `nlohmann::json` PRIVATE to `ajazz_plugins` only; `src/core/include/` must have zero nlohmann hits
- **IPluginHost2 UNIFY decision** — ADR committed (30-ADR-plugin-host-unification.md); UNIFY overrides keep-separate research verdict (user override); UnifiedPluginHost aggregator owns both PluginManager and OutOfProcessPluginHost; SKU-boundary enforcement code-review-only
- **profileChanged → populateContextsForActivePage** is the foundational wire fix (Pitfall 3); must land in Phase 32 before any binding work is declared done
- **QWebEngineScript::DocumentCreation** is the mandatory injection point for the cefQuery polyfill; never defer to runJavaScript
- **Composition at Application root** — Application owns all services; seams via std::function injection, not raw pointer coupling

## Performance Metrics

**v2.0 baseline (2026-06-06):**

- Tests at start: 694/694 ctest green (linux-release, post audit 2026-06-05)
- Phases: 6 (Phases 30–35)
- Plans: TBD (filled by plan-phase)

**Historical velocity:**

- v1.3 sustained ~54 plans across 16 phases
- Cap concurrent execute agents at 2 in autonomous runs

## Accumulated Context

### Decisions (v2.0)

| Plan            | Decision                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 30-01           | UNIFY IPluginHost2: UnifiedPluginHost aggregator owns both PluginManager (sdPlugin) and OutOfProcessPluginHost (Python); overrides keep-separate                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| 30-01           | SKU-boundary enforcement (akp05e, akp03, akp153) in plugin dispatch is CODE-REVIEW-ONLY; no CI grep gate                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| 30-01           | QHostAddress::Any + SIGPIPE invariants are permanent CI assertions (Linux-only, fail-fast)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| 30-02           | HOST-02: sentinel-UUID on connect (__pending__ prefix) rekeyed at registerPlugin; m_live.find authority for crash-window eligibility in onProcessFailed                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| 30-02           | seedLiveForTest() test seam added to PluginManager; crash-window simulation tests must seed m_live before calling onProcessFailed                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| 31-01           | KeyState relocated into action_instance.hpp (depends only on capabilities.hpp); profile.hpp includes it one-directionally to break the include cycle                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| 31-01           | ActionState is a thin struct wrapping KeyState; ActionInstance carries optional id (wire "id", reader also accepts "uuid"); settings = escaped JSON str                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| 31-01           | Reader folds legacy singular "state" -> states[] of one; writer always emits states[]; gate on key presence, never \_schemaVersion; currentState clamped                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| 31-02           | BIND-02 verified by a 7-case [action_instance] suite (0/1/3-state + 2-children round-trip, v1->v2 fold, no-instance -> nullopt on Binding+EncoderBinding, currentState clamp); -R action_instance 7/7, full 708/708                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| 31-CR           | Code-review fixes: JsonReader DepthGuard kMaxDepth=64 on readActionInstance children + skipValue (CR-01 reader-recursion DoS); escape() emits \\u00XX for ctrl chars (WR-01); deterministic state/states fold (WR-02); +5 tests -> action_instance 12/12, full 713/713                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| 33-01           | PI-04: titleParametersDidChange emitted INLINE after each willAppear (no separate pass); appear/disappear via new controller inspectorOpened/inspectorClosed signals routed through the Application seam to sendEvent (controller stays free of a raw SdPluginServer\*); PI->PI switch teardown fires disappear; titleParameters default VALUES [ASSUMED] (Catch2 locks shape, human-verify confirms values); no sendEvent allowlist                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| 33-02           | PI-02 thin-UI: open/close affordances are plain SecondaryButtons (not Switch -- qml.invoke fires onClicked but cannot reproduce Switch.toggled, CLAUDE.md harness gap); openPiButton reuses maybeLoadInspector() rather than re-resolving PI path/uuid/ctx; NO C++ change (loadInspector/closeInspector already Q_INVOKABLE -- research A4 openForCurrentSelection fallback unnecessary, tightens the GREEN-files guard); PI-03 stays partial until the real-PI human-verify in 33-03                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| 34-01           | APROF-01 foundation: IActiveWindowWatcher kept Qt-free in ajazz_core (std::function callback seam, RESEARCH A1); all Wayland/X11/AppKit live in app-tier per-OS TUs so COD-031 holds. StubActiveWindowWatcher is PUBLIC (not anonymous like the input-synth stub) so the debug facade + tests reach injectForeground; window.setForeground RPC dynamic_casts to it                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| 34-01           | Qt6::WaylandClient find_package + qtwaylandscanner codegen + libX11 are Linux-gated (qtwayland is not built on macOS/Windows; REQUIRED would break those configures). Generated qwayland-\*.cpp compiled with -w (trips -Werror old-style-cast/sign-conversion; generated code). RED Wave-0 scaffolds use Catch2 [!shouldfail] (registered + fail-as-expected, pending Plan 04). .mm excluded from clang-format (no ObjC block in .clang-format)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| 34-04           | APROF-02 auto-switch REUSES profileChanged->populateContextsForActivePage reconcile behind an idempotent no-op-on-identical guard (CR WR-01 / T-34-04-01 DoS); resolveProfileForApp matches Profile::applicationHints case-insensitively (reads profiles off disk; debounced call) + device-default fallback. No new willAppear/willDisappear path (grep-verified)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| 34-04           | Host-event fan-out (applicationDidLaunch/Terminate, systemDidWakeUp) factored into app_event_dispatch free fns over SdPluginServer\* + a registered-uuid set so the registered-only(V4)/length-bound(V5)/never-cache-socket(Pitfall 4) contract is UNIT-TESTABLE against a live loopback server (application.cpp is not in the unit target). Application dispatch\* are thin wrappers. switchToProfile token = lookup key only (V5-bound, name-match device-scoped, bad-token reject)                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| 34-04           | systemDidWakeUp DISPATCH implemented + unit-tested via the injectable Application::dispatchSystemWake synthetic-wake seam; the real Linux logind PrepareForSleep D-Bus source is DEFERRED (dispatch proven; OS source = candidate for 34-05). applicationDidTerminate is best-effort focus-loss (watcher reports focus, not process exit)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| 34-05           | APROF-03 UI: assign-profile surface writes Profile::applicationHints (schema-correct wire key) via by-id ProfileController Q_INVOKABLEs addAppProfileMapping/removeAppProfileMapping (V5 256-char bound + case-insensitive dup guard; active = in-memory+saveActiveProfile, non-active = read/mutate/write-back via library index). Wayland/GNOME capability-warning chip is AMBER (Theme.chip\*Warning), visible-on-ABSENT, driven by a ProfileController foregroundCapabilityAvailable NOTIFY property Application injects from IActiveWindowWatcher::capabilityAvailable(). Added window.setCapability debug RPC (singleton not findByName-addressable) to live-verify the chip. HEADLESS-verified: chip both ways + non-empty warningText, add/remove wrote+persisted hints, screenshot read. INTERACTIVE auto-switch gate is HUMAN-UAT — window.setForeground synthetic injection only works on the recording stub, NOT this full build's real watcher backend |
| Phase 35 P35-01 | 35min                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |

### Pending Todos (pre-Phase 30)

1. Wayland foreground-window detection needs a sub-agent research sweep before Phase 34
   implementation: `wlr-foreign-toplevel-management-v1` compositor coverage map (wlroots,
   Hyprland, KDE, GNOME gap), `xdotool`/`hyprctl`/`swaymsg` subprocess fallback. X11,
   Windows, macOS paths are HIGH confidence and can start without waiting.

1. Windows-only `.sdPlugin` plugin ecosystem composition (JS-bundled vs pure Win32 PE) needs
   a WINPLG-01 feasibility spike at Phase 35 start — do not implement the native-exe path
   before running the spike.

1. The PI human-verify checkpoint (Phase 33 criterion 5) requires the user to interact with
   a real plugin's PI HTML. Agree with the user when this checkpoint will be run.

### Blockers / Concerns

- **HARDWARE-GATED**: encoder/touch input wire decode (retail AKP05E needed). Does NOT block
  any v2.0 phase — routing pipeline is tested via synthetic `input.*` RPCs.

- **PLATFORM-GATED (Wayland)**: per-app profile switching on Wayland compositors without a
  foreign-toplevel protocol. Phase 34 must document the limitation and show a UI warning chip.

- **Concurrent agent cap**: 2 max in autonomous runs (v1.1 retrospective lesson, CLAUDE.md).

## Session Continuity

Last session: 2026-06-08T19:30:18.794Z
Stopped at: Completed 33-02-PLAN.md (PI-02 thin-UI: piPanelLoader/piWebView/openPiButton/closePiButton objectNames + open/close SecondaryButton affordances on Inspector.qml/PIWebView.qml; QML-only, zero C++ -- loadInspector/closeInspector already Q_INVOKABLE; PI-01+PI-02 marked complete, PI-03 partial pending the real-PI human-verify in 33-03; 728/728 + 17/17 qml green; live debug-channel pass for criteria 1/2/3 is the orchestrator's consolidated step, exact ajazz-debug commands in 33-02-SUMMARY)
Resume: `/gsd:execute-phase 32` (all code/test plans 01/02/03/04/05 landed; next is the consolidated live debug-channel pass + phase-verify for Phase 32)
