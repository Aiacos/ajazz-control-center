---
phase: 19-device-plugin-bridge
fixed_at: 2026-05-24T14:35:04Z
review_path: .planning/phases/19-device-plugin-bridge/19-REVIEW.md
iteration: 1
findings_in_scope: 8
fixed: 8
skipped: 0
status: all_fixed
---

# Phase 19: Code Review Fix Report

**Fixed at:** 2026-05-24T14:35:04Z
**Source review:** `.planning/phases/19-device-plugin-bridge/19-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 8
- Fixed: 8
- Skipped: 0

**Build gate:** `cmake --build build/linux-release` — PASSED. `Linking CXX executable src/app/ajazz-control-center` confirmed.
**Test gate:** `ctest --preset linux-release` — 519/519 PASSED (was 516 before; +3 new security/isolation tests).

______________________________________________________________________

## Fixed Issues

### IN-01: Unused includes QBuffer and QImageReader in plugin_device_bridge.cpp

**Files modified:** `src/app/src/plugin_device_bridge.cpp`
**Commit:** `96898fb`
**Applied fix:** Removed `#include <QBuffer>` and `#include <QImageReader>` from `plugin_device_bridge.cpp`. Both were unused dead includes; `QBuffer` is only used in the test fixture (separate TU).

______________________________________________________________________

### IN-02: Misleading comment in decodeDataUriImage about fromBase64 behavior

**Files modified:** `src/app/src/plugin_device_bridge.cpp`
**Commit:** `ae51e5a`
**Applied fix:** Replaced the inaccurate comment "fromBase64 never throws; an empty/garbage input yields empty bytes" with a correct explanation: Qt's default `IgnoreBase64DecodingErrors` silently ignores non-base64 characters rather than returning empty bytes; the actual rejection gate for garbage payloads is `loadFromData`. This prevents a future maintainer from removing the essential `loadFromData` check as "redundant".

______________________________________________________________________

### CR-01: No upper-bound on plugin-supplied image payload before decode

**Files modified:** `src/app/src/plugin_device_bridge.cpp`, `tests/unit/test_plugin_device_bridge.cpp`
**Commit:** `1dde6b4`
**Applied fix:** Added two size guards in `decodeDataUriImage` (T-19-img / ARCH-04):

1. `kMaxBase64Bytes = 512 KB`: rejects the base64 body string before `fromBase64` allocates. 512 KB encodes at most ~384 KB raw, comfortably above any real key icon (85x85 RGBA = 28,900 bytes).
1. `kMaxRawBytes = 384 KB`: rejects the decoded byte array before `QImage::loadFromData` can allocate a pixel buffer. Prevents an adversarially-compressed image (e.g. 16384x16384 RGBA = 1 GB decoded) from OOM-crashing the host.

Both guards return `{ok:false}` per spec ss5 (no failure event back, no crash).

Added test: `"PluginDeviceBridge decodeDataUriImage rejects oversize base64 body T-19-img"` — asserts `ok==false` and null image for a 600 KB body that exceeds the 512 KB cap.

______________________________________________________________________

### CR-02: ContextRegistry::coordKey omits deviceId — multi-device coord collision

**Files modified:** `src/app/src/plugin_device_bridge.hpp`, `src/app/src/plugin_device_bridge.cpp`, `tests/unit/test_plugin_device_bridge.cpp`
**Commit:** `aca1375`
**Applied fix (combined with WR-04):** `ContextRegistry::coordKey` now includes `deviceId` as its first component: `deviceId+'#'+controller+'#'+row+'#'+col`. This fixes both CR-02 (collision) and WR-04 (routing):

- Two simultaneously-connected devices with the same `controller/row/col` no longer collide in `m_byCoord`.
- `registerContext` / `retire` / `byCoord` all updated to pass `deviceId`.
- `byCoord` public API gained a `deviceId` parameter (4-param signature).
- `onDeviceEvent` now passes `deviceId` to every `byCoord` call; `Q_UNUSED(deviceId)` removed.
- Updated `retireDevice` test to assert correct isolation semantics (the old test acknowledged the bug with a comment).

