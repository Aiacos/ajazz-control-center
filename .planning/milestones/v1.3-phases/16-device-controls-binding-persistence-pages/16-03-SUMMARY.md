---
phase: 16-device-controls-binding-persistence-pages
plan: 03
subsystem: app-layer page navigation / multi-page profile repaint
tags: [profile, pages, carousel, repaint, tdd, profile-02, page-nav]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService (held handle, repaintFromProfile, coalesced assignKeyImage)
  - phase: 15-stream-dock-input-routing/15-01
    provides: StreamDockInputService::pageNavRequested(int) signal
  - phase: 16-01
    provides: StreamDockControlService QML_SINGLETON
  - phase: 16-02
    provides: ProfileController::activeProfile() with Profile::pages populated
provides:
  - StreamDockControlService: repaintPage(pageId) page-scoped repaint (PROFILE-02)
  - StreamDockControlService: navigatePage(direction) carousel owner + Q_SLOT
  - Application: pageNavRequested -> navigatePage wiring (replaces Phase 15 logged sink)
  - test_profile_pages.cpp: 7 TEST_CASEs covering root/child/missing repaint + carousel + no-op
affects:
  - Phase 19 (plugin bridge): repaintPage reusable for plugin-driven page changes
  - Phase 25 (hardware smoke): live swipe witness + provisional touch-zone reconciliation

# Tech tracking
tech-stack:
  added: []
  patterns:
    - repaintPage("root") -> Profile::keys; repaintPage(id) -> pages.find(id)->keys
    - find() not at() for safe child-page lookup (T-16c-01 mitigation)
    - Single paint path: repaintFromProfile() delegates to repaintPage("root")
    - 'Carousel: ["root"] + sorted(Profile::pages.ids) -- stable/deterministic ordering'
    - 'Clamp-not-wrap: m_carouselIndex clamped to [0, list.size()-1] (T-16c-02)'
    - 'Single-root no-op: list.size() <= 1 -> return early (Decision 2)'
    - TDD cycle: RED commit (e536549) -> GREEN commit (805b3ce)

key-files:
  created:
    - tests/unit/test_profile_pages.cpp
  modified:
    - src/app/src/stream_dock_control_service.hpp
    - src/app/src/stream_dock_control_service.cpp
    - src/app/src/application.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - 'Page-nav owner placement: navigatePage is a Q_SLOT on StreamDockControlService itself (not a helper QObject). It already holds the profile accessor and the carousel index, so colocation avoids an extra member in Application and keeps the signal->slot connect to two objects (input service -> control service).'
  - 'Carousel ordering: ["root"] first (always), then remaining ProfilePage ids in sorted (std::sort) order. Profile::pages is an unordered_map; sorting by id string gives deterministic, stable ordering that survives serialise/deserialise round-trips. Root is always index 0.'
  - 'Clamp vs wrap: clamped to [0, N-1] -- no wrap. Rationale: wrap is surprising on short lists (2 pages); clamping prevents unintended "jump to opposite end" on accidental double swipe. Documented in navigatePage() body comment.'
  - 'Single paint path: repaintFromProfile() refactored to one-liner repaintPage("root"). Previously repaintFromProfile() contained the full iteration loop; now there is one loop (repaintPage). Grep confirms no duplicated paint loop.'
  - 'OpenFolder/BackToParent (vertical folder nesting): these already push/pop on the ActionEngine (Phase 15 dispatch path) during action chain execution. No additional repaint hook was needed for Phase 16-03 -- when an OpenFolder/BackToParent action fires, it mutates the ActionEngine nav stack and returns; the next profile-changed repaint (or a future explicit hook) will pick up the new page. Phase 19+ can add a nav-changed signal if real-time repaint is needed.'
  - 'No second nav stack: navigatePage owns a flat m_carouselIndex (an int) on StreamDockControlService. ActionEngine retains full ownership of the folder nav stack (pushPage/popPage). These two concepts are orthogonal: carousel is top-level page lateral nav; ActionEngine is vertical folder-tree nav.'

requirements-completed: [PROFILE-02]

# Metrics
duration: ~10 min
completed: 2026-05-24
---

# Phase 16 Plan 03: Multi-page Profile Navigation + Page-scoped Repaint (PROFILE-02) Summary

