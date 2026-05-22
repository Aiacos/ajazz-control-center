---
phase: 13-catalogue-v1-1-ui-verifies-back-fill
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - src/app/src/sdplugin_extractor.cpp
  - src/app/src/plugin_catalog_model.cpp
  - src/app/src/plugin_catalog_model.hpp
  - src/app/src/sd_plugin_server.cpp
  - src/app/src/sd_plugin_server.hpp
  - src/app/src/streamdock_catalog_fetcher.cpp
  - src/app/src/app_update_service.cpp
  - src/app/src/device_model.cpp
  - src/app/src/time_sync_service.cpp
  - src/app/qml/LoadedPluginsPage.qml
  - src/app/qml/RgbPicker.qml
  - src/app/qml/SettingsRow.qml
  - src/app/qml/PropertyInspector.qml
  - src/app/qml/PluginStore.qml
  - src/app/qml/components/BatteryIndicator.qml
  - src/app/qml/components/Notification.qml
  - src/app/qml/Main.qml
  - tests/unit/test_sdplugin_extractor.cpp
findings:
  critical: 0
  warning: 5
  info: 4
  total: 9
status: issues_found
---

# Phase 13: Code Review Report (RE-REVIEW / verification pass)

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 18
**Status:** issues_found

## Summary

This is a verification pass over the four prior Phase 13 findings plus a
re-list of the still-applicable warnings/info. All four prior findings are
**confirmed resolved at the source level** with no holes and no regression
introduced by the edits. The remaining open items are warnings and info
carried forward from the prior review; none rise to BLOCKER.

### Verification of the four fixed findings

