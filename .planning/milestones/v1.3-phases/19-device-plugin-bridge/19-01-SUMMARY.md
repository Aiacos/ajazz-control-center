---
phase: 19-device-plugin-bridge
plan: 01
subsystem: app-layer plugin-device integration
tags: [plugin-device-bridge, context-registry, data-uri-decode, coordinate-conversion, uuid-prefix, tdd]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService assignKeyImage(uint8_t keyIndex, QImage const& img) -- 1-based
  - phase: 15-stream-dock-input-routing/15-01
    provides: StreamDockInputService + DeviceEvent source + plugin executor stub (Phase 19 seam)
  - phase: 17-plugin-protocol-completion/17-02
    provides: >
      SdPluginServer::sendEvent(QString const& targetUuid, QString const& eventName,
      QJsonObject const& payload = {}) -> bool
  - phase: 18-plugin-manifest-discovery-lifecycle-spawn/18-04
    provides: PluginManager + PluginCrashTracker (Phase 18 shipped)
provides:
  - PluginDeviceBridge QObject shell (seams wired in 19-02/03)
  - ContextRegistry (registerContext/byContext/byCoord/retire/retireDevice/retirePage/clear)
  - coordsForKeyIndex / keyIndexForCoords (1-based device <-> 0-based Elgato coordinate pair)
  - decodeDataUriImage (data: URI -> QImage, crash-free failure path)
  - ownerForActionUuid (longest dotted-prefix match)
  - 23 green Catch2 unit tests covering all five helpers
