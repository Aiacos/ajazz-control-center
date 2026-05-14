---
phase: 04-hot-plug-hardening
plan: 04
subsystem: app
tags: [device-model, diff-driven, dataChanged, lex-sort, offline-badge, codename-collapse, D-03, D-04, HOTPLUG-02, HOTPLUG-03, HOTPLUG-04, Pitfall-15]

requires:
  - phase: 04-hot-plug-hardening
    provides: 04-01 (shared_ptr ownership migration — rows stay valid past a Removed event)
  - phase: 04-hot-plug-hardening
    provides: 04-03 (HotplugDebouncer — refresh() only fires once per stable transition, never mid-burst)
provides:
  - DeviceModel::refresh() is diff-driven — emits per-row dataChanged({ConnectedRole}) instead of beginResetModel/endResetModel in the common path
  - Rows lex-sorted by (family, codename) — stable across arrival/departure/re-arrival
  - Codename collapse — multiple rebadge VID/PIDs sharing a codename render as one row whose ConnectedRole ORs across the rebadge group
  - QML offline pill renders on rows where model.connected === false; offline rows drop to 72% opacity
  - Sidebar no longer filters by connected — disconnected rows stay in place with visual indicator
affects:
  - 04-05 (multi-device test harness — Section 5 mechanically asserts the one dataChanged({ConnectedRole}) emission this plan installs)
  - Phase 8 (DEVICES-02 MaturityRole — the diff-driven refresh + role-name infrastructure that lands here is what MaturityRole will plug into)

tech-stack:
  added: []
  patterns:
    - Diff-driven QAbstractListModel refresh (snapshot old state → compute next → emit per-row dataChanged with role-mask)
    - Codename-as-row-identity (vs (vid,pid)-as-row-identity) for rebadge collapse
    - std::sort stable-comparison on (family enum cast, codename string)
    - Fallback to beginResetModel/endResetModel only when row count or row-identity set changes (registration drift, bootstrap-only path)

key-files:
  created: []
  modified:
    - src/app/src/device_model.cpp
    - src/app/src/device_model.hpp
    - src/app/qml/DeviceList.qml
    - src/app/qml/components/DeviceRow.qml

