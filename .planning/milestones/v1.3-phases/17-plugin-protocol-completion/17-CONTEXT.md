# Phase 17: Plugin Protocol Completion - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp_plugin_sdk.md (the SDK spec) + sd_plugin_server survey

<domain>
## Phase Boundary

Phase 17 completes the **Elgato Stream Deck v6-compatible protocol** on the existing
`SdPluginServer` so any Elgato/Mirabox plugin's full message surface is honored. The server
today (sd_plugin_server.cpp, 257 LoC) is LocalHost-bound, does the `registerPlugin` handshake,
recognizes the **13 standard actions** (→ `actionReceived`), and explicitly does NOT yet handle
the AJAZZ extensions (line 238), has **no `passHello`/auth**, and has **no host→plugin event
sender**. This phase closes those three gaps. It is device-agnostic and plans/verifies against a
loopback WebSocket test client — **no hardware, no device wiring** (that is Phase 19).

**Delivers (PLUGIN-01/02/03/04/05):**

- The WS server stays **`QHostAddress::LocalHost`-only** on a random free port, loopback
  invariant test-pinned; the JSON envelope (`event`/`context`/`device`/`action`/`payload` +
  `controller`/`coordinates`/`ticks`/`pressed`) round-trips every supported message (PLUGIN-01/02).
- The **26 AJAZZ-only actions** are routed (no longer "unhandled") on top of the 13 standard (PLUGIN-03).
- All host→plugin **events** can be SENT to a registered plugin's socket — incl. the encoder
  events `dialDown`/`dialUp`/`dialRotate` (+ legacy `keyDownCord`) (PLUGIN-04).
- The **`passHello` + salt/challenge auth** handshake is implemented (PLUGIN-05).

**Out of scope:** plugin process spawn / manifest / discovery (Phase 18); the device↔plugin
bridge that connects `actionReceived`→device and device-input→`sendEvent` (Phase 19); Property
Inspector (Phase 20); built-in actions (Phase 21). Phase 17 builds the protocol surface + the
`sendEvent` seam that Phase 19 calls.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Architecture

