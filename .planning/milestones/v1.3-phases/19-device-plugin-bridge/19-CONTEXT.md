# Phase 19: Device ↔ Plugin Bridge (setImage end-to-end) - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp_plugin_sdk.md §5 + existing-code survey

<domain>
## Phase Boundary

Phase 19 is the **convergence point** of the milestone: it connects the plugin protocol
(Phase 17) to the device (Phases 14-15) so a plugin paints a physical key and a physical
press/turn reaches the plugin. This is the **first demoable plugin↔device round-trip**.

**Delivers (PLUGIN-10):**

- **`setImage` end-to-end:** an inbound `setImage` action → strip the `data:image/...;base64,`
  prefix → `QImage::loadFromData` → scale to the device's per-key dims via the existing
  `image_pipeline` → JPEG → the **Phase-14 control service** paint path → the physical key.
  On decode failure, a placeholder (no crash) per spec §5.
- **Outbound action routing:** `actionReceived` for `setImage`/`setTitle`/`setState`/`setBG`/
  `setFeedback`/`setText` resolves the action's `context` to a device target and drives it via
  the control service.
- **Inbound event routing:** device input (the Phase-15 `DeviceEvent`s — `keyDown`/`keyUp`,
  encoder `dialRotate`/`dialDown`/`dialUp`, `touchTap`) → the **Phase-17 `sendEvent`** → the
  plugin bound to that key/encoder, with the correct `context`/`coordinates` envelope.
- **Lifecycle:** `willAppear`/`willDisappear` as a profile page shows/hides an action;
  `deviceDidConnect`/`deviceDidDisconnect` on device arrival/removal.

**Out of scope:** Property Inspector (Phase 20); built-in in-process actions (Phase 21); plugin
store (Phase 22); auxiliary surfaces (Phase 23). Phase 19 routes plugin actions to the **keys**
(and encoders where the binding exists); rich aux-surface targeting is Phase 23.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Architecture — the bridge is the integration spine

- A new app-layer component (e.g. `PluginDeviceBridge`) **composes** the existing seams; it does
  NOT reimplement them: the Phase-17 `SdPluginServer` (`actionReceived` signal in, `sendEvent`
  out), the Phase-14 control service (key paint path), the Phase-15 input service (`DeviceEvent`
  source), and the active `Profile` (the binding → action-UUID map).
- **The bridge owns the `context` registry:** `context` (opaque id) ↔ (deviceId, pageId,
  coordinates {row,column}, action UUID). Populate it as the active profile's keys/encoders bind
  plugin actions; `willAppear` is emitted when an action's page becomes visible (carrying the
  context), and the plugin echoes that `context` back in `setImage`/`setTitle`/etc. — the bridge
  resolves it to the key and paints.

### setImage pipeline (PLUGIN-10 — spec §5; reuse, do not add a second encode path)

- Strip `data:image/{png,jpg};base64,` → `QByteArray::fromBase64` → `QImage::loadFromData` →
  scale to per-key dims → encode via the existing `image_pipeline` (ARCH-04) → control-service
  paint (`BAT`→1024-chunks→`ULEND`). `target` ∈ {0=hw+sw,1=hw,2=sw} per §4.3. COD-031 (QJson/
  QImage app-layer; no nlohmann in core).

### Inbound events → plugin

- Map the Phase-15 `DeviceEvent` to the §4.4 envelope: key press/release → `keyDown`/`keyUp`
  with `coordinates`; encoder turn → `dialRotate` (`ticks`/`pressed`/`controller`); encoder press
  → `dialDown`/`dialUp` (legacy `keyDownCord`/`keyUpCord` alias); touch → `touchTap`. Route only
  to the plugin that owns the bound action at that coordinate (via the context registry).

### Claude's Discretion (planner/executor)

- Exact bridge class name + where it is constructed (Application composition root, after the
  server + control + input services).
- Context-id scheme (UUID vs encoded tuple) and the registry data structure.
- Whether `willAppear` fires only for the active page (Phase-16 pages enrich this) or all bound
  actions on connect — pick the spec-faithful behavior (per-visible-action).
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp_plugin_sdk.md` §5 (setImage end-to-end), §4.3 (action payloads incl. `target`, setBG/setFeedback/setText), §4.4 (keyDown/dialRotate/willAppear/deviceDidConnect envelopes incl. `coordinates`/`ticks`/`pressed`/`controller`).
- `.planning/phases/17-plugin-protocol-completion/17-01-PLAN.md` + `17-02-PLAN.md` — `actionReceived` signal + `sendEvent(uuid, eventName, payload)` seam (read 17-02-SUMMARY for the exact signature; STOP if Phase 17 unexecuted).
- `.planning/phases/14-stream-dock-control-service/14-02-PLAN.md` — the control-service paint path (`assignKeyImage`/`repaintPage`) + `image_pipeline` use.
- `.planning/phases/15-stream-dock-input-routing/15-01-PLAN.md` — the `DeviceEvent` dispatch + coordinates the bridge consumes.
- `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` — encode entry (ARCH-04); `src/core/include/ajazz/core/profile.hpp` — `Binding`/`EncoderBinding` action chains, `Action.id` (the `<plugin>.<action>` UUID).
- `src/app/src/sd_plugin_server.{hpp,cpp}` + `application.hpp` — server signals + composition root.
- CLAUDE.md — COD-031; keyIndex 1-based on the wire; never skip pre-commit; ASCII test names.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free + device-free: a loopback `QWebSocket` test client registers a
  plugin and binds an action to a key; assert (1) the client's `setImage(context, dataUri)` paints
  the right key (control-service spy sees a `BAT`+`ULEND` burst for that 1-based keyIndex); (2) a
  fed `DeviceEvent` key press delivers `keyDown` to the client with the matching `context`+
  `coordinates`; (3) an encoder turn delivers `dialRotate` with signed `ticks`; (4) `willAppear`
  fires with the context when the action's page is active; (5) a malformed data-uri yields the
  placeholder, not a crash. Use the Phase-14 control-service spy + the Phase-15 input feed +
  the loopback client — no real plugin spawn (Phase 18) and no device (Phase 25). ASCII names.

</specifics>

<deferred>
## Deferred Ideas

- Property Inspector + settings persistence → Phase 20. Built-in in-process actions → Phase 21.
- Plugin store install → Phase 22. Auxiliary-surface targeting (encoder overlays / main strip /
  touch strip via setFeedback/setImage to non-key surfaces) → Phase 23 (HW-gated).
- Live third-party `.sdPlugin` + physical device round-trip → Phase 25 (VERIFY-06).

</deferred>

______________________________________________________________________

*Phase: 19-device-plugin-bridge*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; akp_plugin_sdk.md §5 is the spec)*