Added tests:

- `"PluginDeviceBridge ContextRegistry CR-02 two devices same coord do not collide"` — unit test asserting independent retire and byCoord reachability for two devices at the same coord.
- `"PluginDeviceBridgeE2E CR-02 two-device isolation keyDown routes to correct plugin"` — e2e test asserting that a KeyPressed from `akp05e` reaches only `com.plugA` and a KeyPressed from `akp153` reaches only `com.plugB`, even when both are registered at the same grid coordinate.

______________________________________________________________________

### WR-04: onDeviceEvent ignores the deviceId parameter for all routing lookups

**Files modified:** `src/app/src/plugin_device_bridge.hpp`, `src/app/src/plugin_device_bridge.cpp`, `tests/unit/test_plugin_device_bridge.cpp`
**Commit:** `aca1375` (combined with CR-02)
**Applied fix:** See CR-02 above. `Q_UNUSED(deviceId)` annotation removed from `onDeviceEvent`; all five `byCoord` calls in the switch now pass `deviceId` as the first argument.

______________________________________________________________________

### WR-01: onDeviceDisconnected skips willDisappear for individual contexts

**Files modified:** `src/app/src/plugin_device_bridge.cpp`
**Commit:** `1ac2037`
**Applied fix:** `onDeviceDisconnected` now calls `retirePageContexts(deviceId, "root", {})` before `retireDevice(deviceId)`. This sends `willDisappear` for every visible action context on the disconnecting device, matching the Elgato SDK spec ss4.4 requirement and the behavior of `onPluginDisconnected`. `retireDevice()` is retained after `retirePageContexts()` as a safety net for any non-root-page contexts (Phase 16 multi-page scope). The `deviceDidDisconnect` broadcast is unchanged and still follows the retire step.

______________________________________________________________________

### WR-03: onPluginRegistered and onPluginDisconnected hardcode deviceId and pageId

**Files modified:** `src/app/src/plugin_device_bridge.hpp`, `src/app/src/plugin_device_bridge.cpp`
**Commit:** `d1d28c0`
**Applied fix:** Added `m_activeDeviceId` (a `QString` member) to `PluginDeviceBridge`:

- `onDeviceConnected` sets `m_activeDeviceId = deviceId`.
- `onDeviceDisconnected` clears `m_activeDeviceId` when the disconnecting device matches.
- `onPluginRegistered` and `onPluginDisconnected` use `m_activeDeviceId` (with `"akp05e"` fallback only when empty — test shim / startup path).

This removes the functional regression where `onPluginRegistered` populated contexts for the wrong device codename and `onPluginDisconnected` only retired contexts for `"akp05e"`, leaving other-device contexts stranded.

______________________________________________________________________

### WR-02: onActivePageChanged is implemented but never connected in application.cpp

**Files modified:** `src/app/src/stream_dock_control_service.hpp`, `src/app/src/stream_dock_control_service.cpp`, `src/app/src/application.cpp`
**Commit:** `f2a6bc8`
**Applied fix (option a from review):** Added `pageNavigated(QString deviceId, QString pageId)` signal to `StreamDockControlService`. The signal is emitted in `navigatePage()` after `m_carouselIndex` advances and `repaintPage()` is called, using `m_activeCodename` as the device id. Application wires the new signal to `PluginDeviceBridge::onActivePageChanged` in the Phase 19-03 block:

```cpp
QObject::connect(m_streamDockControl.get(),
                 &StreamDockControlService::pageNavigated,
                 m_pluginBridge.get(),
                 &PluginDeviceBridge::onActivePageChanged);
```

Page navigation via touch-strip swipe (Phase 16) now correctly triggers `willDisappear` for departing-page contexts and `willAppear` for arriving-page contexts.

______________________________________________________________________

_Fixed: 2026-05-24T14:35:04Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
