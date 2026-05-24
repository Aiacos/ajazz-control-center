---
phase: 19-device-plugin-bridge
plan: 02
subsystem: plugins
tags: [plugin-bridge, stream-dock, websocket, context-registry, control-service, image-decode, e2e-tests]

# Dependency graph
requires:
  - phase: 19-01-device-plugin-bridge
    provides: PluginDeviceBridge shell, ContextRegistry, ActionContext, decodeDataUriImage, keyIndexForCoords
  - phase: 17-sd-plugin-server
    provides: SdPluginServer::actionReceived(pluginUuid, action) signal
  - phase: 14-stream-dock-control-service
    provides: StreamDockControlService::assignKeyImage(keyIndex, QImage const&) — ARCH-04 sole encode path
provides:
  - PluginDeviceBridge::onAction — inbound router dispatching visual-family events to control service
  - onSetImage — data-URI decode → assignKeyImage(1-based keyIndex); placeholder on malformed URI
  - onSetTitle / onSetBG — solid-fill QImage fallback (text rendering deferred Phase 23)
  - Cross-plugin ownership enforcement (T-19-xplugin): ctx.pluginUuid != sending UUID → no-op
  - Stale/unknown context guard (T-19-stale): no paint, no crash
  - SdPluginServer + PluginDeviceBridge constructed in Application::startBackgroundServices
  - 4 e2e tests: setImage BAT+ULEND burst, malformed-URI placeholder, cross-plugin denial, visual-family-no-crash
affects:
  - 19-03-device-plugin-bridge (willAppear, outbound events, live device keyCols sourcing)
  - 23-text-rendering (setTitle/setFeedback/setText QPainter fills deferred here)
  - Phase 14 callers (assignKeyImage is now exercised from a real plugin action path)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Security gate before paint: resolve ctx, check ctx.pluginUuid == sendingPlugin, then paint — fail-open no-op'
    - 'Placeholder-over-error: malformed decode always emits placeholder paint, never null image to hardware'
    - 'ARCH-04 encode path: all QImage→hardware encoding happens inside control service, not in bridge'
    - 'AJAZZ_HAVE_WEBSOCKETS guard: all WebSocket-dependent bridge and test code conditioned on macro'

key-files:
  created: []
  modified:
    - src/app/src/plugin_device_bridge.hpp
    - src/app/src/plugin_device_bridge.cpp
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - tests/unit/test_plugin_device_bridge.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - onSetTitle/onSetBG use solid QColor fill (no QPainter) — QPainter in QCoreApplication test env = SIGABRT; text rendering deferred to Phase 23
  - kDefaultKeyCols=5 hardcoded (AKP05E canonical) — StreamDockControlService does not expose displayInfo(); Phase 23+ will source from DeviceRegistry
  - actionReceived→onAction connect lives in PluginDeviceBridge ctor (null-guarded), not Application — keeps Application construction order simple
  - SdPluginServer started on port 0 (ephemeral) in startBackgroundServices with WARN if bind fails — non-fatal degradation
  - setFeedback/setText/setState log 'deferred to Phase 23' and return — no paint, no crash
  - Placeholder is 85×85 neutral-grey QImage via assignKeyImage — goes through same ARCH-04 JPEG encode path as real images

patterns-established:
  - 'Plugin action routing: event string dispatch → context resolve → ownership check → key index → assignKeyImage'
  - 'Test E2eFixture pattern: real SdPluginServer + StreamDockControlService + MockTransport, loopback WebSocket client'
  - 'findBatWrite/findUlendWrite spy helpers: scan MockTransport::writes for BAT/ULEND marker bytes to verify paint'

requirements-completed: [PLUGIN-10]

# Metrics
duration: 95min
completed: 2026-05-24
---

# Phase 19 Plan 02: PluginDeviceBridge Inbound Action Router Summary

**Inbound Elgato setImage/setTitle/setBG actions are routed from SdPluginServer through PluginDeviceBridge — with ownership enforcement and placeholder-on-error — to StreamDockControlService::assignKeyImage, verified by 4 loopback e2e tests.**

## Performance

- **Duration:** ~95 min
- **Started:** 2026-05-24T12:16:00Z
- **Completed:** 2026-05-24T13:51:18Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Implemented `PluginDeviceBridge::onAction` dispatching visual-family events (setImage, setTitle, setBG, setFeedback, setText, setState) with security ownership enforcement and stale-context guard
- Implemented `onSetImage` with data-URI decode → `assignKeyImage(1-based keyIndex)` and `paintPlaceholder` fallback on malformed URI (T-19-img: no failure event back to plugin, spec §5)
- Wired `SdPluginServer` + `PluginDeviceBridge` into `Application` — server starts on ephemeral port in `startBackgroundServices`, bridge ctor connects `actionReceived → onAction`
- Added 4 loopback e2e tests (`E2eFixture` with real server + real control service + MockTransport spy): setImage BAT+ULEND, malformed-URI placeholder, cross-plugin denial, visual-family no-crash

## Task Commits

Each task was committed atomically:

1. **Task 1: PluginDeviceBridge inbound action router + onSetImage** - `4f6a212` (feat)
1. **Task 2: Wire PluginDeviceBridge + SdPluginServer into Application; add e2e tests** - `35fd67b` (feat)

**Plan metadata:** (this commit) (docs: complete plan)

## Files Created/Modified

