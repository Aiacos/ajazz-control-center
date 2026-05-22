---
phase: 13-catalogue-v1-1-ui-verifies-back-fill
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 46
files_reviewed_list:
  - src/app/src/app_update_service.cpp
  - src/app/src/app_update_service.hpp
  - src/app/src/application.cpp
  - src/app/src/application.hpp
  - src/app/src/autostart_service.cpp
  - src/app/src/battery_service.cpp
  - src/app/src/battery_service.hpp
  - src/app/src/device_model.cpp
  - src/app/src/device_model.hpp
  - src/app/src/firmware_update_service.cpp
  - src/app/src/firmware_update_service.hpp
  - src/app/src/lighting_service.cpp
  - src/app/src/lighting_service.hpp
  - src/app/src/plugin_catalog_model.cpp
  - src/app/src/plugin_catalog_model.hpp
  - src/app/src/sd_plugin_server.cpp
  - src/app/src/sd_plugin_server.hpp
  - src/app/src/sdplugin_extractor.cpp
  - src/app/src/sdplugin_extractor.hpp
  - src/app/src/settings_service.cpp
  - src/app/src/settings_service.hpp
  - src/app/src/streamdock_catalog_fetcher.cpp
  - src/app/src/time_sync_service.cpp
  - src/app/src/time_sync_service.hpp
  - src/app/qml/DeviceList.qml
  - src/app/qml/LoadedPluginsPage.qml
  - src/app/qml/Main.qml
  - src/app/qml/PluginStore.qml
  - src/app/qml/ProfileEditor.qml
  - src/app/qml/PropertyInspector.qml
  - src/app/qml/RgbPicker.qml
  - src/app/qml/SettingsPage.qml
  - src/app/qml/SettingsRow.qml
  - src/app/qml/Theme.qml
  - src/app/qml/components/BatteryIndicator.qml
  - src/app/qml/components/DeviceImage.qml
  - src/app/qml/components/DeviceRow.qml
  - src/app/qml/components/FirmwarePanel.qml
  - src/app/qml/components/Notification.qml
  - src/app/qml/components/Toast.qml
  - src/app/qml/components/UpdateBanner.qml
  - tests/unit/test_app_update_service.cpp
  - tests/unit/test_battery_service.cpp
  - tests/unit/test_firmware_update_service.cpp
  - tests/unit/test_profile_serialization.cpp
  - tests/unit/test_settings_service.cpp
findings:
  critical: 2
  warning: 8
  info: 6
  total: 16
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 46
**Status:** issues_found

## Summary

Reviewed the Qt 6 / QML application layer and its C++ services touched during the v1.2 milestone. The codebase is generally careful — the QML_SINGLETON / static_assert Pitfall-4 lock is consistently applied, the loopback-only SdPluginServer invariant is intact, the DeviceLookup UAF discipline (pin shared_ptr in a local) is followed everywhere, and the autostart Desktop-Exec quoting is correct. However adversarial tracing surfaced two BLOCKER-class defects: a QML binding that references a non-existent `Theme.materialTheme` property (the Loaded-Plugins drawer renders with wrong/undefined Material theme), and a `.sdPlugin` archive extractor that writes attacker-controlled archive paths without any zip-slip path-traversal guard (the same archives are downloaded over the network in `install()`). Eight WARNING-class issues cover silent-failure / state-machine edge cases in the update, catalog and battery paths.

## Critical Issues

### CR-01: Path-traversal (zip-slip) in `.sdPlugin` archive extraction

**File:** `src/app/src/sdplugin_extractor.cpp:51-69`
**Issue:** `extractSdPluginArchive` iterates `zip.fileInfoList()` and writes each entry to `tmpPath + "/" + info.filePath` with no validation that the resulting path stays inside `tmpPath`. A malicious archive entry named `../../../../home/user/.bashrc` (or an absolute path on platforms where Qt does not normalise it) escapes the staging directory and overwrites arbitrary files with the current user's permissions. This is reachable from the network: `PluginCatalogModel::install` (`plugin_catalog_model.cpp:526`) downloads a `.sdPlugin` over HTTPS from `cdn1.key123.vip` and feeds it straight into this extractor, and `extractStandalonePluginArchives` runs the same path on every launch over any file dropped in the plugins dir. The comment at line 70 only addresses symlinks, not traversal via `..` components in regular file entries.
**Fix:** Reject or skip any entry whose normalised destination escapes the staging root before opening the output file:

```cpp
QString const outPath = QDir::cleanPath(tmpPath + QStringLiteral("/") + info.filePath);
QString const rootCanon = QDir::cleanPath(tmpPath) + QStringLiteral("/");
if (!outPath.startsWith(rootCanon)) {
    AJAZZ_LOG_WARN("plugin-catalog",
                   "extract '{}': rejecting entry escaping staging dir: '{}'",
                   archivePath.toStdString(), info.filePath.toStdString());
    extractOk = false;
    break;
}
```

Also reject entries containing a leading `/` or a Windows drive prefix.

### CR-02: `LoadedPluginsPage.qml` binds to a non-existent `Theme.materialTheme` property

**File:** `src/app/qml/LoadedPluginsPage.qml:31`
**Issue:** `Material.theme: Theme.materialTheme` references a property that does not exist on the `Theme` singleton (`Theme.qml` defines no `materialTheme`; the real `materialTheme` lives on `Main.qml`'s `root`, line 48). At runtime this binding resolves to `undefined`, so `Material.theme` falls back to its default (Light) regardless of `ThemeService.effectiveMode`. Per the project's documented v1.0 light-theme bug class, this produces Light Material chrome (dark/black text) on the dark BrandingService surface inside the Loaded-Plugins drawer — exactly the invisible-text failure mode CLAUDE.md warns about. The sibling drawers in `Main.qml` correctly bind `root.materialTheme`; this page was missed. qmllint with `pragma ComponentBehavior: Bound` should flag the unqualified/missing property.
**Fix:** Pass the resolved theme from the host, matching the Main.qml drawers. Either bind through a property on the page set by the drawer, or use `ThemeService.effectiveMode`:

```qml
Material.theme: ThemeService.effectiveMode === "light" ? Material.Light : Material.Dark
```

(LoadedPluginsPage is mounted inside `loadedPluginsDrawer`, which already sets `Material.theme: root.materialTheme` on the Drawer — but Material attached props don't cross the Popup→child boundary cleanly for a Page that re-asserts its own `Material.theme`, so set it explicitly here.)

## Warnings

### WR-01: Update banner re-fires for a dismissed tag if a 304 follows the dismiss

**File:** `src/app/src/app_update_service.cpp:316-321`
**Issue:** `dismissCurrentUpdate()` sets status to `Idle` and persists the dismissed tag, but it does not clear `m_latestVersion`. On the next check that returns `304 Not Modified`, the handler does `setStatus(m_latestVersion.isEmpty() ? UpToDate : UpdateAvailable)` — since `m_latestVersion` is still the dismissed tag, the banner re-surfaces even though the user clicked "Later" and nothing changed upstream. The dismissed-tag gate in `applyRelease` is bypassed because the 304 path never calls `applyRelease`.
**Fix:** On the 304 path, honour the dismissed tag before flipping to `UpdateAvailable`:

```cpp
QSettings settings;
QString const dismissed = settings.value(QString::fromLatin1(kDismissedTagKey)).toString();
if (httpStatus == 304) {
    if (m_status == Status::Checking) {
        bool const newer = !m_latestVersion.isEmpty()
            && isNewerThan(m_latestVersion, currentVersion())
            && m_latestVersion != dismissed;
        setStatus(newer ? Status::UpdateAvailable : Status::UpToDate);
    }
    return;
}
```

### WR-02: Nightly download URL is never carried into the banner

**File:** `src/app/src/app_update_service.cpp:363-377`
**Issue:** In the nightly-enabled path, `onLatestReplyFinished` calls `applyRelease(tag, notes, url)` with the *stable* release data, then chains the nightly fetch. `onNightlyReplyFinished` (line 401) only re-applies if `isNewerThan(tag, m_latestVersion)`. Because the stable record was already applied to `m_latestVersion`, and nightly is always treated as "newer than stable" by `isNewerThan`, the nightly correctly wins — but the intermediate `applyRelease(stable)` already emitted `UpdateAvailable` and the banner can flash the stable tag/URL before the nightly reply lands. If the nightly request fails (line 381-384), the banner is left pointing at the stable release even though the user opted into nightly. The transient stable-then-nightly flip is observable in QML bindings.
**Fix:** When `m_includeNightly`, defer the `setStatus(UpdateAvailable)` decision until after the nightly reply resolves (or fails); apply the stable record's fields silently but only transition status once.

### WR-03: `StreamdockCatalogFetcher` can wedge in `Loading` forever if a page never finishes

**File:** `src/app/src/streamdock_catalog_fetcher.cpp:507-510, 547-579`
**Issue:** `refresh()` no-ops while `m_state == State::Loading` (re-entry guard). The only transitions out of `Loading` happen inside `onPageFinished`. But `fetchPage` returns early without scheduling any reply when the URL is invalid (line 549-553) — e.g. a malformed `ACC_STREAMDOCK_CATALOG_URL` override or empty scheme — leaving `m_state` pinned at `Loading` with no in-flight reply to ever call `onPageFinished`. The fetcher is then permanently wedged: every subsequent `refresh()` (including the PluginStore "Retry" button) is silently dropped, and the QML banner stays on "loading" indefinitely.
**Fix:** In `fetchPage`, on the invalid-URL early return, reset state out of `Loading`:

```cpp
if (!url.isValid() || url.scheme().isEmpty()) {
    qCWarning(lcStreamdock) << "fetchPage aborted — invalid URL:" << url.toString();
    if (m_state == State::Loading) {
        m_state = m_accumulated.empty() ? State::Offline : State::Cached;
        emit stateChanged(m_state);
    }
    return;
}
```

### WR-04: Plugin install writes the full network body to disk with no size cap

**File:** `src/app/src/plugin_catalog_model.cpp:507-515`
**Issue:** `install()` reads the entire reply (`reply->readAll()`) and writes it to `<userPluginsDir>/<id>.sdPlugin` with no upper bound on the downloaded size. A hostile or compromised CDN (`cdn1.key123.vip`, which the code follows redirects to per line 464-466) can stream an arbitrarily large body and fill the user's home filesystem before the write completes. Combined with CR-01 the same body is then extracted. There is also no Content-Type / magic-byte check before treating the blob as a zip.
**Fix:** Enforce a sane maximum (e.g. 64 MB) by inspecting `QNetworkRequest::ContentLengthHeader` up front and aborting in the `downloadProgress` slot when `received` exceeds the cap; reject the install if the body's first bytes are not the `PK\x03\x04` zip magic.

### WR-05: `RgbPicker` sliders push HID writes on initial value assignment

**File:** `src/app/qml/RgbPicker.qml:84-95, 106-116`
**Issue:** Both firmware sliders set an initial `value: Math.min(3, root.firmwareBrightnessMax)` AND have an `onValueChanged` handler that calls `LightingService.setMode(...)`. In QML, the initial value assignment fires `onValueChanged`, so simply opening the RGB tab for an `IFirmwareLightingCapable` device issues two unsolicited HID `setFirmwareLightingMode` writes (one per slider) before the user touches anything — using `firmwareModeBox.currentValue`, which may itself still be defaulting. This can clobber the device's current effect on tab open.
**Fix:** Gate the writes behind a "user has interacted" flag, or use `onMoved` (user-driven only) instead of `onValueChanged` for the slider handlers, mirroring the ComboBox's `onActivated` (which already only fires on user action).

### WR-06: `SettingsRow` Apply uses unclamped slider value; relies on backend clamp only

**File:** `src/app/qml/SettingsRow.qml:320-326`
**Issue:** `onSnapshotChanged` / `Component.onCompleted` set `responseSlider.value = snapshot.responseLevel`, but the slider's `from: 1; to: 5`. When the device is absent the snapshot returns `responseLevel: 3` (fine), but if a future backend ever returns 0 (the documented "vendor default sentinel"), the slider clamps the displayed value to 1 while `Apply` sends `Math.round(responseSlider.value)` = 1, silently changing the persisted value from "default" to "level 1". The QML and the `clampResponseLevel` C++ contract (0 → 3) disagree at the boundary.
**Fix:** Map the snapshot's 0-sentinel to 3 before seeding the slider, so the UI and `SettingsService::clampResponseLevel` agree:

```qml
responseSlider.value = snapshot.responseLevel === 0 ? 3 : snapshot.responseLevel;
```

### WR-07: `connectedPluginCount` and disconnect bookkeeping leave dead slots in `m_connections`

**File:** `src/app/src/sd_plugin_server.cpp:140-159`
**Issue:** `onClientDisconnected` sets `it->socket = nullptr` but never erases the `PluginConnection` entry from `m_connections`. Over a long session with many plugin connect/disconnect cycles, the vector grows unboundedly with dead `{uuid, nullptr}` entries. `connectedPluginCount` filters them out (`c.socket != nullptr`), so the count stays correct, but `uuidForClient` and the `registerPlugin` lookup do a linear scan over an ever-growing vector, and a disconnected-then-reconnected plugin with the same UUID will have two entries. Not a crash, but a slow leak + correctness smell.
**Fix:** Erase the entry instead of nulling it:

```cpp
m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
    [client](auto const& c){ return c.socket == client; }), m_connections.end());
```

(Capture `uuid` before erasing, as the code already does.)

### WR-08: Battery indicator never collapses a stale reading when the row goes offline

**File:** `src/app/qml/components/BatteryIndicator.qml:58, 168-186`
**Issue:** The chip's `visible: percent >= 0 && !unavailable` only clears on an explicit `batteryUnavailable` signal. `BatteryService::doQuery` emits `batteryUnavailable` when the lookup returns null (device disconnected) — but only if a poll actually fires for that codename. When `pollEnabled` is false (user opted out) and a device is unplugged, no further query runs, so the indicator keeps showing the last cached percent (e.g. "85%") for a device that is no longer present. The DeviceRow keeps the chip mounted for offline battery-capable devices by design (DeviceRow.qml:142-151), so the stale value persists visibly.
**Fix:** Have `BatteryIndicator` also watch the row's connected state (pass `deviceConnected` in from DeviceRow) and reset `percent = -1` when it goes false, independent of the poll timer.

## Info

### IN-01: `streamdockProductId` snapshot round-trip drops the `downloadUrl`

**File:** `src/app/src/streamdock_catalog_fetcher.cpp:381-419, 332-364`
**Issue:** `serialiseSnapshot` writes 14 fields but omits `downloadUrl`; the cached-snapshot reader path (lines 337-358) likewise never reads it. After a live fetch is cached and reloaded on next launch, every cached row loses its direct `downloadUrl`, so `install()` silently falls back to the open-in-browser bridge instead of the in-app HTTPS download — a quiet capability regression that only manifests across an app restart.
**Fix:** Add `{"downloadUrl", e.downloadUrl.toString()}` to `serialiseSnapshot` and parse it back in the `rows` branch of `parseUpstreamJson`.

### IN-02: `humaniseSize` unit-index can read past the array on absurd inputs

**File:** `src/app/src/streamdock_catalog_fetcher.cpp:201-214`
**Issue:** `kUnits` has 4 entries (B/KB/GB) and the loop caps `unit < 3`, so `unit` maxes at 3 — safe today. But the cap is a magic `3` decoupled from `std::size(kUnits)`; a future edit to `kUnits` without updating the literal re-arms an out-of-bounds read.
**Fix:** Use `static_cast<int>(std::size(kUnits)) - 1` instead of the literal `3`.

### IN-03: `parseVersion` swallows non-numeric components as 0

**File:** `src/app/src/app_update_service.cpp:94-98`
**Issue:** `p.toInt(&ok)` failures append `0` to the components, so a tag like `1.x.3` compares equal to `1.0.3`. Probably fine for GitHub release tags, but a malformed tag silently ranks as a real version rather than being rejected. Low impact (notify-only), worth a comment at minimum.
**Fix:** Document the lenient behaviour, or treat a non-numeric component as a parse failure that ranks the tag as not-newer.

### IN-04: `DeviceList` scroll-visibility relies on a brittle child-count heuristic

**File:** `src/app/qml/DeviceList.qml:61`
**Issue:** `visible: rows.children.length > 1` assumes the Repeater contributes exactly one non-delegate child. This is true today but couples the empty-state logic to an internal QML implementation detail (Repeater child accounting); adding any sibling item to the `ColumnLayout` silently breaks the empty-state predicate.
**Fix:** Bind to the model count instead: `visible: root.model && root.model.count > 0` (DeviceModel exposes rowCount via the model; or expose a count property).

### IN-05: Magic timeout / interval literals scattered without named constants in QML

**File:** `src/app/qml/PluginStore.qml:198, 340`; `src/app/qml/components/Notification.qml:119`
**Issue:** `interval: 30000` (relative-age tick) and `interval: root.dwellMs` (3000 default) are inline literals duplicated across the two banners. Minor maintainability smell; the two banners are otherwise copy-paste duplicates (Streamdock vs OpenDeck) of ~120 lines each.
**Fix:** Factor the banner into a shared component parameterised by source state/timestamp, and lift the tick interval to a Theme token.

### IN-06: `PluginCatalogModel::install` re-uses `m_install[uuid]` operator[] which inserts default state

**File:** `src/app/src/plugin_catalog_model.cpp:411`
**Issue:** `auto& state = m_install[uuid];` default-inserts an `InstallState{false,false}` for any uuid that passes the `findRow` check, even when the subsequent download fails — leaving a spurious not-installed entry in the side-map. The reconcile in `replace*Rows` keeps it bounded to catalogue rows, so it is not a true leak, but it muddies `installedCount`-adjacent logic if the semantics ever change to "has an install record".
**Fix:** Use `find` and only insert on actual state change, or accept the current behaviour with a comment.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
