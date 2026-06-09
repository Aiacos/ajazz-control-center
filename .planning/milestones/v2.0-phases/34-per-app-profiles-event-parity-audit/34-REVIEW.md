---
phase: 34-per-app-profiles-event-parity-audit
reviewed: 2026-06-08T15:54:06Z
depth: standard
files_reviewed: 23
files_reviewed_list:
  - src/core/include/ajazz/core/active_window_watcher.hpp
  - src/core/src/active_window_watcher_stub.cpp
  - src/app/src/active_window_watcher_factory.hpp
  - src/app/src/active_window_watcher_factory.cpp
  - src/app/src/active_window_watcher_wayland.cpp
  - src/app/src/active_window_watcher_x11.cpp
  - src/app/src/active_window_watcher_win.cpp
  - src/app/src/active_window_watcher_mac.mm
  - src/app/src/active_window_debounce.hpp
  - src/app/src/app_event_dispatch.hpp
  - src/app/src/app_event_dispatch.cpp
  - src/app/src/application.cpp
  - src/app/src/application.hpp
  - src/app/src/profile_controller.hpp
  - src/app/src/profile_controller.cpp
  - src/app/src/sd_plugin_server.cpp
  - src/app/src/debug_control_facade.cpp
  - src/app/qml/SettingsPage.qml
  - tests/unit/test_active_window_watcher.cpp
  - tests/unit/test_switch_to_profile.cpp
  - src/core/CMakeLists.txt
  - src/app/CMakeLists.txt
  - packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml
findings:
  critical: 1
  warning: 5
  info: 3
  total: 9
status: issues_found
---

# Phase 34: Code Review Report

**Reviewed:** 2026-06-08T15:54:06Z
**Depth:** standard
**Files Reviewed:** 23
**Status:** issues_found

## Summary

Phase 34 adds the foreground-window watcher (Wayland/X11/Win32/macOS backends behind
a runtime factory), the per-app profile auto-switch, and the host-level
`applicationDidLaunch`/`applicationDidTerminate`/`switchToProfile` plugin event fan-out.

The security-critical contracts hold well: the COD-031 boundary is clean
(`active_window_watcher.hpp` is Qt-free and nlohmann-free, verified by grep); the
loopback-only WS bind is preserved; `applicationDidLaunch/Terminate` are delivered
ONLY to the bridge's registered-plugin set (never broadcast); the `switchToProfile`
token is auth-gated, length-bounded, trimmed, and treated purely as a lookup key;
the app-name UI input is bounded (256) and never eval'd/shelled; profile-id
sanitization blocks traversal. The shared debouncer implements the idempotent-switch
DoS guard correctly.

The one BLOCKER is an unhandled-X-error crash path in the X11 backend (a stale
window id between read and class-hint fetch reaches Xlib's default error handler,
which calls `exit()` — a remote-ish availability bug driven by ordinary window
churn). The warnings center on auto-switch behavioural correctness (the
"fall back to default profile on every unmapped focus change" clobbers manual
selection), the launch/terminate spec-semantics mismatch (fires on focus change,
not process lifecycle, and ignores `ApplicationsToMonitor`), and a couple of
resource/robustness gaps in the native backends.

## Critical Issues

### CR-01: X11 backend has no Xlib error handler — a stale window id crashes the whole app

**File:** `src/app/src/active_window_watcher_x11.cpp:148-222` (also `:68` XOpenDisplay)
**Issue:** The backend opens its own `Display` but never installs an `XSetErrorHandler`.
`emitCurrent()` reads `_NET_ACTIVE_WINDOW` to get a window id, then immediately calls
`XGetClassHint(display_, win, ...)` and `XGetWindowProperty(display_, win, ...)` /
`XFetchName(display_, win, ...)` on it. The active window can be destroyed by the WM
between the root-property read and these per-window calls (this is routine on
fast app close / alt-F4 / focus-steal). Xlib then delivers a `BadWindow` error, and
because no handler is installed the **default Xlib error handler prints to stderr and
calls `exit(1)`**, terminating the entire control-center process. This is an
availability bug reachable by ordinary, untrusted window churn — not a rare race.
Note X errors are asynchronous, so the failure may surface on a later Xlib call, but
the crash is the same.

**Fix:** Install a non-fatal error handler for this backend's display (ideally an
I/O error handler too), and treat `BadWindow`/`BadDrawable` as "window vanished,
keep current foreground" rather than fatal:

```cpp
// In the X11ActiveWindowWatcher constructor, after XOpenDisplay succeeds:
XSetErrorHandler(+[](Display*, XErrorEvent* e) -> int {
    // BadWindow/BadDrawable: the active window vanished between the root read
    // and the per-window query — non-fatal, just ignore (Pitfall 4 analog).
    AJAZZ_LOG_WARN("active_window", "X11 error code {} (non-fatal; window vanished)",
                   static_cast<int>(e->error_code));
    return 0; // never let Xlib's default handler exit() the process
});
```

