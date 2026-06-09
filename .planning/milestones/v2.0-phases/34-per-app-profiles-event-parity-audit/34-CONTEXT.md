# Phase 34: Per-App Profiles + Event-Parity Audit - Context

**Gathered:** 2026-06-08
**Status:** Ready for planning

<domain>
## Phase Boundary

Two themes:

1. **Per-app profiles (APROF):** a new `IActiveWindowWatcher` with platform-split backends emits
   debounced foreground-app changes; the active profile auto-switches via `Profile::applicationHints`
   (default-profile fallback), driving the bridge context lifecycle; a UI assigns profiles to app
   names; `applicationDidLaunch`/`applicationDidTerminate` fire to subscribed plugins.
1. **Event-parity audit (EVENT):** a coverage table in `docs/plugin-event-parity.md`; the `willAppear`
   payload is asserted complete; `systemDidWakeUp` + `switchToProfile` host-command dispatch land;
   controller-token normalization (`Knob`↔`Encoder`) is audited so no encoder/touch event is dropped.

Satisfies APROF-01..04, EVENT-01..04. Does NOT implement Windows plugin support (Phase 35),
security hardening (Phase 35), or the final milestone audit (Phase 35).

</domain>

<decisions>
## Implementation Decisions

### Window-watcher scope & Wayland strategy

- **All 4 platform backends behind `IActiveWindowWatcher`:** X11/EWMH, Windows/`GetForegroundWindow`,
  macOS/`NSWorkspace` (Obj-C++ `.mm`), Wayland/`zwlr-foreign-toplevel-management-v1`. Build all
  interfaces; Linux X11 + Wayland are live-verified here; Windows + macOS are compile-guarded +
  unit-tested (no live device on this machine).
- **Wayland = `zwlr-foreign-toplevel-management-v1`** (wlroots compositors: niri / Hyprland / sway).
  GNOME / KDE-without-wlr expose no public foreground API → the watcher degrades gracefully (NO crash,
  NO silent break) and the UI shows a capability-warning chip (APROF-03). Do NOT shell out to
  xdotool / hyprctl / swaymsg as the primary path.
- **Live-verify the Wayland backend on niri** — the dev machine runs niri (wlroots), which implements
  wlr-foreign-toplevel, so the Wayland path is actually exercised live (not just gated).
- **Debounced foreground-app changes** (~150–250ms) to avoid thrash on rapid focus changes.

### Auto-switch behavior + UI

- **Auto-switch matches the foreground app against `Profile::applicationHints`** (the field already
  exists on the Profile model); a default-profile fallback applies when no hint matches. Each switch
  drives the bridge context lifecycle: `willDisappear` for the outgoing profile's contexts, then
  `willAppear` for the incoming.
- **`applicationDidLaunch` / `applicationDidTerminate`** fire from the watcher to plugins that
  subscribe (APROF-04).
- **UI (APROF-03):** a surface to assign profiles to application names + the Wayland/GNOME
  capability-warning chip. Both debug-addressable via `objectName` (the warning chip's `qml.get`
  returns non-empty warning text).
- **Switch latency target: \<500 ms** device repaint (success criterion 1).

### Event-parity audit

- **EVENT-01:** commit `docs/plugin-event-parity.md` listing every OpenDeck `events/inbound` +
  `events/outbound` event + the Elgato SDK events, each marked supported / partial / missing /
  hardware-gated, with a test reference per supported event. Kept current as a verification deliverable.
- **EVENT-02:** a Catch2 test asserts the `willAppear` payload carries `action`, `context`, `device`,
  `event`, and `payload{row, column, controller, state, isInMultiAction}` (normalized controller).
- **EVENT-03:** `systemDidWakeUp` + the `switchToProfile` host-command dispatch are implemented +
  unit-tested (switchToProfile is already partly referenced at sd_plugin_server.cpp:424 — verify +
  complete).
- **EVENT-04:** audit controller-token normalization (`Knob`↔`Encoder`, etc.) against the OpenDeck /
  RE tokens so no encoder/touch event is silently dropped.

### Claude's Discretion

- The exact `IActiveWindowWatcher` interface shape, per-backend internals, debounce timing, the
  assign-profile UI layout, and the parity-table format are at the executor's discretion, consistent
  with existing patterns. Research the wlr protocol binding approach (Qt Wayland client) before
  committing to an implementation.

</decisions>

\<code_context>

## Existing Code Insights

### Reusable Assets

- `src/core/include/ajazz/core/profile.hpp` — `Profile::applicationHints` (std::vector<std::string>)
  ALREADY EXISTS; APROF-02 matches the foreground app against it.
- `src/app/src/sd_plugin_server.cpp:424` — `switchToProfile` is already partly referenced (EVENT-03
  verify + complete).
- `src/app/src/plugin_device_bridge.cpp` — the `willAppear` payload + the context lifecycle
  (willDisappear/willAppear) that the auto-switch must drive; EVENT-02 asserts the payload here.
- `src/app/src/property_inspector_controller.cpp` / the Application seam pattern — for routing new
  watcher events to plugins (mirror how PI-04 events route through Application to sendEvent).

### Established Patterns

- Platform-split via an interface + per-OS backends (mirror the device-backend split). Obj-C++ `.mm`
  for macOS NSWorkspace.
- sendEvent has NO event-name allowlist → new events (applicationDidLaunch/Terminate, systemDidWakeUp)
  are additive on the wire.
- Every new interactive control needs objectName; live debug-channel verification mandatory.
- The dev machine: niri/Wayland (XDG_CURRENT_DESKTOP=niri) — the Wayland backend is live-testable.

### Integration Points

- IActiveWindowWatcher (new) → profile auto-switch logic → ProfileController/PluginDeviceBridge
  context lifecycle.
- Watcher events → Application seam → SdPluginServer::sendEvent (applicationDidLaunch/Terminate).
- Assign-profile UI + warning chip in the QML editor/settings.
- docs/plugin-event-parity.md (new doc) cross-referenced to the SdPluginServer/PluginDeviceBridge events.

\</code_context>

<specifics>
## Specific Ideas

- Pending-todo (from STATE.md): a Wayland foreground-detection research sweep — wlr-foreign-toplevel
  compositor coverage map (wlroots vs GNOME/KDE gap). The research phase must produce the wlr binding
  approach (Qt Wayland client / wayland-scanner generated protocol) before implementation.
- X11/EWMH (`_NET_ACTIVE_WINDOW` + `_NET_WM_PID`/`WM_CLASS`), Windows (`GetForegroundWindow` +
  `GetWindowThreadProcessId`), macOS (`NSWorkspace.frontmostApplication`) are HIGH-confidence and can
  start without waiting on the Wayland research.
- The event-parity table's source of truth: OpenDeck `events/inbound`+`outbound` (ninjadev64/OpenDeck)
  - the Elgato SDK + the local RE corpus.

</specifics>

<deferred>
## Deferred Ideas

- Windows-only plugin support → Phase 35 (WINPLG).
- Plugin security hardening → Phase 35 (PLGSEC).
- Final milestone modularity audit (VERIF-01/02) → Phase 35.

</deferred>