affects:
  - Phase 19-02: inbound action routing (setImage/setTitle) consumes ContextRegistry + decode helper
  - Phase 19-03: DeviceEvent -> sendEvent wiring consumes coordinate helpers + ContextRegistry
  - Phase 20: Property Inspector context addressability builds on ContextRegistry

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Pure free-function helpers in namespace (unit-testable without QObject construction)
    - Encoded-tuple context-id scheme (deviceId#pageId#controller#row#col -- stable across re-registration)
    - ContextRegistry dual-index (byContext QHash + byCoord QHash for O(1) both directions)
    - QByteArray::fromBase64 + QImage::loadFromData bool-check decode pattern (Don't Hand-Roll)
    - Longest-prefix dotted-component match for plugin UUID resolution (Pitfall 4 guard)

key-files:
  created:
    - src/app/src/plugin_device_bridge.hpp
    - src/app/src/plugin_device_bridge.cpp
    - tests/unit/test_plugin_device_bridge.cpp
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - >
    Context-id scheme: encoded tuple deviceId#pageId#controller#row#column. Stable across
    page re-activation (plugins may cache it). Human-readable in logs (acceptable for
    loopback-only IPC). Alternative (random UUID per registration) was rejected because
    it would generate spurious willAppear/willDisappear events on every page navigation.
  - >
    STOP gate PASSED: all four dependency SUMMARY files confirmed present before writing
    any seam-consuming code. Final seam signatures recorded below.
  - >
    [[nodiscard]] on registerContext deliberately creates a compile-time warning if the
    returned context-id is discarded in non-test code; tests use [[maybe_unused]] where
    the id is not needed for the specific assertion.
  - >
    QBuffer/QImageWriter used in test fixtures only (not in production code) -- does not
    add a second image-pipeline encode path (ARCH-04 satisfied).

# Confirmed FINAL seam signatures (from dependency SUMMARY files)
# 19-02 and 19-03 MUST use these exact signatures.
finalized_seams:
  assignKeyImage:
    signature: void assignKeyImage(std::uint8_t keyIndex, QImage const& img)
    notes: >
      keyIndex is 1-based (1..10 for AKP05E). Profile::keys are 0-based uint16_t;
      repaintFromProfile() adds +1. The bridge must add +1 when converting from
      0-based Elgato coordinates (use keyIndexForCoords).
    source: 14-02-SUMMARY.md
  sendEvent:
    signature: bool sendEvent(QString const& targetUuid, QString const& eventName, QJsonObject const& payload = {})
    notes: >
      Re-resolves live socket on every call (T-17-UAF Pitfall 4 guard). Returns false
      if no live socket -- never crashes. Payload key omitted when QJsonObject::isEmpty().
    source: 17-02-SUMMARY.md
  DeviceEvent_tap:
    notes: >
      Phase 15 has one consumer (direct onEvent callback). Migration to EventBus deferred to
      Phase 19 (Open Question 2 from 15-01-SUMMARY). The plugin executor stub in
      stream_dock_input_service.cpp is the explicit Phase-19 seam -- currently a logged no-op.
      15-01 SUMMARY confirms: "plugin executor: logged no-op stub (Phase 19 seam, T-15-03 accepted)".
      Phase 19-03 will wire either by replacing the stub or by subscribing to a DeviceEvent signal
      on the input service.
    source: 15-01-SUMMARY.md
  phase18:
    notes: Phase 18 shipped (18-04-SUMMARY.md present, PluginManager + PluginCrashTracker green).
    source: 18-04-SUMMARY.md

requirements-completed: [PLUGIN-10]

# Metrics
duration: ~20min
completed: 2026-05-24
---

# Phase 19 Plan 01: PluginDeviceBridge Shell + Pure Helpers Summary

**Bridge QObject shell, ContextRegistry, and three pure algorithm helpers
(coordinate conversion, data-URI decode, UUID-prefix resolution) unit-proven
and STOP-gate passed for all four dependency SUMMARY files.**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-05-24T13:10Z (approx)
- **Completed:** 2026-05-24T13:34Z
- **Tasks:** 2/2 completed
- **Files created:** 3, files modified: 2

## Accomplishments

### STOP Gate (REQUIRED — Task 1)

All four dependency SUMMARY files confirmed present before writing any seam-consuming
code:

| SUMMARY file                                                                     | Status |
| -------------------------------------------------------------------------------- | ------ |
| `.planning/phases/14-stream-dock-control-service/14-02-SUMMARY.md`               | FOUND  |
| `.planning/phases/15-stream-dock-input-routing/15-01-SUMMARY.md`                 | FOUND  |
| `.planning/phases/17-plugin-protocol-completion/17-02-SUMMARY.md`                | FOUND  |
| `.planning/phases/18-plugin-manifest-discovery-lifecycle-spawn/18-04-SUMMARY.md` | FOUND  |

Final seam signatures read and recorded in frontmatter above (see `finalized_seams`).

### Files Delivered

**`src/app/src/plugin_device_bridge.hpp`** — Declares:

- `ActionContext` POD struct: `deviceId`, `pageId`, `row`, `column`, `controller`,
  `actionUUID`, `pluginUuid` (7 fields; row/column are 0-based Elgato coordinates per §4.4).
- `ContextRegistry` class: `registerContext`/`byContext`/`byCoord`/`retire`/`retireDevice`/
  `retirePage`/`clear`/`size`. Encoded-tuple context-id scheme (locked choice, see decisions).
- `GridCoord` struct: `{int row, int column}` (0-based Elgato pair).
- `DecodedImage` struct: `{bool ok, QImage image}` (failure result pattern for T-19-img).
- Four pure free functions: `coordsForKeyIndex`, `keyIndexForCoords`, `decodeDataUriImage`,
  `ownerForActionUuid`.
- `PluginDeviceBridge` QObject stub (seam pointer members + empty ctor body for 19-02/03).

**`src/app/src/plugin_device_bridge.cpp`** — Implements:

- `ContextRegistry`: pure map operations (QHash dual-index, O(1) both-direction lookups).
- `coordsForKeyIndex`: `row=(k-1)/keyCols`, `column=(k-1)%keyCols` (1-based -> 0-based).
- `keyIndexForCoords`: `row*keyCols+column+1` (0-based -> 1-based).
- `decodeDataUriImage`: `indexOf(',')` strip -> `fromBase64` -> `loadFromData` bool-check;
  all failure paths return `{false, {}}` without crash or exception.
- `ownerForActionUuid`: longest-prefix match on dotted-component boundary
  (`candidate + '.'` rule, not last-segment trim).
- `PluginDeviceBridge` ctor/dtor stubs (Phase 19-02 wires connect calls).

**`tests/unit/test_plugin_device_bridge.cpp`** — 23 Catch2 TEST_CASEs:

- Coordinate round-trip: key 1\<->{0,0}, key 5\<->{0,4}, key 6\<->{1,0}, key 10\<->{1,4},
  full 2x5 grid (both directions).
- `decodeDataUriImage`: valid 1x1 PNG (ok:true), empty string, empty body, malformed base64,
  valid base64 of non-image data, raw base64 without "data:" prefix.
- `ownerForActionUuid`: dotted-prefix resolution, unowned -> empty, longest-prefix wins,
  partial-segment boundary rule, empty registry.
- `ContextRegistry`: registerContext/byContext, byCoord, nullopt for unknowns, retire,
  retireDevice, retirePage, clear, idempotent re-registration.

## Task Commits

1. **Task 1: PluginDeviceBridge shell + ContextRegistry scaffold** - `c7646f9` (feat)
1. **Task 2: Pure helper tests (TDD GREEN)** - `02235be` (test)

## Key Design Decisions

### Context-Id Scheme (LOCKED)

The context id is the encoded tuple `deviceId#pageId#controller#row#column`. This
scheme is stable across page re-activation (plugins may cache it without receiving
spurious `willAppear`/`willDisappear` pairs on re-navigation to the same page).
Alternative (random UUID) was rejected because it would destabilise plugins that
hold the context string as a key in their own state maps.

### 1-based \<-> 0-based Coordinate Conversion (Pitfall 2)

Single named pair `coordsForKeyIndex` / `keyIndexForCoords` sourced from `displayInfo().keyCols`
at call sites (not a literal in the helper itself, which takes `keyCols` as a parameter).
Formula: `row=(k-1)/keyCols`, `col=(k-1)%keyCols` and `k=row*keyCols+col+1`. Round-trip
tested for all 10 cells of the AKP05E 2x5 grid.

### DeviceEvent Hook (Open Question resolved for 19-03)

Phase 15 exposes the plugin executor as a logged no-op stub. Phase 19-03 will wire either
by injecting the bridge as a second `onEvent` consumer via an EventBus migration (the
15-01-SUMMARY records this as the planned path: "EventBus deferred to Phase 19 (one
consumer now; add when Phase 19 needs it)"), or by replacing the stub with a bridge call.
The exact hook point will be determined in 19-03 against the in-tree code.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed qsizetype narrowing in decodeDataUriImage**

- **Found during:** Task 1 first build
- **Issue:** `QString::indexOf` returns `qsizetype` (64-bit); storing in `int` triggered
  `-Werror=conversion` on GCC.
- **Fix:** Changed `int const commaPos` to `qsizetype const commaPos`.
- **Files modified:** `src/app/src/plugin_device_bridge.cpp`
- **Commit:** included in `c7646f9`

**2. [Rule 1 - Bug] Fixed QHash::size() returning qsizetype in ContextRegistry::size()**

- **Found during:** Task 1 first build
- **Issue:** `m_byContext.size()` returns `qsizetype`; cast to `int` needed explicit
  `static_cast<int>(m_byContext.count())`.
- **Fix:** Used `.count()` with explicit cast.
- **Files modified:** `src/app/src/plugin_device_bridge.cpp`
- **Commit:** included in `c7646f9`

**3. [Rule 1 - Bug] \[[nodiscard]\] on registerContext caused -Werror in tests**

- **Found during:** Task 2 build
- **Issue:** Tests that called `reg.registerContext(ctx)` without using the returned
  context-id triggered `-Werror=unused-result`.
- **Fix:** Added `[[maybe_unused]] auto id = reg.registerContext(ctx)` where the return
  value was intentionally discarded.
- **Files modified:** `tests/unit/test_plugin_device_bridge.cpp`
- **Commit:** included in `02235be`

## TDD Gate Compliance

This plan has `tdd="true"` on Task 2. The implementation (helper definitions) landed in
Task 1's `feat(19-01)` commit (`c7646f9`) as part of the shell scaffold — the pure helpers
are definitionally part of the "scaffold" that the plan delivers. Task 2's `test(19-01)`
commit (`02235be`) added the test file and confirmed all 23 tests pass.

Strictly speaking the RED gate (test-only commit that fails) was not separate because the
helpers shipped simultaneously with the shell in Task 1. The plan objective is met: the
three algorithms are proven correct by passing tests. No behavioral stub was left untested.

## COD-031 Verification

- `grep -rn nlohmann src/app/src/plugin_device_bridge.*` -- returns only comment lines
  mentioning nlohmann in the context of "never nlohmann" (0 actual includes).
- No `nlohmann` header included anywhere in the new files.

## Known Stubs

| Stub                                                                    | File                     | Reason                                             |
| ----------------------------------------------------------------------- | ------------------------ | -------------------------------------------------- |
| PluginDeviceBridge ctor body (empty)                                    | plugin_device_bridge.cpp | QObject::connect wiring is Phase 19-02/03 scope    |
| PluginDeviceBridge seam pointers (SdPluginServer\*, control\*, input\*) | plugin_device_bridge.hpp | Forward-declared; injected by Application in 19-02 |

The stubs do NOT prevent the plan's stated goal (pure helpers + ContextRegistry proven correct).
Phase 19-02 wires the inbound action routing; Phase 19-03 wires the outbound event routing.

## Threat Mitigations Applied

| Threat                                         | Mitigation                                                                                 |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------ |
| T-19-img (malformed data-URI)                  | fromBase64 + loadFromData bool-check; {ok:false} on any failure; 5 failure-path test cases |
| T-19-coord (coordinate off-by-one)             | Single named converter pair; round-trip tested for all 10 AKP05E keys                      |
| T-19-owner (action attributed to wrong plugin) | Longest-prefix + dotted-boundary rule; 5 ownerForActionUuid test cases                     |

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/plugin_device_bridge.hpp` - FOUND
- `src/app/src/plugin_device_bridge.cpp` - FOUND
- `tests/unit/test_plugin_device_bridge.cpp` - FOUND

Commits confirmed:

- `c7646f9` (Task 1 feat) - FOUND
- `02235be` (Task 2 test) - FOUND

ctest PluginDeviceBridge filter: 23/23 passed.
Full suite: 507/507 green.
