---
phase: 20-property-inspector-settings
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - src/app/src/pi_url_policy.cpp
  - src/app/src/pi_url_request_interceptor.cpp
  - src/app/src/pi_bridge.cpp
  - src/app/src/pi_cef_shim.cpp
  - src/app/src/property_inspector_controller.cpp
  - src/app/src/application.cpp
findings:
  critical: 1
  warning: 3
  info: 2
  total: 6
status: issues_found
---

# Phase 20: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

Phase 20 delivers the Property Inspector settings persistence layer (M4): per-context and
plugin-wide settings stored as atomic-rename JSON files, the `invoke()` cefQuery dispatcher
wired to typed `$SD` slots, the cefQuery JS polyfill injected at DocumentCreation, and the
PI->plugin relay wiring in Application. The URL policy and interceptor are the security
boundary.

The overall architecture is sound. The `isSafeUuidComponent` path-traversal guard is
correctly placed and the `pathIsInsideDir` textual comparison is the right call over
`canonicalFilePath` (symlink-aware). The CDN allowlist uses host-equality (not suffix),
`isOpenUrlAllowed` correctly rejects non-https, and `isSdpiCssRequest` redirect target is a
fixed qrc constant. Per-plugin profile isolation via `QQuickWebEngineProfile` without a
storageName produces independent off-the-record profiles. The `activeBridgeChanged` signal
and `bridge` as the connection context for auto-disconnect are both correct.

One critical defect was found: the `invoke()` dispatcher silently drops the `sendToPlugin`
payload when the plugin passes a JSON object (which is the idiomatic SDK usage) because
`QJsonValue::toString()` returns an empty `QString` for non-string values. Three warnings
and two info items follow.

______________________________________________________________________

## Critical Issues

### CR-01: `invoke()` silently drops `sendToPlugin` payload when plugin sends a JSON object

**File:** `src/app/src/pi_bridge.cpp:452`

**Issue:** The `invoke()` dispatcher extracts the `sendToPlugin` payload with
`.toString()`:

```cpp
QString const payload = obj.value(QStringLiteral("payload")).toString();
sendToPlugin(payload);
```

The Stream Deck SDK-2 wire protocol specifies that `sendToPlugin` carries its payload as a
JSON *object* (not a string):

```json
{ "event": "sendToPlugin", "payload": { "key": "value" } }
```

`QJsonValue::toString()` on a JSON object value returns `QString()` (an empty string) — it
does not serialize the object. As a result, `sendToPlugin("")` is called, the relay signal
`toPluginRequested(pluginUuid_, "")` fires, and `SdPluginServer::sendEvent` receives an
empty JSON object instead of the plugin's data. **The payload is silently dropped with no
error log.**

This breaks PI-to-plugin messaging for every plugin that sends a JSON object payload (the
majority of real-world plugins). The `logMessage` case on line 458 has the same issue if
plugins ever send `payload` as an object there, but that field is reliably a string per SDK
spec so the impact is lower.

**Fix:** Extract the value as a `QJsonValue`, then re-serialize it if it is not already a
string — preserving the idiomatic object case:

```cpp
} else if (event == QLatin1String("sendToPlugin")) {
    QJsonValue const rawPayload = obj.value(QStringLiteral("payload"));
    QString payload;
    if (rawPayload.isString()) {
        payload = rawPayload.toString();
    } else {
        // Most PI implementations pass an object: { key: val }.
        // Re-serialize so sendToPlugin receives a non-empty JSON string.
        payload = QString::fromUtf8(
            QJsonDocument(QJsonDocument::fromVariant(rawPayload.toVariant()))
                .toJson(QJsonDocument::Compact));
    }
    sendToPlugin(payload);
```

Or more concisely, using `QJsonDocument` directly on the value:

```cpp
QJsonValue const v = obj.value(QStringLiteral("payload"));
QString const payload = v.isString()
    ? v.toString()
    : QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
sendToPlugin(payload);
```

______________________________________________________________________

## Warnings

### WR-01: `PIWebView.qml` has no `onNavigationRequested` handler — PI can navigate the view to arbitrary URLs

**File:** `src/app/qml/PIWebView.qml:57`

**Issue:** The `PIUrlRequestInterceptor` blocks sub-resource loads (images, scripts, XHR,
fetch). Qt WebEngine distinguishes *navigation requests* (changing the top-level or frame
URL) from sub-resource loads: a main-frame navigation triggered by `window.location = "file:///etc/passwd"` or a plugin `<a href="file:///...">` click may not be intercepted by
`QWebEngineUrlRequestInterceptor` (its `interceptRequest` is called for sub-resources and
navigations, but the `ResourceTypeMainFrame` resource type is handled differently on some Qt
builds, and the QML-side `settings.localContentCanAccessFileUrls: false` only restricts XHR
/fetch, not navigations). Without a `navigationRequested` deny-by-default, the view could
navigate away from the plugin PI page entirely.

`PIWebView.qml` currently sets `settings.localContentCanAccessRemoteUrls: false` and
`settings.localContentCanAccessFileUrls: false`, which mitigates many cases, but defense-in-
depth requires an explicit navigation guard.

**Fix:** Add a `onNavigationRequested` signal handler to `WebEngineView` in `PIWebView.qml`
that allows only the initial PI file:// URL and blocks all other navigations:

```qml
WebEngineView {
    // ... existing bindings ...

    onNavigationRequested: function(request) {
        // Allow the initial PI load; block all other navigations.
        // The URL interceptor handles sub-resource policy; this gate
        // prevents the PI from redirecting the view itself.
        var allowed = PropertyInspectorController.activeUrl;
        if (request.url.toString() === allowed.toString()) {
            request.action = WebEngineNavigationRequest.AcceptRequest;
        } else {
            request.action = WebEngineNavigationRequest.IgnoreRequest;
        }
    }
}
```