(Xlib's error handler is process-global, so confirm it does not fight Qt's xcb
plugin — this backend runs only on an X11 session, where Qt uses its own xcb
connection, not this raw Xlib one, so a dedicated handler is appropriate. If global
collision is a concern, gate per-window calls with `XGrabServer`/existence checks,
or set the handler only while pumping.)

## Warnings

### WR-01: Auto-switch reverts to the default profile on every focus change to an unmapped app — clobbers manual selection

**File:** `src/app/src/profile_controller.cpp:383-387`, consumed at `src/app/src/application.cpp:1194-1207`
**Issue:** `resolveProfileForApp()` returns the device DEFAULT profile id (first by
name) whenever no `applicationHints` match. The auto-switch callback then activates
that default. Concretely: the user manually selects profile "Gaming"; they alt-tab to
a terminal that has no mapping; the resolver returns the default "Work" profile and
the device is force-switched to "Work", silently undoing the manual choice. Because
*most* foreground apps are unmapped, the practical effect is that the device is pinned
to the default profile and any manual switch survives only until the next focus change.
The idempotent guard at application.cpp:1200 does not help — it only suppresses a
re-switch when the resolved (default) profile is already active. This contradicts the
"auto-switch when a *mapped* app takes focus; otherwise leave the active profile
alone" intent stated in the SettingsPage copy ("Without a mapping, your default
profile stays active").

**Fix:** Distinguish "no hint matched" from "switch to default". For the
focus-driven auto-switch path, return empty (no-op) when no hint matches, so an
unmapped app leaves the active profile untouched:

```cpp
// resolveProfileForApp: drop the default fallback for the auto-switch caller.
// No hint matched -> return {} so the caller's idempotent guard no-ops and the
// user's current (possibly manually-chosen) profile is preserved.
return {};   // was: !fallbackId.isEmpty() ? fallbackId : QString::fromStdString(m_profile.id)
```

If a device-default fallback is genuinely wanted on *device connect* (not on every
focus change), drive that from `activateDeviceProfile()` instead, which already owns
that semantics.

### WR-02: applicationDidLaunch/Terminate fire on focus change, not process lifecycle, and ignore ApplicationsToMonitor

**File:** `src/app/src/application.cpp:1172-1187`
**Issue:** The Elgato/AJAZZ contract (`docs/architecture/PLUGIN-SDK.md:180`,
`akp_plugin_sdk.md:286`) defines `applicationDidLaunch`/`applicationDidTerminate` as
**OS process launch/terminate events for the apps in a plugin's `ApplicationsToMonitor`
manifest list**. This implementation instead emits `applicationDidTerminate(prev)` +
`applicationDidLaunch(new)` on every *foreground change*, to *every* registered plugin
regardless of its `ApplicationsToMonitor`. Consequences: (a) a plugin receives
"launch" for an app that was already running and merely gained focus, and "terminate"
for an app that is still running and merely lost focus — semantically wrong; (b) an
A→B→A focus bounce tells a plugin that A launched twice; (c) plugins that subscribe
to monitor only "obs.exe" get spammed with launch/terminate for every app the user
touches (information disclosure of the user's app-switching pattern to *every*
registered plugin, even though delivery is correctly registered-only). The code labels
this "best-effort", but the divergence is large enough to mislead real plugins.

**Fix:** Either (a) source these events from a real process-lifecycle watcher
(`PrepareForSleep`-style logind / Win32 process notifications / NSWorkspace
launch/terminate notifications) keyed on each plugin's `ApplicationsToMonitor`, or
(b) if focus-derived approximation is the accepted scope, filter delivery to only
those plugins whose `ApplicationsToMonitor` contains the appId, and document the
focus-vs-process approximation in the event payload/comment so plugins are not misled.
At minimum, gate the fan-out behind the per-plugin monitor list so it is not broadcast
to every registered plugin.

### WR-03: Wayland backend submits empty appId on activated toplevels that set no app_id

