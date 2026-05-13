# Roadmap: AJAZZ Control Center

## Overview

This roadmap is **retro-fitted onto an active brownfield repo** as of 2026-05-12. It does not attempt to project forward — it captures the two most recent thematic clusters of work as discrete phases so they can be reviewed via `/gsd-code-review`. Forward planning will be added when the next milestone is scoped.

## Phases

**Phase Numbering:**

- Integer phases (1, 2): Retro-fitted clusters from work already on `main`.

- [x] **Phase 1: SEC-003 Plugin Host** — Wire `OutOfProcessPluginHost` into `Application`; close robustness gaps; expose plugin headers to app target.

- [x] **Phase 2: QML Singleton Sweep** — Fix branding light-theme silent-fail; preventive sweep across 5 other singleton services.

## Phase Details

### Phase 1: SEC-003 Plugin Host

**Goal**: Production-ready out-of-process plugin host wired into the Application lifecycle, with signed-manifest gating and CMake plumbing for plugin headers.
**Depends on**: Nothing (retro-phase)
**Commits** (chronological):

1. `65015a1` chore(review): address self-review pass on SEC-003 #51 changes
1. `f044660` fix(plugins): close two pre-existing robustness gaps in SEC-003 path
1. `43add98` feat(app): wire OutOfProcessPluginHost into Application (SEC-003 #51)
1. `4b4cbcc` fix(cmake): always expose plugins headers to the app target

**Success Criteria** (what must be TRUE):

1. `Application` constructs and owns an `OutOfProcessPluginHost` instance
1. `LoadedPluginsModel` is wired through the host so the QML page reflects plugin state
1. Plugin manifests are signature-verified before load
1. App target consistently sees plugins headers (no link-time surprises)

**Plans**: 1 retro plan

- [x] 01-01: Wire OutOfProcessPluginHost end-to-end with manifest signer + CMake exports

### Phase 2: QML Singleton Sweep

**Goal**: Eliminate the `QML_SINGLETON`-macro-only registration anti-pattern that silently double-instantiated services per QML import. Fix the surfaced branding light-theme bug, then prophylactically apply the same fix across 5 other affected services.
**Depends on**: Nothing (retro-phase)
**Commits** (chronological):

1. `d7f932f` fix(branding): light theme silently rendered dark — QML_SINGLETON dual-instance
1. `e221b21` chore(qml): prevent same dual-instance bug on five other QML_SINGLETON services

**Success Criteria** (what must be TRUE):

1. Light theme renders correctly when selected (the surfaced symptom)
1. Each affected service has exactly one C++ instance shared across QML imports
1. Tests for `BrandingService` and `ThemeService` reflect the corrected lifecycle

**Plans**: 1 retro plan

- [x] 02-01: QML_SINGLETON dual-instance fix + preventive sweep across 5 services

## Progress

| Phase                  | Plans Complete | Status           | Completed  |
| ---------------------- | -------------- | ---------------- | ---------- |
| 1. SEC-003 Plugin Host | 1/1            | Complete (retro) | 2026-05-03 |
| 2. QML Singleton Sweep | 1/1            | Complete (retro) | 2026-05-04 |