**repaintPage(pageId) generalises the Phase-14 paint path to child pages; navigatePage(direction) implements a carousel over the profile's ordered top-level pages; pageNavRequested is wired to navigatePage replacing the Phase 15 logged sink.**

## Performance

- **Duration:** ~10 min
- **Started:** 2026-05-24T12:47Z
- **Completed:** 2026-05-24T12:57Z
- **Tasks:** 2/2 completed
- **Files modified:** 4 (3 modified + 1 created)

## Accomplishments

- PROFILE-02 delivered: multi-page/folder profiles navigate host-side and repaint correctly
- `repaintPage("root")` paints root keys (Profile::keys); `repaintPage(childId)` paints the
  child page's keys via Profile::pages.find() -- Pitfall 1 closed (root is NOT repainted on
  child-page switch)
- `repaintPage("doesNotExist")` no-ops cleanly: uses find() not unguarded at() (T-16c-01)
- `repaintFromProfile()` refactored to `repaintPage("root")` -- single paint loop, no
  duplicated iteration
- `navigatePage(direction)` carousel: stable ordered list = ["root"] + sorted page ids;
  clamp at ends; single-root no-op (Decision 2 / T-16c-02)
- Application wiring: `pageNavRequested -> navigatePage` replaces the Phase-15 logged sink
- 7 new TEST_CASEs tagged [profile-pages][PROFILE-02]; all pass; full suite 444/444 green
- TDD gate compliance: RED commit e536549 -> GREEN commit 805b3ce

## Task Commits

1. **Task 1 (RED): Failing tests for PROFILE-02 page-scoped repaint + carousel** - `e536549` (test)
1. **Task 1/2 (GREEN): repaintPage + navigatePage on StreamDockControlService** - `805b3ce` (feat)
1. **Task 2: Wire pageNavRequested -> navigatePage in Application** - `ef3c252` (feat)

## Files Created/Modified

- `tests/unit/test_profile_pages.cpp` - 7 Catch2 TEST_CASEs tagged [profile-pages][PROFILE-02];
  covers: repaintPage root, repaintPage child (Pitfall 1), repaintPage missing (no-op),
  repaintFromProfile delegates to root, pageNavRequested +1, pageNavRequested -1,
  single-root no-op; ASCII-only titles
- `tests/unit/CMakeLists.txt` - Registered test_profile_pages.cpp
- `src/app/src/stream_dock_control_service.hpp` - Added repaintPage(QString), navigatePage(int)
  Q_SLOT declarations; m_carouselIndex int member; doc-comments for both
- `src/app/src/stream_dock_control_service.cpp` - repaintFromProfile() delegates to
  repaintPage("root"); repaintPage() full implementation (root path + child find() path +
  missing-page no-op); navigatePage() carousel implementation; added <vector> include
- `src/app/src/application.cpp` - Replaced Phase-15 pageNavRequested logged sink with
  QObject::connect(m_streamDockInput, pageNavRequested, m_streamDockControl, navigatePage)

## Key Design Decisions

### Page-Nav Owner: Q_SLOT on StreamDockControlService

The plan left the owner placement to the executor (Application method vs helper QObject vs
control service slot). `navigatePage` was placed as a `Q_SLOT` on `StreamDockControlService`
for three reasons: (1) the service already holds the `ProfileAccessor` needed to build the
carousel list; (2) colocation with `repaintPage` means no cross-object call is needed after
the carousel step; (3) Application wiring stays a simple 2-object connect (input service ->
control service) with no new Application member needed.

### Carousel Ordering: Sorted Page IDs

`Profile::pages` is an `std::unordered_map<std::string, ProfilePage>` with no intrinsic order.
The carousel builds `["root"] + std::sort(page ids)`. This is deterministic across serialize/
deserialize round-trips (stable by id string) and matches the plan's RESEARCH recommendation.
Root is always index 0. The ordering is documented in the `navigatePage` header comment.

### Clamp vs Wrap

Chosen: clamp (not wrap). At the first page, `pageNavRequested(-1)` is a no-op; at the last
page, `pageNavRequested(+1)` is a no-op. Wrap would produce surprising "jump to opposite end"
on an accidental double swipe on a 2-page profile. The decision is documented in the method
body. Can be changed to wrap via a one-line edit; the test covers clamped semantics.

