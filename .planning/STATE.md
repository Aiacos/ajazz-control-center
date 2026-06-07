---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Modular Plugin & Binding System
status: executing
stopped_at: v2.0 roadmap creation
last_updated: '2026-06-06T11:25:49.718Z'
last_activity: 2026-06-06
progress:
  total_phases: 6
  completed_phases: 0
  total_plans: 3
  completed_plans: 2
  percent: 0
---

# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-06-06)

**Core value:** Honest, capability-driven control of AJAZZ hardware with a sandboxed plugin
system — never lying about what a device can do, never crashing when a device is yanked, never
silently leaking host state into plugin children.

**Current focus:** Phase 30 COMPLETE — autonomous run RESUMED at Phase 31 (2026-06-07, Opus-everywhere)

## Current Position

Phase: 31 (ActionInstance Core Model + Profile Schema v2) — starting
Plan: not started
Status: EXECUTING — autonomous run resumed 2026-06-07 21:15 Europe/Rome; model_profile=quality + all sub-agents overridden to Opus (no Sonnet-quota exposure)
Next: Phase 31 → 32 → 33 → 34 → 35
Last activity: 2026-06-07

### Progress bar

```
v2.0 [█████                         ] 1/6 phases (17%)
Phase 30 DONE
Phase 31 ....
Phase 32 ....
Phase 33 ....
Phase 34 ....
Phase 35 ....
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

| Plan  | Decision                                                                                                                                                |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 30-01 | UNIFY IPluginHost2: UnifiedPluginHost aggregator owns both PluginManager (sdPlugin) and OutOfProcessPluginHost (Python); overrides keep-separate        |
| 30-01 | SKU-boundary enforcement (akp05e, akp03, akp153) in plugin dispatch is CODE-REVIEW-ONLY; no CI grep gate                                                |
| 30-01 | QHostAddress::Any + SIGPIPE invariants are permanent CI assertions (Linux-only, fail-fast)                                                              |
| 30-02 | HOST-02: sentinel-UUID on connect (__pending__ prefix) rekeyed at registerPlugin; m_live.find authority for crash-window eligibility in onProcessFailed |
| 30-02 | seedLiveForTest() test seam added to PluginManager; crash-window simulation tests must seed m_live before calling onProcessFailed                       |

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

Last session: 2026-06-06T13:42:00Z
Stopped at: Completed 30-02-PLAN.md (HOST-02 sentinel-UUID + pre-reg exit guard; 696/696 tests green)
Resume: `/gsd:execute-phase 30` (Plan 3 of 3 remaining)