- **Extend the existing `src/app/src/sd_plugin_server.{hpp,cpp}`** — do NOT create a
  `src/host/plugin-host/` module (the RE doc's speculative path). nlohmann::json may be used
  PRIVATE in the app/plugin layer only; the server already uses `QJsonObject` (prefer it here).
- Keep the **LocalHost-only** bind invariant (already present + test-pinned) — never `Any`.

### Protocol surface (PLUGIN-02/03 — source of truth: `docs/protocols/streamdeck/akp_plugin_sdk.md`)

- Implement the JSON envelope per §4.2 and route ALL plugin→host actions per §4.3 — the 13
  standard PLUS the **26 AJAZZ-only**: `setBG`, `setBackground`, `clearIcon`, `sendToDevice`,
  `openTouchbarSecondaryMenu`/`exitTouchbarSecondaryMenu`, `enterGatheringEvent`,
  `registrationScreenSaverEvent`/`unRegistrationScreenSaverEvent`, `setText`, `setFeedback`,
  `lockScreen`/`unLockScreen`, `getScreenshot`, `getSystemAudioVolume`, `getUserInfo`,
  `setAcImgTop`, `onSwitchToFolderProfile`/`onSwitchFromFolderProfile`, `deleteAction`,
  `stopBackground`, `exitFullScreen`, `touchTap`, `getDetectedSensorsData`,
  `startAudioCapture`/`stopAudioCapture`, `sendUserInfo`.
- **Routing vs fulfillment:** Phase 17 PARSES + ROUTES every message (typed handling or a routed
  `actionReceived`/typed signal) so **nothing falls through to `unhandledEventReceived`**. Actions
  needing a host capability not yet built (`getScreenshot`, `getSystemAudioVolume`, `getUserInfo`,
  `getDetectedSensorsData`) return a structured/empty ack or a logged stub — full fulfillment is
  owned by later phases. Device-targeting actions (`setImage`/`setTitle`/`setState`/`setBG`/
  `setFeedback`/`setText`) continue to surface via signals for the Phase-19 bridge.

### Host→plugin events (PLUGIN-04)

- Add a `sendEvent(pluginUuid/context, eventName, payload)` surface that writes a JSON text frame
  to the right plugin socket. Cover §4.4: `keyDown`/`keyUp`, `dialDown`/`dialUp`/`dialRotate`
  (+ legacy `keyDownCord`/`keyUpCord`), `touchTap`, `willAppear`/`willDisappear`,
  `deviceDidConnect`/`deviceDidDisconnect`, `applicationDidLaunch`/`applicationDidTerminate`,
  `titleParametersDidChange`, `systemDidWakeUp`, `didReceiveSettings`/`didReceiveGlobalSettings`,
  `sendToPlugin`/`sendToPropertyInspector`. This is the seam Phase 19 calls with real device input.

### Auth (PLUGIN-05 — §4.5)

- After `registerPlugin`, the host sends **`passHello`** with `{device, deviceInfo, authentication:{challenge, salt}}` (salt random per connection). When a password is configured,
  the plugin replies `{event:"authentication", challenge: sha256(password+salt)}`; the host
  verifies and **rejects after N (e.g. 5) bad attempts** (close the socket). Default (no password)
  = passHello sent, connection accepted (loopback-only by design; **no TLS**).

### Claude's Discretion (planner/executor)

- Typed signals per action-class vs a single routed `actionReceived(uuid, msg)` the app
  switches on — your call; keep it the seam Phase 19 consumes.
- Where the `sendEvent` API lives (method on `SdPluginServer`) and the context→socket lookup.
- Salt/challenge storage + attempt-counter location.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp_plugin_sdk.md` — **the spec.** §2 manifest (Phase 18), §3 lifecycle (Phase 18), §4.1 servers (LocalHost), §4.2 envelope, §4.3 plugin→host actions (13 + 26), §4.4 host→plugin events, §4.5 auth handshake, §7 Property Inspector (Phase 20), §9 corrections table (test names like `PluginAuthTest::rejectsAfter5BadAttempts`, `SDPluginProtocolTest::roundTrip_setImage…`).
- `src/app/src/sd_plugin_server.{hpp,cpp}` — the server to extend (LocalHost bind, `registerPlugin` handshake, 13-action dispatch at sd_plugin_server.cpp:212, `actionReceived`/`pluginRegistered`/`unhandledEventReceived` signals, `uuidForClient`).
- `docs/protocols/streamdeck/akp05_init_sequence.md` §5/§6 — the security posture (bind LocalHost, JS shim hardcodes 127.0.0.1, auth challenge-response) the server already honors.
- CLAUDE.md — COD-031 (nlohmann PRIVATE to app/plugin only); never skip pre-commit; ASCII test names.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free and device-free: drive a `QWebSocket` (or `QWebSocketServer`
  loopback) test client. Assert: (1) server binds LocalHost on a random port (loopback invariant
  test-pinned); (2) `registerPlugin` → `passHello` with a salt; (3) auth: correct
  `sha256(password+salt)` accepted, wrong rejected, socket closed after N attempts; (4) each of
  the 13+26 actions routes without hitting `unhandledEventReceived`; (5) each host→plugin event
  arrives at the client with the correct envelope (e.g. `dialRotate` carries `ticks`/`pressed`/
  `controller`); (6) a real/sample `.sdPlugin`'s JS shim sequence (`registerPlugin`→`passHello`)
  completes. Mirror the §9 target test names where sensible. ASCII-only test names.

</specifics>

<deferred>
## Deferred Ideas

- Plugin process spawn / manifest parse / discovery / lifecycle / Mirabox shim → Phase 18.
- Device↔plugin bridge (actionReceived→device; device input→sendEvent; setImage e2e) → Phase 19.
- Property Inspector + settings persistence → Phase 20.
- Built-in in-process actions (page/profile nav, hotkey, OBS, …) → Phase 21.
- Host-capability fulfillment for getScreenshot/getSystemAudioVolume/getUserInfo/getDetectedSensorsData → the phases that own those capabilities.

</deferred>

______________________________________________________________________

*Phase: 17-plugin-protocol-completion*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; akp_plugin_sdk.md is the spec)*