**CR-01 (zip-slip in `extractSdPluginArchive`) — RESOLVED.**
`sdplugin_extractor.cpp:51-71` now computes `rootCanon = QDir::cleanPath(tmpPath) + "/"` and, for every entry, derives
`outPath = QDir::cleanPath(tmpPath + "/" + info.filePath)` and rejects when
the entry is absolute (`startsWith('/')`), drive/scheme-prefixed (`:/` or
`:\`), or `!outPath.startsWith(rootCanon)`. Because `cleanPath` collapses
`..` lexically, the surviving traversal `x/../../../escape.txt` resolves
above the root and fails the `startsWith` containment check; the loop
`break`s, `extractOk=false`, the staging dir is `removeRecursively()`'d, and
the function returns `false` before any write. The guard precedes both the
`isDir` mkpath and the `isFile` write. The trailing `/` on `rootCanon`
defeats sibling-prefix attacks (e.g. `tmp_foo-evil`). Symlinks are skipped.
The new regression test `test_sdplugin_extractor.cpp:190-243` drives a
Python-built malicious zip (base64-embedded, since QZipWriter sanitises on
write) and asserts the escape file is never created and neither the staging
nor final dir survives. No hole found.

**CR-02 (`LoadedPluginsPage` Material.theme bound to non-existent
`Theme.materialTheme`) — RESOLVED.**
`LoadedPluginsPage.qml:36` now reads
`Material.theme: ThemeService.effectiveMode === "light" ? Material.Light : Material.Dark`, which is exactly the expression `Main.qml:48-49` uses for
its `materialTheme` property and re-asserts on every Drawer
(`Main.qml:196,226,260`). `ThemeService.effectiveMode` is a real, always-
resolved property (never `undefined`), so the binding can no longer evaluate
to `undefined`. The CLAUDE.md "Material attached props don't cross Popup
scope" gotcha is correctly honoured by re-asserting inside the Page that
mounts in the Drawer. No hole found.

**WR-04 (unbounded plugin download) — RESOLVED.**
A 64 MiB cap (`plugin_catalog_model.cpp:388 kMaxPluginDownloadBytes`) plus a
`PK\x03\x04` magic check are enforced at three layers:
(1) up-front abort in the `downloadProgress` slot when `received` or the
advertised `total` exceed the cap (`:508-512`);
(2) the static, pure, unit-testable `validateDownloadedArchive()`
(`:415-427`) is called in the `finished` handler *before* any disk write
(`:545-550`), re-checking the size cap (covers the no-progress-signal case)
and the zip magic;
(3) the `finished` handler maps the resulting
`OperationCanceledError` to a clear "exceeds the N MB limit" message
(`:529-532`).
The gate sits strictly ahead of `QFile::open`/`write`, so a bogus or
oversized body never lands on disk. The unit test
`test_plugin_catalog_proxy_model.cpp:255-280` covers valid magic, empty
body, non-zip body, and a 64 MiB+1 body with valid magic. No hole found.

**WR-07 (`SdPluginServer` dead connection slots) — RESOLVED.**
`onClientDisconnected` (`sd_plugin_server.cpp:140-161`) now captures the uuid
first, then `erase`/`remove_if`s the slot whose `socket == client` rather
than nulling it, then `deleteLater()`s the socket. No `{uuid,nullptr}` row
survives, so `connectedPluginCount`/`uuidForClient`/`registerPlugin` linear
scans never see dead rows and a same-UUID reconnect yields exactly one live
slot. The regression test `test_sd_plugin_server.cpp:207-246` asserts the
count returns to 0 after disconnect and is exactly 1 after a same-UUID
reconnect. `stop()` (`:72-82`) still disconnects-this, closes, deleteLaters,
nulls, and `clear()`s the whole vector — consistent. No hole found.

### Regression scan of the four edits

No regressions introduced. Specifically:

- The zip-slip guard does not break the legitimate paths: the wrapper-strip,
  in-place file-to-dir replacement, and standalone-sweep tests still pass the
  containment check (entries are plain relative paths under the root).
- `validateDownloadedArchive` is `static` + pure and does not perturb the
  install state machine; `installFinished` still fires exactly once on each
  branch.
- The `erase`/`remove_if` in `onClientDisconnected` is safe against the
  `disconnect(this)` performed in `stop()` (stop nulls + clears, so the
  disconnected slot can't double-fire into a stale iterator).

## Warnings

### WR-05: RgbPicker fires unsolicited HID writes on tab open / device swap

**File:** `src/app/qml/RgbPicker.qml:88-95, 110-116`
**Issue:** The brightness and speed `Slider`s call
`LightingService.setMode(...)` from `onValueChanged`. `onValueChanged` fires
not only on user drag but also on the programmatic seed at
`value: Math.min(3, root.firmwareBrightnessMax)` (`:84`, `:106`) and whenever
`root.firmwareBrightnessMax` / `firmwareSpeedMax` re-resolve because
`deviceCodename` changed. The only guard is
`if (firmwareModeBox.currentValue === undefined) return` — that is satisfied
as soon as the ComboBox model is populated, so merely opening the RGB tab (or
switching the bound device) emits an HID `setMode` write to hardware the user
never touched. The `ComboBox.onActivated` path is correctly user-only;
the two sliders are not. This can flicker device lighting and burns HID I/O
on every tab open.
**Fix:** Gate the slider handlers on user interaction, e.g. only write from
`onMoved` (fires on user drag, not on programmatic `value` assignment) rather
than `onValueChanged`, or set a `property bool seeded: false` flipped true in
`Component.onCompleted`/after the seed and early-return while `!seeded`:

```qml
Slider {
    id: firmwareBrightnessSlider
    // ...
    onMoved: {                           // user-drag only
        if (firmwareModeBox.currentValue === undefined) return
        LightingService.setMode(root.deviceCodename,
            firmwareModeBox.currentValue, value, firmwareSpeedSlider.value)
    }
}
```

### WR-06: SettingsRow sleep ComboBox silently maps an unknown value to "Never" (0)

**File:** `src/app/qml/SettingsRow.qml:99-108, 320-326`
**Issue:** `_sleepIndexFor(minutes)` returns `0` ("Never") for any
`sleepMinutes` value not in `_sleepValues [0,1,3,5,10,30]`. If
`SettingsService.currentSettings()` ever reports a sleep value the UI list
doesn't contain (a firmware default or a value set by the vendor app, e.g.
`2` or `15`), the ComboBox silently snaps to "Never". On the next "Apply" the
device is reprogrammed to `0` (disable sleep) without the user ever choosing
that — a silent destructive write of the sentinel. The response slider
(`:274-279`, `from:1 to:5 SnapAlways`) is correctly clamped; the sleep path is
the unguarded one. Note the snapshot fallback default in the binding
(`:94 sleepMinutes: 0`) is also "Never", so a disconnected device defaults to
disabling sleep on Apply too.
**Fix:** When `_sleepIndexFor` finds no match, append the actual value as a
custom entry (e.g. `"%1 min"`) and select it, or disable Apply until the user
explicitly picks a known value, so an out-of-list firmware value is never
silently rewritten to 0.

### WR-08: app-update banner re-fires for a dismissed tag on a non-304 re-check

**File:** `src/app/src/app_update_service.cpp:316-321, 406-435`
**Issue:** The dismissed-tag suppression in `applyRelease` (`:426-431`) only
holds while the server returns a full body. The dismissed tag is session-
scoped by design (`dismissCurrentUpdate` persists it, `applyRelease` honours
it), but the 304 fast-path at `:316-321` restores
`Status::UpdateAvailable` purely from `m_latestVersion.isEmpty()` — it does
**not** re-consult the persisted `dismissedTag`. So the sequence {check ->
UpdateAvailable -> user dismisses (Idle) -> next 24 h auto-check returns 304
because the ETag still matches} flips the banner back to `UpdateAvailable`
even though the user dismissed that exact tag. The full-body path
(`:426-431`) is correct; only the 304 shortcut regresses.
**Fix:** Mirror the dismissed-tag check in the 304 branch:

```cpp
if (httpStatus == 304) {
    if (m_status == Status::Checking) {
        QSettings s;
        QString const dismissed =
            s.value(QString::fromLatin1(kDismissedTagKey)).toString();
        bool const haveUpdate = !m_latestVersion.isEmpty()
            && isNewerThan(m_latestVersion, currentVersion())
            && m_latestVersion != dismissed;
        setStatus(haveUpdate ? Status::UpdateAvailable : Status::UpToDate);
    }
    return;
}
```

### WR-09: StreamdockCatalogFetcher Loading re-entry guard has no watchdog

**File:** `src/app/src/streamdock_catalog_fetcher.cpp:502-510, 581-657`
**Issue:** `refresh()` early-returns whenever `m_state == State::Loading`
(`:507`). The only exits from `Loading` live inside `onPageFinished` (every
error/parse/envelope branch resets the state; the success branch emits
`Online`). A per-request `setTransferTimeout(kPerPageTimeoutMs)` (`:567`)
means a stalled socket fires `finished` with a timeout error and unwedges —
good, that closes the common case. But the guard has no watchdog of its own:
if a reply is never delivered to `onPageFinished` (NAME torn down mid-flight,
or a future code path drops the connection), `m_state` stays `Loading`
forever and every later `reload()`/Retry no-ops at `:507`, while the QML
Retry button is also disabled while state is "loading"
(`PluginStore.qml:314,448`). Low likelihood given the transfer timeout, but
the guard is not self-healing.
**Fix:** Arm a single-shot watchdog QTimer when entering `Loading` that, on
expiry without a terminal page result, forces the state back to
`Cached`/`Offline` (mirroring `:592-597`) so the guard self-heals and Retry
becomes usable again.

### WR-10: BatteryIndicator keeps a stale percent across an undetected offline transition

**File:** `src/app/qml/components/BatteryIndicator.qml:58, 168-199`
**Issue:** The chip self-hides only on an explicit `batteryUnavailable`
signal (`:178-185`) or while `percent < 0`. If a device goes offline without
`BatteryService` emitting `batteryUnavailable` for that codename (the poll
simply stops because the codename de-registers, or the row's `connected` flag
flips while the last `batteryQueried` value lingers), the chip keeps showing
the last-known percent. The header comment (`:14-18`) claims the parent gates
`visible` on "connected", but inside the component `visible: percent >= 0 && !unavailable` (`:58`) does not consider connection state — the indicator
trusts that an offline device always produces a `batteryUnavailable`. There
is also no `onCodenameChanged` reset, so a recycled delegate can inherit a
prior device's charge until the first signal arrives (only
`Component.onCompleted` seeds, `:191-199`). This is a stale-reading display
bug, not a crash.
**Fix:** (a) clear `percent = -1; unavailable = false` in
`onCodenameChanged` so a recycled delegate doesn't inherit a prior device's
charge, and/or (b) have the parent row bind a `connected` property the
component honours so the chip collapses on disconnect even without an explicit
unavailable signal.

## Info

### IN-01: `extractSdPluginArchive` silently skips entries that are neither file nor dir

**File:** `src/app/src/sdplugin_extractor.cpp:72-92`
**Issue:** The loop handles `info.isDir` and `info.isFile`; any other entry
kind (symlink, FIFO, special) is silently skipped after passing the path
guard. That is the intended security posture (symlinks deliberately not
honoured), but a plugin whose payload genuinely depends on a symlink would
extract an incomplete tree with no warning. Low impact for flat vendor
`.sdPlugin` payloads.
**Fix:** Optional `AJAZZ_LOG_DEBUG` noting the skipped non-regular entry so a
"plugin extracted but broken" field report has a breadcrumb.

### IN-02: `kStandardActions` includes register-events that are unreachable in that scan

**File:** `src/app/src/sd_plugin_server.cpp:212-224, 185-206`
**Issue:** `kStandardActions` (size 13) lists `registerPlugin` and
`registerPropertyInspector`, but both events are fully handled and `return`ed
at `:185-206` before the action scan runs, so the `isAction` branch can never
see them. Harmless dead entries that may mislead a maintainer into thinking
those events flow through `actionReceived`.
**Fix:** Drop the two register-events from `kStandardActions` (size 11), or
note explicitly that they appear only for documentation symmetry.

### IN-03: PluginStore status-glyph trailing comments duplicate the glyph

**File:** `src/app/qml/PluginStore.qml:238-242, 312, 377-381, 446`
**Issue:** Lines like `return "●";  // ●` and `text: "↻" // ↻` carry a
trailing comment that just repeats the literal — redundant noise.
**Fix:** Drop the redundant trailing comments.

### IN-04: `streamdockProductId` field comment overstates its warning-suppression role

**File:** `src/app/src/plugin_catalog_model.hpp:80-85`
**Issue:** The comment says the `= {}` default "doubles as a
`-Wmissing-field-initializers` suppressor", but `downloadUrl` (`:97`) also has
a default and the `mockFixture()` rows
(`plugin_catalog_model.cpp:721-836`) rely on the defaults for both trailing
fields. The suppression is the aggregate's trailing-default behaviour, not a
single field's. Documentation nit.
**Fix:** Reword to attach the note once to the struct: "trailing fields use
default member initializers so positional aggregate init can omit them".

## Hard checks (all pass)

- **QML_SINGLETON via `qmlRegisterSingletonInstance`**: `PluginCatalogModel`
  (hpp:121) and `DeviceModel` use the `create()`+`registerInstance()` factory
  pattern with `static_assert(!std::is_default_constructible_v<...>)`
  (hpp:385) co-located — matches the CLAUDE.md rule. `AppUpdateService` /
  `TimeSyncService` use the same `create`/`registerInstance` shape.
- **WebEngineView / WebChannelQuick**: `PropertyInspector.qml` loads
  `PIWebView.qml` via a *string* `source` (`:94`) so the WebEngine import is
  not hard-required at compile time on no-WebEngine builds; the `Loader.Error`
  status is logged (`:100-105`). The WebChannel wiring lives in PIWebView.qml
  (out of scope here) but the indirection is correct.
- **MultiEffect.maskSource Item type**: `Notification.qml:93-100` uses
  `MultiEffect` only as a `layer.effect` shadow (no `maskSource`), so the
  raw-Rectangle-mask SIGABRT trap does not apply.
- **Material attached props across Popup scope**: `Main.qml` re-asserts
  `Material.theme/accent/primary` inside every Drawer (`:196-198, 226-228, 260-262`) and `LoadedPluginsPage.qml:36-37` re-asserts on the Page mounted
  in the drawer — correct per CLAUDE.md.
- **Schema-as-source-of-truth**: the cached-snapshot reader/writer in
  `streamdock_catalog_fetcher.cpp:319-419` round-trips the documented JSON
  keys; the upstream `download`/`headUrl`/`deviceUuid` keys are read per
  PLUGIN-SDK.md, not aligned to C++ field names. No COD-031 `nlohmann::json`
  leak (all Qt JSON).
- **app-update 404 graceful handling**: `onLatestReplyFinished` treats any
  `reply->error() != NoError` (incl. 404) as `Status::Error` and logs
  (`:323-330`); the nightly tag 404 is explicitly non-fatal (`:379-385`);
  empty `tag_name` maps to `Status::Error` (`:353-357`). Graceful. (See WR-08
  for the 304-vs-dismissed-tag interaction, which is the only update-path
  defect.)
- **Cross-platform**: `qsizetype` used throughout `isNewerThan`
  (`app_update_service.cpp:263-266`) per the GCC `-Wconversion`/`-Werror`
  note; `platformLabel` covers macOS/Win/Linux/source branches. No `sprintf`
  / deprecated `_wgetenv` patterns in scope.
- **ASCII test names**: every `TEST_CASE` string in the four reviewed test
  files uses `-` / `->` only — no em-dash or right-arrow that would mangle
  through the Win32 CMD codepage. Confirmed in
  `test_sdplugin_extractor.cpp`, `test_sd_plugin_server.cpp`,
  `test_plugin_catalog_proxy_model.cpp`, `test_app_update_service.cpp`.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