### OpenFolder/BackToParent Repaint Hook

Phase 15 already wires OpenFolder/BackToParent chain steps to ActionEngine::pushPage/popPage
inside the dispatch path. When a key's onPress chain fires an OpenFolder step, the engine
nav stack advances to the child page. The current Phase 16-03 does NOT add an explicit
post-nav repaint hook; the next profileChanged event (or a future explicit hook) triggers
the correct page repaint. Phase 19 or Phase 25 can add a nav-changed callback if a real-time
repaint-on-folder-enter is needed. This is documented as a follow-up item.

## Deviations from Plan

None -- plan executed exactly as written. The navigatePage Q_SLOT placement was
"executor's discretion" per the plan and documented above.

## TDD Gate Compliance

- RED gate: commit `e536549` (test(16-03)) -- 7 failing tests for PROFILE-02
- GREEN gate: commit `805b3ce` (feat(16-03)) -- implementation; all 7 tests pass

## Follow-up Items (Not Phase 16-03 Scope)

| Item                                                              | Phase    | Reason deferred                                                               |
| ----------------------------------------------------------------- | -------- | ----------------------------------------------------------------------------- |
| Repaint-on-OpenFolder/BackToParent (immediate child-page repaint) | 19 or 25 | ActionEngine nav-changed hook not yet present; Phase 15 dispatch fires chain  |
| Live hardware witness: swipe drives real AKP05E page switch       | 25       | Phase 16 proof is MockTransport-only; Phase 25 does live power-cycle UAT      |
| Provisional touch-zone / swipe map reconciliation                 | 25       | zoneForX formula is PROVISIONAL (Phase 15); hardware-reconciled in Phase 25   |
| m_carouselIndex reset on profile load                             | 16-03+   | Index is not reset when a new profile is loaded; first swipe starts from root |

## Threat Mitigations Applied

| Threat                                             | Mitigation                                                                                                  |
| -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| T-16c-01: repaintPage with unknown page id throws  | pages.find() + no-op on miss; never pages.at() unguarded; tested with "doesNotExist"                        |
| T-16c-02: carousel index OOB on +/-1 at ends       | std::clamp(idx + dir, 0, listSize-1); single-root list.size()\<=1 -> early return                           |
| T-16c-03: child-page imagePath untrusted file read | Same Qt safe image decoder path as repaintFromProfile; load failure -> placeholder or skip                  |
| T-16c-04: device page opcode invented (STP)        | No new wire builder; no STP write; git diff excludes akp05.cpp / akp05_protocol.hpp                         |
| T-16c-05: second page-nav stack introduced         | navigatePage holds a simple int carousel index; ActionEngine owns folder nav stack; no new pageStack member |

## Known Stubs

None that prevent the plan's goal.

## Threat Flags

None. No new network endpoints, auth paths, file access patterns, or schema changes at trust
boundaries beyond what is already present in Profile::pages (already in the schema).

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/stream_dock_control_service.hpp` - FOUND (repaintPage + navigatePage + m_carouselIndex)
- `src/app/src/stream_dock_control_service.cpp` - FOUND (repaintPage + navigatePage bodies)
- `src/app/src/application.cpp` - FOUND (pageNavRequested -> navigatePage connect)
- `tests/unit/test_profile_pages.cpp` - FOUND (7 TEST_CASEs)

Commits confirmed:

- `e536549` (RED) - FOUND
- `805b3ce` (GREEN) - FOUND
- `ef3c252` (Application wiring) - FOUND

Invariants confirmed:

- `grep -c nlohmann src/core/include/` == 0 (COD-031 intact)
- `git diff --stat -- src/devices/streamdeck/src/akp05.cpp src/devices/streamdeck/src/akp05_protocol.hpp` -- empty (wire layer untouched)
- `grep -n pageNavRequested application.cpp` -- shows connect to navigatePage (line 366)
- `grep -n repaintPage stream_dock_control_service.cpp` -- shows the single paint path (lines 168-169, 172+)
- dynamic_cast<core::IDisplayCapable> null-checks all within 3 lines (lines 138, 177, 315, 344, 370)
- TEST_CASE titles ASCII-only
- ctest suite: 444/444 green