- `src/app/src/plugin_device_bridge.hpp` - Added `onAction` slot, private `onSetImage`/`onSetTitle`/`onSetBG`/`paintPlaceholder` declarations
- `src/app/src/plugin_device_bridge.cpp` - Full inbound routing implementation; ctor wires `actionReceived→onAction`; visual-family dispatch with T-19-xplugin ownership check and T-19-stale guard
- `src/app/src/application.hpp` - Added `m_pluginServer` and `m_pluginBridge` members under `AJAZZ_HAVE_WEBSOCKETS`
- `src/app/src/application.cpp` - Construct bridge after server+control+input; start server in `startBackgroundServices`
- `tests/unit/test_plugin_device_bridge.cpp` - 4 new `[E2E]` test cases under `AJAZZ_HAVE_WEBSOCKETS`; `E2eFixture`, `makeE2eFixture`, BAT/ULEND spy helpers
- `tests/unit/CMakeLists.txt` - Updated Phase 19 block comment; `AJAZZ_HAVE_WEBSOCKETS=1` made explicit in 19-01/02 conditional block

## Decisions Made

1. **No QPainter in onSetTitle/onSetBG (Phase 23 deferral):** `QPainter::drawText` in a `QCoreApplication` context (no display backend) caused `SIGABRT` in Test #502. Replaced with solid `QColor` fill. Text rendering deferred to Phase 23 per plan spec.
1. **kDefaultKeyCols=5 hardcoded:** `StreamDockControlService` does not expose `displayInfo()`. AKP05E 2×5 canonical value used as constant. Phase 23+ will source from DeviceRegistry at `willAppear` time.
1. **Bridge ctor owns the connect:** `actionReceived→onAction` connect lives in `PluginDeviceBridge` constructor (null-guarded on `m_server`), not in Application. Application just constructs — cleaner responsibility split.
1. **Ephemeral port + non-fatal start:** `m_pluginServer->start(0)` — OS assigns port. Bind failure logs WARN but does not crash the app — plugin functionality degrades gracefully.
1. **Placeholder via assignKeyImage:** `StreamDockControlService` has no `setKeyColor` public API. Placeholder is 85×85 neutral-grey QImage passed to `assignKeyImage`, going through the same ARCH-04 JPEG encode path as real plugin images.
1. **setFeedback/setText/setState log + return:** These Elgato aux-surface events are correctly recognised but deferred. `AJAZZ_LOG_INFO` "deferred to Phase 23" ensures they are traceable without crashing.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Removed QPainter from onSetTitle to fix SIGABRT in test**

- **Found during:** Task 1 (bridge implementation)
- **Issue:** `QPainter::drawText` on a QImage in `QCoreApplication` (no display backend) caused `SIGABRT` when Test #502 ran the visual-family no-crash case
- **Fix:** Replaced with solid `QColor(30, 30, 30)` fill; removed `#include <QPainter>` and `#include <QFont>`. Text rendering deferred to Phase 23 per plan
- **Files modified:** `src/app/src/plugin_device_bridge.cpp`
- **Verification:** Test #502 passes; all 511 tests pass
- **Committed in:** `4f6a212` (Task 1 commit)

**2. [Rule 1 - Bug] Removed unused writeCountBeforeFeedback variable**

- **Found during:** Task 2 (e2e test authoring)
- **Issue:** `-Werror=unused-variable` — variable declared but never read in visual-family test
- **Fix:** Removed the declaration
- **Files modified:** `tests/unit/test_plugin_device_bridge.cpp`
- **Verification:** Build clean; no unused-variable warning
- **Committed in:** `35fd67b` (Task 2 commit)

______________________________________________________________________

**Total deviations:** 2 auto-fixed (1 QPainter SIGABRT, 1 unused-variable)
**Impact on plan:** Both fixes necessary for correctness and build cleanliness. No scope creep; Phase 23 deferral for text rendering was already in the plan spec.

## Issues Encountered

- **clang-format pre-commit triggered twice:** Both task commits required re-staging after clang-format modified files. Standard pattern, no `--no-verify` used.
- **cmake-format pre-commit triggered on Task 2:** `CMakeLists.txt` required re-staging after cmake-format. Standard pattern.

## Security / ARCH Compliance

- **ARCH-04 verified:** `grep 'encodeForDevice\|QImageWriter\|\.save(' plugin_device_bridge.cpp` — 2 occurrences, both in comment text only. Bridge never encodes; all JPEG encoding flows through `StreamDockControlService::assignKeyImage`.
- **COD-031 verified:** `grep 'nlohmann' plugin_device_bridge.{cpp,hpp}` — 3 occurrences, all in "never nlohmann" comment text. No nlohmann at app layer.
- **T-19-xplugin enforced:** `ctx.pluginUuid != pluginUuid` check before any paint — cross-plugin canvas isolation.
- **T-19-stale enforced:** Unknown context → `std::nullopt` from `ContextRegistry::resolve` → return with no paint, no crash.
- **T-19-img enforced:** Malformed data-URI → `paintPlaceholder` → assigns neutral grey; no failure event back to plugin (spec §5).

## Next Phase Readiness

- Phase 19-03 (willAppear, context registration at connection time, outbound plugin events) can build directly on the established `onAction` routing surface
- Phase 23 (text rendering) has a clear integration point: replace solid-fill stub in `onSetTitle` with QPainter text path (needs QGuiApplication or offscreen surface)
- `kDefaultKeyCols=5` hardcode is the only known limitation — Phase 23 DeviceRegistry integration will resolve it

______________________________________________________________________

*Phase: 19-device-plugin-bridge*
*Completed: 2026-05-24*