### WR-02: `sendToPlugin` and `logMessage` have no payload size cap

**File:** `src/app/src/pi_bridge.cpp:332-343, 410-419`

**Issue:** `setSettings` and `setGlobalSettings` both enforce `kMaxSettingsBytes` (1 MiB)
before writing to disk or emitting any signal. `sendToPlugin` and `logMessage` have no
equivalent cap:

```cpp
void PIBridge::sendToPlugin(QString const& json) {
    // No size check here.
    emit toPluginRequested(pluginUuid_, json);  // forwards to SdPluginServer
}

void PIBridge::logMessage(QString const& message) {
    // No size check here.
    AJAZZ_LOG_INFO("pi-bridge", "... {}", message.toStdString());
}
```

A hostile or buggy PI page can call `$SD.sendToPlugin(hugePayload)` in a loop, causing:

- Unbounded in-memory `QString` copies per relay hop (bridge → Application lambda →
  `SdPluginServer::sendEvent`)
- Potentially large WebSocket frames sent to the plugin process
- Log spam that fills available disk if `logMessage` is called with a large string

**Fix:** Apply the same `kMaxSettingsBytes` cap (or a separate, per-method cap) at the top
of both methods:

```cpp
void PIBridge::sendToPlugin(QString const& json) {
    if (json.toUtf8().size() > kMaxSettingsBytes) {
        AJAZZ_LOG_ERROR("pi-bridge", "sendToPlugin: payload exceeds cap; refusing");
        return;
    }
    // ...
}
```

A smaller cap (e.g. 64 KiB) is more appropriate for `logMessage` to keep log lines
manageable.

### WR-03: `readJsonOrEmpty` reads settings files without a size cap

**File:** `src/app/src/pi_bridge.cpp:202`

**Issue:** `readJsonOrEmpty` uses `in.readAll()` with no size cap:

```cpp
QByteArray const data = in.readAll();
```

The write path (`writeJsonAtomic`) is guarded by `kMaxSettingsBytes`, so files written
by the bridge are bounded. However, the settings directory (`<AppDataLocation>/plugins/<uuid>/ settings/`) is in a user-writable location. If a file at that path is replaced externally
with a large file (by a malicious plugin binary running with user privileges, by a local
privilege escalation, or by test/dev tooling) the next `getSettings` call reads it entirely
into RAM before the JSON parse check.

**Fix:** Cap the read before `readAll`:

```cpp
constexpr qint64 kMaxReadBytes = kMaxSettingsBytes; // reuse the write-side cap
if (in.size() > kMaxReadBytes) {
    AJAZZ_LOG_ERROR("pi-bridge", "{}: file '{}' is {} bytes, exceeds cap; returning empty",
                    whatForLog.toStdString(), path.toStdString(),
                    static_cast<long long>(in.size()));
    return QStringLiteral("{}");
}
QByteArray const data = in.readAll();
```

______________________________________________________________________

## Info

### IN-01: `isSafeUuidComponent` accepts non-ASCII Unicode (0x80–0xFFFF) including lone surrogates

**File:** `src/app/src/pi_bridge.cpp:66-86`

**Issue:** The validator blocks ASCII control chars (< 0x20) and DEL (0x7F), and explicitly
blocks `/`, `\`, `.`, and `..`. But it accepts all code points 0x80–0xFFFF, including lone
surrogate halves (0xD800–0xDFFF), which are invalid in well-formed Unicode and not valid in
stream-deck manifest UUIDs (which are reverse-DNS or hex-UUID strings):

```cpp
ushort const u = c.unicode();
if (u < 0x20 || u == 0x7f) {
    return false;
}
// Everything 0x80–0xFFFF passes here.
```

This is not exploitable for path traversal (the `/`, `\`, and `..` checks hold), but a
plugin with an emoji or non-Latin UUID would be accepted and used as a filesystem directory
name, which is unusual and may cause issues on case-insensitive or encoding-sensitive
filesystems (Windows NTFS, old macOS HFS+).

**Fix:** Tighten the allowlist to match the stated contract (reverse-DNS + hex UUIDs):

```cpp
// Accept: ASCII printable except / \ control chars and ..
// Reject: any char above 0x7E (DEL exclusive) to stay filesystem-safe
// across platforms.
if (u > 0x7e) {
    return false;  // non-ASCII: reject (UUIDs are ASCII)
}
```

### IN-02: `cefQuery` shim runs in sub-frames (`runsOnSubFrames: true`) — each sub-frame gets its own `QWebChannel` connection attempt

**File:** `src/app/src/pi_cef_shim.cpp:53`, `src/app/src/pi_cef_shim.hpp:125`

**Issue:** `makeCefQueryShim()` sets `runsOnSubFrames(true)`, meaning every `<iframe>` inside
the PI page also gets the cefQuery polyfill injected. Each call to
`new QWebChannel(qt.webChannelTransport, ...)` from a sub-frame initiates a new QWebChannel
handshake over the same underlying transport. While this mirrors the `makeMiraboxShim()`
disposition, the QWebChannel transport is not designed for concurrent initialisation from
multiple frames. In practice, sub-frame PI pages are uncommon, but the pattern can cause
redundant channels or race-condition ordering issues if a PI embeds an iframe that also
calls `cefQuery`.

**Fix:** If sub-frame cefQuery is truly needed, evaluate whether the sub-frame should share
the same channel object (resolved via parent-frame access or a shared module) rather than
opening its own. If not needed, set `setRunsOnSubFrames(false)` to match the simpler case
and document the decision.

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