**File:** `src/app/src/active_window_watcher_wayland.cpp:126-142`
**Issue:** `recomputeFocus()` submits `ActiveWindowInfo{focused->appId().toStdString(), ...}`
for the first activated handle without checking that `app_id` is non-empty. Some
toplevels (splash surfaces, certain Electron/Java windows before they set `app_id`)
report an empty `app_id`. The X11 backend explicitly guards this
(`active_window_watcher_x11.cpp:135` returns when `appId.empty()`), but the Wayland
backend does not. An empty appId then flows to the auto-switch (which, combined with
WR-01's default fallback, force-switches to the default profile) and to the
launch/terminate fan-out (where it is caught by the `!appId.isEmpty()` guards, so the
last-foreground bookkeeping briefly records ""). Behaviour is inconsistent between the
two Linux backends.

**Fix:** Mirror the X11 guard — skip submitting when the focused handle has an empty
app_id:

```cpp
if (focused == nullptr) {
    return;
}
QString const appId = focused->appId();
if (appId.isEmpty()) {
    return; // no usable identity token yet; keep current foreground (Pitfall 4)
}
debounce_->submit(ActiveWindowInfo{appId.toStdString(), focused->title().toStdString()});
```

### WR-04: Win32 backend deduplicates on HWND only — a process re-using a window or fast hwnd reuse can suppress/duplicate a change

**File:** `src/app/src/active_window_watcher_win.cpp:91-96`
**Issue:** `poll()` short-circuits when `hwnd == lastHwnd_`. HWND values are recycled
by Windows after a window is destroyed, so a different application can be assigned a
previously-seen HWND; the poll would then treat a genuine foreground change as a
no-op and miss the switch. Conversely, the dedup is purely on HWND, so it never
collapses two distinct windows of the *same* app to one identity change (minor). The
debouncer downstream only dedups on appId, but it never receives the suppressed event
in the first place. This backend is compile-only (no Windows hardware here), so it
won't be caught by the Linux test suite — it must be reasoned about, not assumed.

**Fix:** Dedup on the resolved appId (the identity contract) rather than the raw
HWND, or in addition to it. Resolve the image name first, compare against a stored
`lastAppId_`, and only then submit:

```cpp
// after computing appId:
if (appId == lastAppId_) { return; }
lastAppId_ = appId;
debounce_->submit(...);
```

Keep the HWND check only as a cheap fast-path, not as the sole identity.

### WR-05: X11 window title read can truncate multi-property UTF-8 and silently drop \_NET_WM_NAME longer than 4 KB

**File:** `src/app/src/active_window_watcher_x11.cpp:198-213`
**Issue:** `XGetWindowProperty` is called with `long_length = 1024` (1024 * 32-bit =
4096 bytes) and the returned data is treated as a C string. If `_NET_WM_NAME` is
longer than 4096 bytes, the property is silently truncated (and `bytesAfter` is
ignored). More importantly, the result is read as a NUL-terminated C string
(`reinterpret_cast<char const*>(prop)`) but `_NET_WM_NAME` (UTF8_STRING) is not
guaranteed NUL-terminated by the protocol — Xlib appends one extra zero byte for the
caller's convenience, so this happens to be safe in practice, but the code does not
bound the read by `nItems`, so an embedded NUL truncates the title. Title is
diagnostic-only (the matcher keys on appId), so impact is low, but the unbounded
C-string read is fragile.

**Fix:** Bound the title by the returned item count and request the full property
length when needed:

```cpp
if (... == Success && prop != nullptr) {
    title.assign(reinterpret_cast<char const*>(prop), nItems); // bound by nItems, not strlen
    XFree(prop);
}
```

(Title is best-effort; truncation to 4 KB is acceptable, but read it length-bounded.)

## Info

### IN-01: macOS notification block captures `this` and may run after stop() if already queued

**File:** `src/app/src/active_window_watcher_mac.mm:85-92`
**Issue:** The `addObserverForName:...usingBlock:` block captures `this` raw and is
dispatched on `NSOperationQueue.mainQueue`. `removeObserver()` (called from `stop()`
and the destructor) prevents *new* deliveries, but a block already enqueued on the
main queue at the moment of removal could still run against a destroyed object.
Practically the watcher lives for the whole app session and removal happens on the
same (main) thread that drains the queue, so the window is essentially nil — hence
Info, not a Warning.

**Fix:** Capture a `__weak`-style guard or a `std::weak_ptr` self-token, or assert
the single-thread invariant in a comment; if ever made movable/short-lived, this
becomes a real UAF.

### IN-02: `m_lastForegroundApp` member is declared unconditionally but used only under AJAZZ_HAVE_WEBSOCKETS

**File:** `src/app/src/application.hpp:332`, used at `src/app/src/application.cpp:1178-1185`
**Issue:** `m_lastForegroundApp` is only read/written inside the
`#ifdef AJAZZ_HAVE_WEBSOCKETS` block in the auto-switch callback. In a build with
WebSockets disabled the member is dead state. Harmless (a QString member, not a
-Werror unused-variable), but it is unused dead data in that configuration.

**Fix:** Wrap the member declaration in the same `#ifdef AJAZZ_HAVE_WEBSOCKETS`
guard as its sole consumer, or add a brief comment that it is WS-only.

### IN-03: Duplicated DeviceLookup lambda repeated ~7 times in the Application constructor

**File:** `src/app/src/application.cpp:108-243` (TimeSync, Lighting, Settings, Battery, Firmware, StreamDockControl)
**Issue:** The identical `codename -> shared_ptr<IDevice>` enumerate-and-open lambda is
copy-pasted into six service constructions. Not introduced by Phase 34, but it sits in
the file under review and is a maintenance hazard (a fix to the lookup must be applied
in six places). Out of v1 performance scope; flagged only as a duplication/quality
note.

**Fix:** Extract a single private `Application::deviceByCodename(QString)` member (or a
local `auto lookup = [this](QString const&){...};`) and pass it to each service.

______________________________________________________________________

_Reviewed: 2026-06-08T15:54:06Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
