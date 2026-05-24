---
phase: 20-property-inspector-settings
fixed_at: 2026-05-24T19:03:00Z
review_path: .planning/phases/20-property-inspector-settings/20-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 20: Code Review Fix Report

**Fixed at:** 2026-05-24T19:03:00Z
**Source review:** `.planning/phases/20-property-inspector-settings/20-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 5 (CR-01, WR-01, WR-02, WR-03, IN-01; IN-02 skipped per guidance)
- Fixed: 5
- Skipped: 1 (IN-02, per fix_guidance: "skip unless trivial")

## Fixed Issues

### CR-01: invoke() silently drops sendToPlugin payload when plugin sends a JSON object

**Files modified:** `src/app/src/pi_bridge.cpp`, `tests/unit/test_pi_bridge.cpp`
**Commit:** `60f86e9`
**Applied fix:** In the `invoke()` dispatcher's `sendToPlugin` branch, replaced
`obj.value("payload").toString()` (which returns `""` for JSON objects) with an
explicit `isString()` check: if the value is a string, use it directly; otherwise
re-serialize via `QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)`.
This preserves the idiomatic SDK shape `{"event":"sendToPlugin","payload":{...}}`.

Added regression tests:

- `PIBridge invoke sendToPlugin with object payload forwards non-empty payload` — verifies
  the object round-trips through `invoke()` to `toPluginRequested` non-empty and parses back correctly.
- `PIBridge invoke sendToPlugin with string payload forwards the string unchanged` — verifies
  pre-serialized string payloads still pass through unchanged.
- Two additional WR-02 size-cap tests (sendToPlugin oversized rejected, boundary accepted)
  were included in this commit as they exercise the same relay path.

### WR-01: PIWebView.qml has no onNavigationRequested handler

**Files modified:** `src/app/qml/PIWebView.qml`
**Commit:** `ad06b6d`
**Applied fix:** Added an `onNavigationRequested` handler to the `WebEngineView` in
`PIWebView.qml`. The handler allows only the URL equal to
`PropertyInspectorController.activeUrl` (the current PI entry) and sets
`WebEngineNavigationRequest.IgnoreRequest` for all other navigations. Updated the
security-posture comment in the file header to document the WR-01 guard.

### WR-02: sendToPlugin and logMessage have no payload size cap

**Files modified:** `src/app/src/pi_bridge.cpp`
**Commit:** `0ea343c`
**Applied fix:** Added two new constants:

- `kMaxRelayBytes = kMaxSettingsBytes` (1 MiB) — caps `sendToPlugin` payloads before the
  relay signal fires; oversized payloads log `ERROR` and return early.
- `kMaxLogMessageBytes = 64 KiB` — caps `logMessage` payload to keep log lines manageable;
  oversized messages log `ERROR` and return early.

Size-cap regression tests (`PIBridge sendToPlugin rejects oversized payload` and
`PIBridge sendToPlugin accepts a payload at the boundary`) were added in the CR-01 commit.

### WR-03: readJsonOrEmpty reads settings files without a size cap

**Files modified:** `src/app/src/pi_bridge.cpp`
**Commit:** `c073dbf`
**Applied fix:** Added a size check in `readJsonOrEmpty()` immediately after `open()` and
before `readAll()`. If `in.size() > kMaxSettingsBytes` (1 MiB), the function logs an
`ERROR` with the actual byte count and returns `"{}"` without buffering the file. This
guards against externally-replaced oversized settings files in the user-writable
`<AppDataLocation>/plugins/<uuid>/settings/` directory.

### IN-01: isSafeUuidComponent accepts non-ASCII Unicode

**Files modified:** `src/app/src/pi_bridge.cpp`
**Commit:** `c93d839`
**Applied fix:** Added `if (u > 0x7e) { return false; }` inside the character loop in
`isSafeUuidComponent()`, before the slash check. Stream Deck SDK-2 UUIDs are reverse-DNS
strings and hex UUIDs — both are pure ASCII. Lone surrogates (0xD800-0xDFFF) and non-Latin
characters are now rejected. No existing valid UUIDs are affected since the contract is
ASCII-only.

## Skipped Issues

### IN-02: cefQuery shim runs in sub-frames (runsOnSubFrames: true)

**File:** `src/app/src/pi_cef_shim.cpp:53`
**Reason:** Skipped per fix_guidance ("skip unless trivial"). The sub-frame channel
initialization is an edge case; evaluating whether sub-frames should share the parent
frame's channel object (rather than opening their own) requires design work beyond a
mechanical fix. No functional regression exists for the common case (PIs without iframes).
**Original issue:** Each sub-frame gets its own `QWebChannel` connection attempt, which may
cause redundant channels or race conditions if a PI embeds an iframe that also calls
`cefQuery`.

______________________________________________________________________

## Build and Test Verification

**Build:** `cmake --build build/linux-release` — completed with no errors. All 18 ninja
steps passed including `ajazz-control-center` app link and `ajazz_unit_tests` link.
Incremental build confirmed clean after fast-forward.

**Tests:** `ctest --preset linux-release` — **538/538 tests passed** (0 failures).
New tests added: 4 (2 × CR-01 object/string payload, 2 × WR-02 oversized/boundary cap).
All pre-existing 534 tests continue to pass.

______________________________________________________________________

_Fixed: 2026-05-24T19:03:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