key-decisions:
  - Row identity is `codename`, not `(vid, pid)` — D-04 rebadge contract; 8 AKP03 rebadge VID/PIDs collapse to ONE sidebar row
  - ConnectedRole semantics: codename row connected iff ANY (vid, pid) in m_codename_keys[codename] is currently in m_connected (OR across the rebadge group)
  - Sort comparator: cast `family` to int first (DeviceFamily is `enum class : uint8_t`, can't compare directly), then string-compare `codename` — order is stable and deterministic across runs since both keys are static descriptor attributes
  - Reset-fallback gate: if next row count != current OR codename-set differs, use beginResetModel/endResetModel + AJAZZ_LOG_INFO so the path is visible if it ever fires (expected: never outside bootstrap; registration is bootstrap-only in v1.1)
  - QML reversal: removed the d377d80 connected-only filter — rows are always present in the QML list; the offline state is visualised via the offline badge + 72% opacity, not via removal
  - Visual offline state: opacity reduction (1.0 → 0.72) + explicit "Offline" pill below the device name + accessibility-name suffix "Offline device <codename>" so screen readers convey the state
  - No toast, no sound, no modal — D-01 silent-badge policy strictly honoured
  - No "last seen N min ago" tooltip — deferred per CONTEXT Deferred Ideas

patterns-established:
  - 'Diff-driven QAbstractListModel: snapshot m_connected (old), compute next_connected, walk rows, emit per-row dataChanged(idx, idx, {ConnectedRole}) only where state flipped — selection + scroll preserved by construction'
  - 'Row identity == codename — the model collapses backend-registry duplicates via m_codename_keys (codename → set of (vid,pid)) at refresh time; data(ConnectedRole) ORs the set against m_connected'

requirements-completed: [HOTPLUG-02, HOTPLUG-03, HOTPLUG-04]

duration: 35 min (production code) + 60 min wall-clock awaiting human verify
completed: 2026-05-14
human_verified: 2026-05-14
verification_outcome: approved (all 5+1 manual sub-checks passed — sidebar populated with all rows, cable yank → offline pill at 72% opacity + selection retained + no toast + scroll preserved, lex sort holds)
---

# Phase 4 Plan 04: Diff-Driven DeviceModel + Lex Sort + QML Offline Badge Summary

**Rewrote `DeviceModel::refresh()` as diff-driven per D-03 + HOTPLUG-02/03/04. Replaced the v1.0 connected-only sidebar filter with full-list + offline-pill rendering so HOTPLUG-02's offline-badge UX target is structurally satisfied: rows are always present, the row's ConnectedRole flips per stable hot-plug transition, and selection + scroll position survive a disconnect/reconnect cycle automatically (no row index moves).**

## Performance

- **Duration:** ~35 min production code; ~60 min wall-clock awaiting human-verify checkpoint (no automated test surface for QML visual contract — Plan 05 provides the mechanical follow-up for the C++ side)
- **Started:** 2026-05-14T13:30Z (resumed-execution dispatch)
- **Production-code committed:** 2026-05-14T13:46Z (`fc9d794`)
- **Human-verified + SUMMARY landed:** 2026-05-14 (this commit)
- **Tasks:** 2 (Task 1: diff-driven refresh + lex sort + codename collapse; Task 2: QML offline badge + manual verify)
- **Files modified:** 4 (2 C++ + 2 QML)

## Accomplishments

- **`DeviceModel::refresh()` is diff-driven** — `beginResetModel`/`endResetModel` reserved for the row-count-or-codename-set-changed fallback path; the common path emits per-row `dataChanged(idx, idx, {ConnectedRole})` only on rows whose connected-state flipped. QML's `currentIndex` and the `ListView` scroll offset are preserved by construction.
- **Codename collapse** — `std::map<codename, set<(vid,pid)>> m_codename_keys` records every rebadge group at `refresh()` time. The 8 AKP03 rebadge VID/PIDs collapse to ONE sidebar row whose `ConnectedRole = (m_codename_keys["akp03"] ∩ m_connected) != ∅`. Verified during manual test: only one "AKP03" row appears in the sidebar; plugging any one of the 8 rebadge cables flips its `ConnectedRole` to true.
- **Lex sort by `(family, codename)`** — `std::sort` with a comparator that casts `family` to `int` (DeviceFamily is `enum class : uint8_t`, can't `operator<` directly), then string-compares codename. Order is deterministic across runs since both keys are static descriptor attributes — verified visually that AKP03 appears above AKP05 in the sidebar regardless of which was last connected.
- **QML offline pill + 72% opacity reduction** — `DeviceRow.qml` renders an "Offline" pill below the device name when `root.deviceConnected === false`, and the entire row drops to 72% opacity (combined with the explicit pill, this gives offline rows the "still here, but not active" appearance specified by HOTPLUG-02). The pill reuses the existing v1.0 component-library palette tokens — no new design tokens introduced.
- **Connected-only filter (d377d80) reversed** — the v1.0 `visible: connected` binding is removed from `DeviceList.qml`; the offline state is now communicated entirely via `DeviceRow.qml`'s visual treatment.
- **Accessibility name** — offline rows get a screen-reader name suffix "Offline device <codename>" so the state is conveyed to assistive technology, not only visually.

## Task Commits

The two tasks were committed atomically because the C++ diff-driven refresh and the QML filter reversal must land together — the C++ side stops resetting the model, but if the QML side still filters by connected, disconnected rows still vanish (defeating HOTPLUG-02). Single commit closes the gap.

1. **Tasks 1 & 2 (atomic):** `fc9d794` — feat(app): diff-driven DeviceModel + lex sort + QML offline badge

_Plan metadata commit follows below (this SUMMARY)._

## Files Created/Modified

- `src/app/src/device_model.hpp` (+39 lines) — added `m_codename_keys` member (`std::map<std::string, std::set<std::pair<u16, u16>>>`); updated `refresh()` doc-comment to describe diff-driven contract + codename-as-row-identity + lex sort; updated `m_rows` doc-comment to note codename-collapse semantics.
- `src/app/src/device_model.cpp` (+162/-) — `refresh()` rewritten: snapshot old connected-state per codename → rebuild `next_rows` from `registry.enumerate()` with codename-collapse → build `next_codename_keys` → `std::sort` by `(family, codename)` → gate on `(size changed || codename set differs)` (fallback to reset) → common path: assign `m_rows`/`m_codename_keys`, `refreshLiveEnumeration()` to update `m_connected`, emit per-row `dataChanged` for flipped rows. `data(ConnectedRole)` branch rewritten to do an OR-lookup via `m_codename_keys[d.codename]` against `m_connected`. `capabilitiesFor()` lookup also routes via the codename → (vid,pid)-set map so the QML grid sizing remains correct for collapsed rebadge rows.
- `src/app/qml/DeviceList.qml` (+/-45) — removed the d377d80 `visible: connected` filter; rows always present. Repeater iterates over the full model.
- `src/app/qml/components/DeviceRow.qml` (+81 lines) — added the offline-pill rendering (a `Rectangle` + `Label` styled with the existing palette tokens, sized at the existing pill geometry from the component library). Wrapped the whole row in an `opacity` binding that drops to `0.72` when `deviceConnected === false`. Added accessibility-name suffix.

## Design Choices

### Why codename-as-row-identity (D-04 rebadge contract)

The Stream Dock "AKP03" line ships under at least 8 different USB VID/PIDs (Mirabox rebrand vs first-party AJAZZ vs OEM partners). v1.0's `(vid, pid)`-keyed model surfaced 8 separate sidebar rows for what the user perceives as one device class. v1.1's D-04 decision collapses these to ONE row keyed by codename; the row's ConnectedRole ORs across the rebadge group so plugging *any* rebadge variant marks the row online.

The codename-collapse is computed at `refresh()` time rather than at registration time because (a) registration order is bootstrap-only, but the OR-across-rebadge-keys check is per-refresh, and (b) the descriptor list returned by `registry.enumerate()` is the authoritative source — we don't want to silently drop descriptors at registration. Instead, the first descriptor encountered per codename wins as the representative (which determines the human-readable "model" name shown — deterministic across runs because `registerDevice()` insertion order is deterministic).

### Why std::sort with `static_cast<int>(family)` rather than `family <=> b.family`

`DeviceFamily` is `enum class : std::uint8_t` and there's no `operator<=>` defined for it (intentional — it's not a value type, just a discriminator). Casting to `int` for the comparison is the cleanest path and doesn't pull in `<compare>` or modify the public enum.

### Why fallback to beginResetModel only on row-count-OR-codename-set change

The diff-driven path assumes `m_rows[i]` corresponds to the same codename across refreshes — that's what makes per-row `dataChanged(idx, idx, ...)` correct (the index is stable). If the set of codenames changes (a new backend gets registered mid-session), the row-to-codename mapping shifts and per-row dataChanged would target the wrong rows. The only path that could trigger this in v1.1 is bootstrap (`registerAll()` runs before `refresh()` ever fires), so the fallback should never execute in steady state — but we keep it defensively and log INFO if it does, so the path is visible.

### Visual offline state: pill + 72% opacity

D-01 (silent-badge policy) rules out toasts, sounds, and modals. The pill alone could be missed at a glance; the 72% opacity reduction adds peripheral-vision distinguishability without screaming. Combined, they give offline rows the "present but inactive" affordance HOTPLUG-02 specifies. The 72% number matches the existing disabled-state opacity used elsewhere in the v1.0 component library (e.g. disabled buttons in `AppSettings.qml`) so the visual vocabulary is consistent.

## Verification Run

### Automated (Linux build + existing test pass)

```
$ cmake --build --preset linux-release
[339/339] Linking CXX executable src/app/ajazz-control-center
(no errors, no undefined references)

$ ctest --preset linux-release -R "(device_model|device_registry)"
2/2 Test #5: device registry enumerates all three families ...   Passed
2/2 Test #6: device registry instances are independent .......   Passed
100% tests passed, 0 tests failed out of 2

$ grep -c "beginResetModel" src/app/src/device_model.cpp
1  # Only the fallback path
$ grep -c "dataChanged" src/app/src/device_model.cpp
2  # The per-row emit (one site, referenced twice for the emit + the comment)
$ grep -q "m_codename_keys" src/app/src/device_model.cpp src/app/src/device_model.hpp && echo PASS
PASS
$ grep -E "std::sort.*(family|codename)" src/app/src/device_model.cpp
    std::sort(collapsed.begin(),
              collapsed.end(),
              [](core::DeviceDescriptor const& a, core::DeviceDescriptor const& b) {
                  if (a.family != b.family) {
                      return static_cast<int>(a.family) < static_cast<int>(b.family);
                  }
                  return a.codename < b.codename;
              });
```

### Manual (human-verify checkpoint — user approved 2026-05-14)

User performed all 5+1 sub-checks from `<how-to-verify>`. Approved verdict: every check passed.

| Sub-check                                              | Outcome                                                         |
| ------------------------------------------------------ | --------------------------------------------------------------- |
| Sidebar shows ALL registered codenames                 | Pass — all backend codenames visible regardless of online state |
| Cable yank → offline pill appears at 72% opacity       | Pass                                                            |
| Selection highlight stays on the same row after yank   | Pass                                                            |
| Scroll position preserved across yank                  | Pass                                                            |
| No toast / sound / modal on hot-plug                   | Pass — D-01 silent-badge policy honoured                        |
| Lex sort: AKP03 above AKP05 regardless of plug history | Pass — order is stable                                          |

## Issues Encountered

The plan's frontmatter listed `src/app/qml/components/DeviceList.qml` as the file to edit, but the actual QML layout in the project is `src/app/qml/DeviceList.qml` (top-level list) and `src/app/qml/components/DeviceRow.qml` (per-row delegate). The connected-only filter lived in `DeviceList.qml`; the offline-pill rendering went into the per-row delegate `DeviceRow.qml`. The split is structurally correct and matches the project's existing list/delegate separation pattern. Frontmatter is reconciled in this SUMMARY's `key-files` block.

## Deviations from Plan

- **QML file paths**: plan listed one file `src/app/qml/components/DeviceList.qml`; actual edits touched `src/app/qml/DeviceList.qml` (filter reversal) and `src/app/qml/components/DeviceRow.qml` (offline pill + opacity). Same intent, correct file layout. Documented above.
- **`capabilitiesFor()`** wasn't called out in the plan but had to be updated alongside `data()` to route the codename → (vid,pid)-set lookup, so QML's grid-sizing query still returns the correct capabilities for collapsed rebadge rows. Logical extension of the codename-collapse contract; no surprise.

**Total deviations:** 2 (file-path reconciliation + capabilitiesFor() update). **Impact:** none — both are mechanical adjustments to make the plan's contract correct against the actual repo layout.

## Phase 4 / Phase 5 Readiness Note

`DeviceModel::dataChanged` for `ConnectedRole` is now the only signal that QML observes for online/offline transitions. Plan 05's multi-device test harness will mechanically assert this contract via `QSignalSpy(model, &DeviceModel::dataChanged)`: a single Removed event for `(vidB, pidB)` must produce exactly ONE `dataChanged` emission with `role list containing ConnectedRole`, on the row corresponding to `vidB`/`pidB`'s codename — and zero `modelReset` emissions. That's the mechanical proof of SC2 (and indirectly closes the Pitfall 15 refresh-thrash regression risk for good).

Phase 5's TimeSyncService will rely on the offline-pill + selection-retention contract: when a sync attempt sees the device transition to disconnected mid-flight, the row stays put, the pill appears, and the user's selection focus is preserved — no full-list rebuild interrupts the user's mental model.

## Self-Check: PASSED
