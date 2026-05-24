---
phase: 19-device-plugin-bridge
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 3
files_reviewed_list:
  - src/app/src/plugin_device_bridge.hpp
  - src/app/src/plugin_device_bridge.cpp
  - src/app/src/application.cpp
findings:
  critical: 2
  warning: 4
  info: 2
  total: 8
status: issues_found
---

# Phase 19: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 3
**Status:** issues_found

## Summary

Phase 19 delivers `PluginDeviceBridge` composing the Phase 14/15/17 seams to
route inbound plugin actions to device paints and outbound device input events
to registered plugins. The auth gate (T-17-PREAUTH in SdPluginServer) is correctly
in place — `actionReceived` is only emitted after authentication, and the bridge's
`onAction` enforces cross-plugin context ownership (T-19-xplugin). The
`decodeDataUriImage` crash-safety chain is sound (Qt's `fromBase64` + `loadFromData`
each bool-checked, placeholder painted on failure, no failure event back).

Two blockers are present: (1) no upper-bound on the decoded image payload allows
a plugin to exhaust host memory via an oversized `data:` URI — explicitly called
out in the project's security checklist (T-19-img) as requiring an ARCH-04 size
bound; (2) `ContextRegistry::coordKey` omits `deviceId`, causing m_byCoord to
be corrupted when two simultaneously-connected devices share the same coordinate,
which also causes `retire()` to silently remove a live binding for the surviving
device.

Four warnings follow: `onDeviceDisconnected` skips `willDisappear` for individual
contexts before retiring them; `onActivePageChanged` is fully implemented but
never connected in `application.cpp`; `onPluginRegistered` / `onPluginDisconnected`
hardcode `"akp05e"` and `"root"` rather than consulting the actual active device
and page; `onDeviceEvent` ignores the `deviceId` parameter for routing.

______________________________________________________________________

## Critical Issues

### CR-01: No upper-bound on plugin-supplied image payload before decode

**File:** `src/app/src/plugin_device_bridge.cpp:167-201` (`decodeDataUriImage`)

**Issue:** `decodeDataUriImage` receives an untrusted `data:` URI from a plugin
WebSocket frame and calls `QByteArray::fromBase64` + `QImage::loadFromData` without
any size check on either the base64 string length or the decoded byte array.
`QByteArray::fromBase64` allocates memory proportional to the input length;
`QImage::loadFromData` then allocates width × height × channels for the decoded
pixel buffer. A malicious plugin on the loopback interface can send a
multi-hundred-megabyte base64 string encoding a valid high-resolution image
(e.g. 16384×16384 RGBA = 1 GB decoded) and OOM-crash the host process. The project
security checklist in the review scope explicitly requires: "confirm decoded image
size is bounded (a plugin must not OOM/crash the host with a huge image); reuse
ARCH-04 image_pipeline."

No message-size cap is set on the `QWebSocketServer` (`SdPluginServer` constructor
does not call `setMaxAllowedIncomingMessageSize`) so the guard must live at the
decode site.

**Fix:** Add two guards in `decodeDataUriImage` before any allocation:

```cpp
// T-19-img: cap base64 body length (~512 KB encodes a 384 KB raw image,
// comfortably above any real key icon).
constexpr qsizetype kMaxBase64Bytes = 512 * 1024; // 512 KB
if (bodyStr.size() > kMaxBase64Bytes) {
    return {false, {}};
}

QByteArray const raw = QByteArray::fromBase64(bodyStr.toUtf8());
if (raw.isEmpty()) {
    return {false, {}};
}

// T-19-img: cap raw byte count to prevent QImage::loadFromData OOM.
constexpr qsizetype kMaxRawBytes = 384 * 1024; // 384 KB
if (raw.size() > kMaxRawBytes) {
    return {false, {}};
}
```

Alternatively, set `m_server->setMaxAllowedIncomingMessageSize(1 * 1024 * 1024)`
in `SdPluginServer::SdPluginServer` to reject oversized frames before JSON parse.
Both layers are recommended (defense-in-depth).

______________________________________________________________________

### CR-02: ContextRegistry::coordKey omits deviceId — multi-device coord collision corrupts m_byCoord

**File:** `src/app/src/plugin_device_bridge.cpp:55-57` (`coordKey`), `62-63` (`registerContext`), `96` (`retire`)

**Issue:** The `m_byCoord` reverse index maps `controller + "#" + row + "#" + col`
to a context string, but this key does not include `deviceId`. When two
simultaneously connected devices each register a binding for the same controller /
row / col (e.g. both "akp05e" and "akp153" have a plugin bound to Keypad key 3),
the second `registerContext` call silently overwrites the first entry in `m_byCoord`.
Subsequently:

1. Events from the first device route to the second device's plugin (cross-device
   event leakage — a security and correctness failure).
1. Calling `retire()` on the *first* device's context removes the *shared* coord key
   from `m_byCoord`, leaving the second device's context stranded in `m_byContext`
   but invisible to `byCoord` lookups.

The test at line 457-463 in `test_plugin_device_bridge.cpp` explicitly acknowledges
this: *"The coord lookup for ('Keypad', 0, 0) might have been overwritten by the
last registration"* — it does not assert `byCoord` correctness for the surviving
device. Phase 19 is single-device (A6 simplification), but the bug is live in the
data structure today and blocks multi-device wiring.

**Fix:** Include `deviceId` in `coordKey`:

```cpp
// static private
QString ContextRegistry::coordKey(QString const& deviceId,
                                  QString const& controller,
                                  int row, int column) {
    return deviceId + QChar('#') + controller + QChar('#') +
           QString::number(row) + QChar('#') + QString::number(column);
}
```

Update all call sites in `registerContext`, `retire`, and `byCoord` to pass
`deviceId`. `byCoord` will need a `deviceId` parameter; `onDeviceEvent` must pass
its `deviceId` argument rather than ignoring it (see WR-04).

______________________________________________________________________

## Warnings

### WR-01: onDeviceDisconnected skips willDisappear for individual contexts

**File:** `src/app/src/plugin_device_bridge.cpp:806-821`

**Issue:** `onDeviceDisconnected` sends `deviceDidDisconnect` to all registered
plugins and then calls `m_registry.retireDevice(deviceId)` directly. It does NOT
call `retirePageContexts(deviceId, "root", {})` first, which is the path that
iterates contexts, sends `willDisappear` per context, and then retires them.
Compare with `onPluginDisconnected` (line 773) which correctly calls
`retirePageContexts`. The Elgato SDK spec requires `willDisappear` for each
visible action instance before the device disappears; plugins that track
lifecycle events will mis-count their mounted instances.

**Fix:** Call `retirePageContexts` before (or instead of) `retireDevice`:

```cpp
void PluginDeviceBridge::onDeviceDisconnected(QString const& deviceId) {
    if (m_server == nullptr) {
        return;
    }
    // Send willDisappear for every context on this device before retiring.
    retirePageContexts(deviceId, QStringLiteral("root"), {});
    // m_registry.retireDevice() is now a no-op since retirePageContexts
    // retired all root-page contexts; call it anyway for safety in case
    // of non-root-page contexts (Phase 16 multi-page).
    m_registry.retireDevice(deviceId);

    // Send deviceDidDisconnect to all registered plugins (§4.4).
    QJsonObject const deviceInfo{ ... };
    for (QString const& uuid : m_registeredPlugins) {
        m_server->sendEvent(uuid, QStringLiteral("deviceDidDisconnect"), payload);
    }
}
```

______________________________________________________________________

### WR-02: onActivePageChanged is implemented but never connected in application.cpp

**File:** `src/app/src/application.cpp` (constructor, lines 363-412)

**Issue:** `PluginDeviceBridge::onActivePageChanged` is fully implemented (it calls
`retirePageContexts` then `populateContextsForActivePage`), declared as a public
slot, and documented. However, `application.cpp` never wires it to any signal.
The candidate signal (`StreamDockControlService::navigatePage`) is a slot, not a
signal, so there is currently no signal to connect to. The result is that page
navigation via touch-strip swipe (Phase 16) does not update plugin contexts —
plugins on the departing page receive neither `willDisappear` nor are new contexts
minted for the arriving page (`willAppear`). This silently breaks multi-page plugin
support when Phase 16 is active.

**Fix:** Either (a) add a `pageNavigated(QString deviceId, QString pageId)` signal
to `StreamDockControlService` emitted inside `navigatePage` after the page state
updates, then connect it:

```cpp
QObject::connect(m_streamDockControl.get(),
                 &StreamDockControlService::pageNavigated,
                 m_pluginBridge.get(),
                 &PluginDeviceBridge::onActivePageChanged);
```

Or (b) document clearly in a `TODO(Phase-23)` comment that the slot is deferred
and will not fire until this connection is added — preventing the current silent
mismatch where the slot exists but has no callers.

______________________________________________________________________

### WR-03: onPluginRegistered and onPluginDisconnected hardcode deviceId and pageId

**File:** `src/app/src/plugin_device_bridge.cpp:764-780`

**Issue:** Both lifecycle methods hardcode `QString const deviceId = "akp05e"` and
`QString const pageId = "root"`. This means:

- If the active device is any device other than `"akp05e"` when a plugin registers,
  `populateContextsForActivePage` is called with the wrong codename; the profile
  accessor returns a profile for a different device; the context IDs minted carry
  `deviceId = "akp05e"` even though the physical device is not an AKP05E.
- `onPluginDisconnected` retires contexts under `"akp05e"#"root"` only; contexts
  for any other device are orphaned in the registry indefinitely.

The hardcode is acknowledged in comments as a Phase 23 simplification, but it is
a functional regression path (not just a deferred feature) because `onDeviceConnected`
already uses the actual `deviceId` parameter. The two lifecycle flows are
inconsistent.

**Fix:** Obtain the actual active device codename via the bridge's profile accessor
or a separate device-accessor injected from Application:

```cpp
void PluginDeviceBridge::onPluginRegistered(QString const& pluginUuid) {
    m_registeredPlugins.insert(pluginUuid);
    // Use the accessor to derive the active device codename; fall back to
    // "akp05e" only when no accessor is set (test shim path).
    QString const deviceId = m_activeDeviceId.isEmpty()
                             ? QStringLiteral("akp05e")
                             : m_activeDeviceId;
    populateContextsForActivePage(deviceId, pluginUuid);
}
```

Where `m_activeDeviceId` is updated by `onDeviceConnected` / `onDeviceDisconnected`.

______________________________________________________________________

### WR-04: onDeviceEvent ignores the deviceId parameter for all routing lookups

**File:** `src/app/src/plugin_device_bridge.cpp:471-614`

**Issue:** `onDeviceEvent` receives `deviceId` as its first parameter but marks it
`Q_UNUSED` (line 614). All `byCoord` lookups use only `controller + row + col`
with no device filter. Consequently, a `KeyPressed` event from device `"akp153"` on
key 3 routes to whatever plugin registered coord `(Keypad, 0, 2)` last — which may
belong to `"akp05e"`. This is a cross-device event leakage that will manifest as
soon as a second Stream Dock device is connected. It is structurally connected to
CR-02 (the root fix is to include `deviceId` in `coordKey`).

Even before the CR-02 fix, passing `deviceId` through to `byCoord` (once that
method gains a `deviceId` parameter) is the right change here.

**Fix:** After fixing CR-02, pass `deviceId` to each `byCoord` call:

```cpp
auto const ctxOpt = m_registry.byCoord(deviceId, QStringLiteral("Keypad"), gc.row, gc.column);
```

Remove the `Q_UNUSED(deviceId)` annotation once the parameter is used.

______________________________________________________________________

## Info

### IN-01: Unused includes QBuffer and QImageReader in plugin_device_bridge.cpp

**File:** `src/app/src/plugin_device_bridge.cpp:35,38`

**Issue:** `#include <QBuffer>` and `#include <QImageReader>` are present but
neither `QBuffer` nor `QImageReader` is referenced anywhere in the implementation.
`QBuffer` is used in the test fixture (`test_plugin_device_bridge.cpp`) but that is
a separate translation unit. These are dead includes that add compilation overhead
and may confuse readers into thinking they affect behavior.

**Fix:** Remove both includes from `plugin_device_bridge.cpp`.

______________________________________________________________________

### IN-02: Misleading comment in decodeDataUriImage — fromBase64 does not return empty for garbage input

**File:** `src/app/src/plugin_device_bridge.cpp:185-188`

**Issue:** The comment states: *"fromBase64 never throws; an empty/garbage input
yields empty bytes (which loadFromData will reject)."* This is inaccurate for
non-empty garbage input. Qt's default `QByteArray::fromBase64(data)` uses
`IgnoreBase64DecodingErrors` and silently ignores non-base64 characters rather than
returning empty bytes. For example, `fromBase64("!!!notbase64!!!")` produces
non-empty garbage bytes (the valid base64 characters `n`, `o`, `t`, `b`, `a`, `s`,
`e`, `6`, `4` are decoded). The `if (raw.isEmpty())` guard then does not fire, but
`QImage::loadFromData(garbage)` correctly returns false — so the end-to-end behavior
(`ok = false`) is correct. The comment misrepresents the intermediate state and
could mislead a future maintainer into removing the `loadFromData` check as
"redundant." The behavior is sound; the comment needs correction.

**Fix:**

```cpp
// Decode base64. fromBase64 with the default IgnoreBase64DecodingErrors flag
// silently ignores non-base64 characters rather than returning empty bytes.
// An empty result indicates an all-whitespace or zero-length input.
// loadFromData is the actual rejection gate for any garbage payload.
QByteArray const raw = QByteArray::fromBase64(bodyStr.toUtf8());
if (raw.isEmpty()) {
    return {false, {}};
}
```

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
