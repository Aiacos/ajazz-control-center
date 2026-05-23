# Phase 19: Device \<-> Plugin Bridge (setImage end-to-end) - Research

**Researched:** 2026-05-23
**Domain:** App-layer integration spine — composing the Phase-17 WebSocket plugin protocol, the Phase-14 device paint path, and the Phase-15 input routing into a bidirectional plugin\<->device bridge (Qt 6 / C++20).
**Confidence:** HIGH on seam signatures and protocol shapes (all read directly from the canonical PLAN files + SDK doc + in-tree headers); MEDIUM on the context-registry design (Claude's discretion, no precedent in tree); HIGH on the blocking dependency-state finding.

## Summary

Phase 19 builds a single new app-layer component (the bridge) that composes four already-designed seams — it reimplements none of them. The bridge owns a **context registry** (the only genuinely new data structure): an opaque `context` id mapped to `(deviceId, pageId, coordinates{row,column}, actionUUID, pluginUuid)`. Inbound, it consumes `SdPluginServer::actionReceived(QString pluginUuid, QJsonObject action)` and, for the visual actions (`setImage`/`setTitle`/`setState`/`setBG`/`setFeedback`/`setText`), resolves the action's `context` to a device key and drives the **Phase-14 `StreamDockControlService`** paint path. Outbound, it consumes the **Phase-15 `StreamDockInputService`** `DeviceEvent` stream (the existing plugin-executor stub is the seam), maps each event to the §4.4 envelope, and calls **Phase-17 `SdPluginServer::sendEvent(uuid, eventName, payload)`** to deliver `keyDown`/`keyUp`/`dialRotate`/`dialDown`/`dialUp`/`touchTap` to the bound plugin. Lifecycle (`willAppear`/`willDisappear` on page show/hide; `deviceDidConnect`/`deviceDidDisconnect` on hot-plug) registers/retires contexts and emits the matching events.

The single most important finding is a **hard execution-order block**: Phases 14, 15, 17, and 18 are all **planned but NOT executed** — none of `*-SUMMARY.md` exists, the live `sd_plugin_server.cpp` still has the obsolete `kStandardActions[13]` and **no `sendEvent`**, and `stream_dock_control_service.*` / `stream_dock_input_service.*` **do not exist on disk**. The seams this phase composes are committed in PLAN form but not in code. Per the phase objective, the bridge plan MUST read the four `*-SUMMARY.md` files for the finalized seam signatures and **STOP** if any is absent. The signatures below come from the PLAN files (the contract) and must be re-verified against the SUMMARY files at plan time, because Phase 14/15 leave several seam details to executor discretion (profile-accessor shape, whether services are QML-exposed, the synthetic-encoder-release observability hook).

The setImage decode path is genuinely new code (no base64/data-URI decoder exists in the app today), but it must **reuse the existing `image_pipeline` encode** (ARCH-04) and the Phase-14 paint path — no second encode path, no second HID handle, no `nlohmann` in core (COD-031). Verification is hardware-free AND real-plugin-free: a loopback `QWebSocket` test client (the established `test_sd_plugin_server.cpp` harness) + a control-service MockTransport spy (Phase 14) + a Phase-15 input feed, combined to assert the five round-trips in the CONTEXT `<specifics>`.

**Primary recommendation:** Build one `PluginDeviceBridge` QObject in `src/app/src/`, constructed in `Application` after the server + control + input services. It owns a context registry, three `QObject::connect` wires (`actionReceived` -> route-to-device, input plugin-executor/event source -> `sendEvent`, hot-plug/page-change -> lifecycle), and a new `data:`-URI decode helper that feeds the existing `image_pipeline`. Gate the whole plan behind a STOP-if-SUMMARY-absent check for phases 14/15/17/18.

## Architectural Responsibility Map

| Capability                                                         | Primary Tier                                | Secondary Tier                                                | Rationale                                                                                                                                                              |
| ------------------------------------------------------------------ | ------------------------------------------- | ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Plugin protocol transport (WS, register, route, sendEvent)         | App / `SdPluginServer` (Phase 17)           | —                                                             | Already owns the loopback WS, the `actionReceived` signal, and the `sendEvent` writer. Bridge consumes, never reimplements.                                            |
| Device paint (brightness, key image, BAT/ULEND burst)              | App / `StreamDockControlService` (Phase 14) | Device backend (`Akp05Device` + `image_pipeline`)             | Single held-open handle + coalesced write queue; the bridge calls `assignKeyImage`, not the wire.                                                                      |
| Device input decode -> DeviceEvent                                 | Device backend (`Akp05Device::poll`)        | App / `StreamDockInputService` (Phase 15)                     | Backend decodes the HID report; the input service dispatches `DeviceEvent` to bindings + the plugin-executor stub the bridge hooks.                                    |
| `context` \<-> (device,page,coords,actionUUID,pluginUuid) registry | App / `PluginDeviceBridge` (Phase 19, NEW)  | —                                                             | No existing component owns this. It is the bridge's reason to exist.                                                                                                   |
| `data:`-URI decode + scale (setImage)                              | App / `PluginDeviceBridge` (Phase 19, NEW)  | Device / `image_pipeline` (encode only)                       | Decode/scale is new app-layer glue; the JPEG encode MUST reuse `image_pipeline` (ARCH-04, no second encode path).                                                      |
| DeviceEvent -> §4.4 envelope mapping                               | App / `PluginDeviceBridge` (Phase 19, NEW)  | —                                                             | Coordinate/controller/ticks translation is bridge logic.                                                                                                               |
| Page model / `pageNavRequested` consumption                        | App / Phase 16 (ActionEngine pages)         | App / `PluginDeviceBridge` (reads active page for willAppear) | Phase 16 owns the page authority; the bridge reads "which page is active" to decide which contexts are visible. Phase 16 is NOT a hard dep of 19 (see Open Questions). |

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                            | Research Support                                                                                                                                                                                                                                                                                                                                                                                       |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| PLUGIN-10              | `setImage` works end-to-end (strip `data:` URI -> `QImage::loadFromData` -> scale to per-key dims -> JPEG q85 -> Phase-14 control service -> physical key; placeholder on decode failure). The bridge connects `actionReceived` (`setImage`/`setTitle`/`setState`/`setBG`/`setFeedback`/`setText`) -> device, and device input (`keyDown`/`keyUp`, `dialRotate`/`dialDown`/`dialUp`, `touchTap`) -> registered plugin, with `willAppear`/`deviceDidConnect` lifecycle. | setImage pipeline: `image_pipeline::encodeForDevice` reuse (Standard Stack); decode path is new (Don't Hand-Roll — use Qt's `QByteArray::fromBase64` + `QImage::loadFromData`). actionReceived/sendEvent seams: §"Standard Stack" + Code Examples. Context registry: §"Architecture Patterns" Pattern 1. DeviceEvent->envelope: Pattern 4 + the coordinates mapping (Pitfall 2). Lifecycle: Pattern 3. |
| \</phase_requirements> |                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |                                                                                                                                                                                                                                                                                                                                                                                                        |

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **The bridge composes, does not reimplement.** A new app-layer component (e.g. `PluginDeviceBridge`) composes the Phase-17 `SdPluginServer` (`actionReceived` in, `sendEvent` out), the Phase-14 control service (key paint path), the Phase-15 input service (`DeviceEvent` source), and the active `Profile` (binding -> action-UUID map).
- **The bridge owns the `context` registry:** `context` (opaque id) \<-> (deviceId, pageId, coordinates {row,column}, action UUID). Populated as the active profile's keys/encoders bind plugin actions; `willAppear` is emitted when an action's page becomes visible (carrying the context); the plugin echoes that `context` back in `setImage`/`setTitle`/etc. and the bridge resolves it to the key and paints.
- **setImage pipeline (PLUGIN-10, spec §5; reuse, do not add a second encode path):** Strip `data:image/{png,jpg};base64,` -> `QByteArray::fromBase64` -> `QImage::loadFromData` -> scale to per-key dims -> encode via the existing `image_pipeline` (ARCH-04) -> control-service paint (`BAT`->1024-chunks->`ULEND`). `target` in {0=hw+sw, 1=hw, 2=sw} per §4.3. COD-031 (QJson/QImage app-layer; no nlohmann in core).
- **Inbound events -> plugin:** Map the Phase-15 `DeviceEvent` to the §4.4 envelope: key press/release -> `keyDown`/`keyUp` with `coordinates`; encoder turn -> `dialRotate` (`ticks`/`pressed`/`controller`); encoder press -> `dialDown`/`dialUp` (legacy `keyDownCord`/`keyUpCord` alias); touch -> `touchTap`. Route ONLY to the plugin that owns the bound action at that coordinate (via the context registry).

### Claude's Discretion

- Exact bridge class name + where it is constructed (Application composition root, after the server + control + input services).
- Context-id scheme (UUID vs encoded tuple) and the registry data structure.
- Whether `willAppear` fires only for the active page (Phase-16 pages enrich this) or all bound actions on connect — pick the spec-faithful behavior (per-visible-action).

### Deferred Ideas (OUT OF SCOPE)

- Property Inspector + settings persistence -> Phase 20. Built-in in-process actions -> Phase 21.
- Plugin store install -> Phase 22. Auxiliary-surface targeting (encoder overlays / main strip / touch strip via setFeedback/setImage to non-key surfaces) -> Phase 23 (HW-gated).
- Live third-party `.sdPlugin` + physical device round-trip -> Phase 25 (VERIFY-06).

## Project Constraints (from CLAUDE.md)

- **COD-031 boundary (release-blocker):** no `nlohmann::json` in `ajazz_core` or any installed public header. The bridge is app-layer; it uses `QJsonObject`/`QJsonDocument` (already the `SdPluginServer` convention) and `QImage`. Never add JSON to core. Verified by `grep -rn nlohmann src/core/include/` == 0.
- **Schema doc is the source of truth for JSON wire keys.** When a C++ field name and a JSON key differ, the schema/SDK doc wins. The §4.2 envelope keys (`event`/`context`/`device`/`action`/`payload`/`controller`/`coordinates`/`deviceCoordinates`/`ticks`/`pressed`) are the wire truth — do not rename to match C++ field names.
- **keyIndex is 1-based on the device wire** (`Akp05Device` rejects 0 and >10), but **Elgato `coordinates` {row,column} are 0-based** (verified). The bridge MUST translate between the two — see Pitfall 2.
- **Never skip pre-commit hooks** (`--no-verify` only if the hook itself is broken, documented in the commit body).
- **ASCII-only test names** (`-` / `->`, never em-dash/arrow — Win32 ctest codepage mangles them and Catch2 filters break).
- **RE is the source of truth for wire formats**, but provisional values are hypotheses. The touch-strip zone map and the encoder LCD model are provisional (Phase 25 reconciles). Do not invent device opcodes.
- **Always test on the active feature branch** (`feat/streamdock`), not main (user directive).
- **Atomic, Conventional Commits**; cap concurrent execute agents at 2.

## Standard Stack

### Core (reuse — these ARE the phase; do not reimplement)

| Component                                                                                                                                        | Where                                                                      | Purpose                                                                                                                                                                                                              | Why Standard                                                                                                                                                                                                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SdPluginServer::actionReceived(QString const& pluginUuid, QJsonObject const& action)`                                                           | `src/app/src/sd_plugin_server.hpp:121` (signal)                            | Inbound plugin->host action stream; carries the verbatim §4.2 envelope (`event`/`context`/`device`/`action`/`payload`). After Phase 17-01, all 41 routed actions surface here.                                       | The single inbound seam; the bridge connects to it. [CITED: 17-01-PLAN.md interfaces]                                                                                                                                                                                            |
| `SdPluginServer::sendEvent(QString const& targetUuid, QString const& eventName, QJsonObject const& payload = {})` -> `bool`                      | `src/app/src/sd_plugin_server.hpp` (added by Phase 17-02)                  | Outbound host->plugin event writer; serializes `{event, payload}` to the plugin's live socket, addressed by registered plugin uuid; returns false (no crash) on missing socket; re-resolves the live slot each call. | The single outbound seam; the bridge calls it for every §4.4 event. **Signature is the LOCKED 17-02 contract — confirm against 17-02-SUMMARY.** [CITED: 17-02-PLAN.md interfaces]                                                                                                |
| `StreamDockControlService` (`setActiveDevice`, `assignKeyImage(uint8 keyIndex /*1-based*/, QImage)`, `repaintFromProfile`, `firmwareVersionFor`) | `src/app/src/stream_dock_control_service.{hpp,cpp}` (added by Phase 14-02) | The device paint path: held-open handle, coalesced `BAT`->1024-chunk->`ULEND` write queue, brightness LIG.                                                                                                           | The bridge's setImage target — calls `assignKeyImage`, never the wire. **`assignKeyImage` arg shape (QImage vs RGBA) is executor discretion — confirm in 14-02-SUMMARY.** [CITED: 14-02-PLAN.md]                                                                                 |
| `StreamDockInputService` (held device, `dispatch(DeviceEvent)`, `pageNavRequested(int)`, ActionExecutors incl. the `plugin` stub)                | `src/app/src/stream_dock_input_service.{hpp,cpp}` (added by Phase 15-01)   | The `DeviceEvent` consumer; the `ActionExecutors.plugin` field is "a logged no-op stub (Phase-19 seam)".                                                                                                             | The bridge's inbound-event source. Two viable hook points (see Open Questions): the `plugin` ActionExecutor, OR a parallel `DeviceEvent` tap. [CITED: 15-01-PLAN.md]                                                                                                             |
| `image_pipeline::encodeForDevice(span<uint8> rgba, uint16 srcW, uint16 srcH, ImageTransform)` -> `vector<uint8>`                                 | `src/devices/streamdeck/src/image_pipeline.hpp:65`                         | RGBA8 -> resize (SmoothTransformation) -> reorient -> JPEG/PNG encode. ARCH-04 single encode path.                                                                                                                   | The ONLY encode path; the bridge must reuse it (PRIVATE-linked to `ajazz_devices_streamdeck`). The Phase-14 `setKeyImage` backend already calls it internally — the bridge passes a `QImage`/RGBA to the control service and lets the backend encode. [VERIFIED: in-tree header] |
| `core::Profile` / `Binding` / `EncoderBinding` / `Action` (`ActionKind::Plugin`, `id` = `<plugin>.<action>`)                                     | `src/core/include/ajazz/core/profile.hpp`                                  | The binding -> action-UUID map: `keys[idx].onPress/onRelease`, `encoders[idx].onCw/onCcw/onPress`, each an `Action` chain. `Action.id` is the dotted plugin action UUID when `kind == Plugin`.                       | How the bridge knows which key/encoder is bound to which plugin action, and which plugin owns it (UUID-prefix match against registered plugins). [VERIFIED: in-tree header]                                                                                                      |
| `core::DisplayInfo` (`widthPx`, `heightPx`, `keyRows`, `keyCols`) via `IDisplayCapable::displayInfo()`                                           | `src/core/include/ajazz/core/capabilities.hpp:107`                         | Per-key target dims (AKP05E: 85x85, 2 rows x 5 cols).                                                                                                                                                                | The setImage scale target + the coordinates\<->keyIndex grid math source. [VERIFIED: in-tree header]                                                                                                                                                                             |

### Supporting (Qt facilities used by the new decode glue)

| Facility                                                                | Purpose                                                                                                                                                    | When to Use                                                                                                                  |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `QByteArray::fromBase64(body, QByteArray::Base64Encoding)`              | Decode the base64 body after stripping the `data:image/...;base64,` prefix.                                                                                | setImage decode step 2. Use the validating overload (`fromBase64Encoding`) if you want to detect malformed input explicitly. |
| `QImage::loadFromData(bytes)` -> `bool`                                 | Decode PNG/JPEG bytes into a `QImage`. Returns false on malformed data -> placeholder path.                                                                | setImage decode step 3.                                                                                                      |
| `QImage::scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)` | Pre-scale to per-key dims (or let the control service / `image_pipeline` do it — pick one, not both).                                                      | setImage scale step. Prefer letting the backend normalise (it already resizes) and pass the decoded QImage through.          |
| `QJsonObject` / `QJsonDocument`                                         | Build §4.4 payloads (`coordinates`, `ticks`, `pressed`, `controller`) and read §4.2/§4.3 inbound payloads (`context`, `payload.image`, `target`, `state`). | All envelope construction/parsing. COD-031: app-layer only.                                                                  |
| `QString` reverse-DNS prefix match                                      | Resolve `Action.id` (`com.x.plugin.action1`) to the owning registered plugin uuid (`com.x.plugin`).                                                        | Context registration: which plugin owns a bound action. See Pitfall 4.                                                       |

### Alternatives Considered

| Instead of                                        | Could Use                                                                            | Tradeoff                                                                                                                                                                                                                                                                                                                                  |
| ------------------------------------------------- | ------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Hooking the Phase-15 `plugin` ActionExecutor stub | A parallel `DeviceEvent` tap registered as a second `onEvent` consumer on the device | The ActionExecutor only fires on a BOUND `ActionKind::Plugin` step (it has the dotted id but not the raw coordinates/ticks); a raw `DeviceEvent` tap has full coordinates/ticks but no binding context. The bridge likely needs BOTH signals OR a richer executor — resolve in the plan against the 15-01-SUMMARY. See Open Questions Q1. |
| New `PluginDeviceBridge` QObject                  | Extending `SdPluginServer` to own the registry                                       | Keeps the server a pure transport (its design intent); the registry needs the Profile + control + input services which the server should not depend on. A separate bridge is the locked decision.                                                                                                                                         |
| Letting the bridge JPEG-encode                    | Reusing `image_pipeline` via the control service                                     | A second encode path violates ARCH-04 + the locked decision. The decoded `QImage` flows to `assignKeyImage`; the backend encodes.                                                                                                                                                                                                         |

**Installation:** None. Pure in-tree C++/Qt6. No new external packages. Qt6 modules already linked by `src/app` (`Qt6::Gui` for QImage, `Qt6::WebSockets` via the server). Confirm `Qt6::Gui` is visible to the bridge translation unit (QImage) at plan time.

## Package Legitimacy Audit

**Not applicable.** Phase 19 installs zero external packages — it is pure in-tree C++/Qt6 composition. The Package Legitimacy Gate is N/A (no npm/PyPI/cargo install; no `[SLOP]`/`[SUS]` exposure). Confirmed against the four dependency PLAN threat models, which all record "no package installs — Package Legitimacy Gate N/A."

## Architecture Patterns

### System Architecture Diagram

```
                     PLUGIN (loopback WS client / Phase-18 spawned child)
                          |  ^
          setImage/setTitle  |  | keyDown/dialRotate/willAppear (§4.4 envelopes)
          /setBG/... (§4.3)  |  |
                          v  |
                  +----------------------+
                  |   SdPluginServer     |  (Phase 17 — transport, NOT modified here)
                  | actionReceived  ^    |
                  |   (signal)      | sendEvent(uuid,event,payload)
                  +-----|-----------|----+
                        |           |
       (QJsonObject     |           | (QJsonObject payload)
        action w/       v           |
        context)  +=========================================+
                  |        PluginDeviceBridge  (NEW)        |
                  |                                         |
                  |  +-----------------------------------+  |
   resolve ctx -> |  | CONTEXT REGISTRY                  |  |
   to key/encoder |  | context -> {deviceId,pageId,      |  |
                  |  |   coords{row,col}, actionUUID,    |  |
                  |  |   pluginUuid}                     |  |
                  |  +-----------------------------------+  |
                  |    |                         ^          |
                  |    | INBOUND route           | OUTBOUND map |
                  |    | (visual actions)        | DeviceEvent->envelope |
                  +----|-------------------------|----------+
                       |                         |
   decode data:URI ->  |                         |  coords = keyIndex-1 -> {row,col}
   QImage -> scale     v                         |  ticks/pressed/controller
                  +-------------------+    +------|------------------+
                  | StreamDockControl |    | StreamDockInputService  |
                  | Service (Phase14) |    | (Phase 15)              |
                  | assignKeyImage    |    | DeviceEvent source +    |
                  | (1-based keyIndex)|    | plugin ActionExecutor   |
                  +---------|---------+    +-----------|-------------+
                            |                          |
                  image_pipeline encode    Akp05Device::poll/onEvent
                  BAT->1024 chunks->ULEND   parseInputReport (DeviceEvent)
                            |                          |
                            v                          ^
                   +======================================+
                   |   PHYSICAL DEVICE (AKP05E, HW-gated  |
                   |   to Phase 25; MockTransport here)   |
                   +======================================+
```

Trace the setImage use case: plugin sends `{event:setImage, context:c1, payload:{image:"data:...", target:0}}` -> `actionReceived` -> bridge resolves `c1` -> key 3 on active page/device -> strip prefix + `fromBase64` + `loadFromData` -> `QImage` -> `controlService.assignKeyImage(3, img)` -> backend `image_pipeline` encode -> BAT/ULEND -> key 3 lights.

Trace the keyDown use case: physical press of key 3 -> `Akp05Device::poll` -> `DeviceEvent{KeyPressed, index=3}` -> input service -> bridge maps index 3 (1-based) -> coordinates `{row:0,column:2}` (0-based) -> looks up the context bound at that coordinate -> finds plugin uuid + context -> `server.sendEvent(uuid, "keyDown", {coordinates:{row:0,column:2}, settings, state, isInMultiAction})`.

### Recommended Project Structure

```
src/app/src/
├── plugin_device_bridge.hpp     # NEW: bridge QObject + ContextRegistry + decode helpers
├── plugin_device_bridge.cpp     # NEW: connect wiring, route-to-device, event mapping, lifecycle
├── application.{hpp,cpp}        # MODIFIED: construct m_pluginBridge after server+control+input
└── (sd_plugin_server, stream_dock_control_service, stream_dock_input_service: NOT modified)
tests/unit/
└── test_plugin_device_bridge.cpp # NEW: loopback client + control spy + input feed e2e
```

### Pattern 1: Context Registry (the new data structure)

**What:** An app-layer map the bridge owns: `context` (opaque id) -> a struct `{QString deviceId; QString pageId; struct {int row; int column;} coords; QString actionUUID; QString pluginUuid;}`. Plus reverse indices for the two hot lookups: by `context` (inbound resolve) and by `(deviceId, controller, coords)` (outbound, given a `DeviceEvent`).

**When to use:** Populated when a profile page becomes active and its bound plugin actions are enumerated; a fresh `context` is minted per visible bound action and `willAppear` is emitted. Cleared/retired on `willDisappear` (page hidden) and `deviceDidDisconnect`.

**Context-id scheme (discretion):** A `QUuid::createUuid().toString()` per registration is simplest and collision-free; an encoded tuple (`deviceId#pageId#controller#row#col`) is debuggable and idempotent across re-registration. Recommend the encoded tuple so re-activating the same page yields the same context (plugins may cache it) — but document the choice in the SUMMARY.

**Example:**

```cpp
// Source: design (no in-tree precedent — Claude's discretion per CONTEXT)
struct ActionContext {
    QString deviceId;
    QString pageId;
    int row{0};       // 0-based Elgato coordinate
    int column{0};    // 0-based Elgato coordinate
    QString controller; // "Keypad" | "Encoder"
    QString actionUUID; // dotted, e.g. com.x.plugin.action1
    QString pluginUuid; // owning plugin, e.g. com.x.plugin
};
// context-string -> ActionContext (inbound resolve from setImage's `context`)
QHash<QString, ActionContext> m_byContext;
// (controller,row,column) -> context-string (outbound: DeviceEvent -> which context)
QHash<QString, QString> m_byCoord; // key = controller + "#" + row + "#" + col
```

### Pattern 2: Inbound action routing (setImage and the visual family)

**What:** Connect `SdPluginServer::actionReceived` -> a bridge slot that reads `action["event"]`, and for the visual set (`setImage`/`setTitle`/`setState`/`setBG`/`setFeedback`/`setText`) resolves `action["context"]` against the registry and drives the control service. Non-visual routed actions (the other 35) are ignored by Phase 19 (later phases consume them).

**When to use:** Every inbound action. setImage is the PLUGIN-10 must-have; the others paint title text / background / state image via the same control-service key target.

### Pattern 3: Lifecycle (willAppear / deviceDidConnect)

**What:** On a page becoming active (the Phase-16 page authority / `pageNavRequested` consumer, or — if Phase 16 is not a dependency — on plugin registration / device connect for all bound actions), enumerate the page's bound `ActionKind::Plugin` actions, register a context each, and `sendEvent(pluginUuid, "willAppear", {coordinates, settings, state, isInMultiAction})`. On hot-plug arrival, `sendEvent(uuid, "deviceDidConnect", {deviceInfo:{name,type,size}})` to every registered plugin; mirror with `willDisappear`/`deviceDidDisconnect`. Spec-faithful behavior is **per-visible-action** `willAppear` (CONTEXT discretion resolved this way).

**When to use:** Page show/hide and device arrival/removal. Note: the device-arrival wire already exists (`Application` re-resolves on hot-plug for Phase 14); the bridge taps the same signal rather than adding a second `HotplugMonitor`.

### Pattern 4: DeviceEvent -> §4.4 envelope mapping

**What:** Map each Phase-15 `core::DeviceEvent` kind to the §4.4 event + payload:

| DeviceEvent                        | index/value semantics                                           | §4.4 event                          | Payload                                                                                                   |
| ---------------------------------- | --------------------------------------------------------------- | ----------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `KeyPressed`                       | index = 1-based key (1..10)                                     | `keyDown`                           | `{coordinates:{row,column}, settings, state, isInMultiAction}`                                            |
| `KeyReleased`                      | index = 1-based key                                             | `keyUp`                             | same as keyDown                                                                                           |
| `EncoderTurned`                    | index = 0-based encoder (0..3); value = signed delta (+CW/-CCW) | `dialRotate`                        | `{ticks:<signed>, pressed:false, controller:"Encoder", deviceCoordinates:{point,width,height}, settings}` |
| `EncoderPressed`                   | index = 0-based encoder; value=1                                | `dialDown` (+ legacy `keyDownCord`) | `{controller:"Encoder", coordinates, settings}`                                                           |
| (synthetic release, Phase-15 hook) | —                                                               | `dialUp` (+ legacy `keyUpCord`)     | same                                                                                                      |
| `TouchStrip` (gesture 0 = Tap)     | value packs (gesture\<<16)\|X; zone = X\*4/640                  | `touchTap`                          | `{x, y, hold:false, settings}`                                                                            |
| `TouchStrip` (gesture 1/2 = swipe) | —                                                               | (page-nav, Phase-16 intent)         | not a plugin event                                                                                        |

**Coordinates derivation (CRITICAL — see Pitfall 2):** 1-based keyIndex `k` on a 2x5 grid (KeyRows=2, KeyCols=5, row-major) -> `row = (k-1) / KeyCols`, `column = (k-1) % KeyCols`, both 0-based. e.g. key 1 -> {0,0}; key 5 -> {0,4}; key 6 -> {1,0}; key 10 -> {1,4}. Verify the device's physical key numbering is row-major top-left-origin against `displayInfo().keyRows/keyCols` and the akp05 decode order before locking; document in SUMMARY.

### Anti-Patterns to Avoid

- **A second image encode path.** The decoded `QImage` must reach the device via `image_pipeline` (through the Phase-14 control service). Do not call `QImageWriter`/`QImage::save` in the bridge. (ARCH-04, locked.)
- **A second HID handle or a second input parser.** The bridge holds neither a device handle nor a decoder — it taps the existing input service. (ARCH-03.)
- **Caching a `QWebSocket*` for sendEvent.** `sendEvent` already re-resolves the live slot per call; the bridge addresses by `pluginUuid`/`context`, never a raw socket. (17-02 Pitfall 4.)
- **`nlohmann::json` anywhere app-side that could leak to core.** Use `QJsonObject`. (COD-031.)
- **Routing inbound actions to a key the plugin doesn't own.** Resolve `context` first; an unknown/stale context is a no-op (not a crash).

## Don't Hand-Roll

| Problem                                       | Don't Build                  | Use Instead                                                        | Why                                                                                           |
| --------------------------------------------- | ---------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------- |
| Base64 decode of the data-URI body            | A hand-rolled base64 decoder | `QByteArray::fromBase64`                                           | Qt's is validated, fast, and handles padding/whitespace. A hand-rolled one is a CWE magnet.   |
| PNG/JPEG decode                               | A format sniffer + decoder   | `QImage::loadFromData` (returns false on malformed -> placeholder) | Qt wraps libpng/libjpeg with the malformed-input handling the spec §5 placeholder path needs. |
| Image resize to per-key dims + JPEG encode    | A new scale/encode           | `image_pipeline::encodeForDevice` via the Phase-14 control service | ARCH-04 single encode path; the backend already resizes + encodes JPEG q85.                   |
| Plugin->host transport, routing, sendEvent    | A new WS handler             | `SdPluginServer` (`actionReceived` / `sendEvent`)                  | Phase 17 owns it; loopback-bind invariant + 41-action routing already designed.               |
| Device input decode + DeviceEvent dispatch    | A new poll loop              | `StreamDockInputService` + `Akp05Device::poll`                     | Phase 15 owns it; the `plugin` ActionExecutor is the explicit Phase-19 seam.                  |
| Held-open device handle + coalesced key paint | A new write queue            | `StreamDockControlService::assignKeyImage`                         | Phase 14 owns the single held handle + BAT/ULEND coalescing.                                  |
| JSON envelope build/parse                     | Manual string concat         | `QJsonObject`/`QJsonDocument`                                      | The server already speaks this; consistency + COD-031 compliance.                             |

**Key insight:** Phase 19 is almost entirely composition. The ONLY genuinely new code is (a) the context registry data structure + its lookups, (b) the `data:`-URI -> `QImage` decode helper (with the placeholder fallback), and (c) the `DeviceEvent` -> §4.4 envelope translation (including the 1-based-keyIndex -> 0-based-coordinates math). Everything else is `QObject::connect` wiring to existing seams.

## Runtime State Inventory

> Phase 19 is additive integration (new bridge + Application wiring), not a rename/refactor/migration. This section is included only to record the no-op explicitly.

| Category            | Items Found                                                                                                                                                                                                                                                                              | Action Required                       |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| Stored data         | None — the context registry is in-memory only, rebuilt on each page activation; no persisted keys/IDs introduced. Settings persistence is Phase 20.                                                                                                                                      | None                                  |
| Live service config | None — no external service config touched.                                                                                                                                                                                                                                               | None                                  |
| OS-registered state | None — no OS registrations.                                                                                                                                                                                                                                                              | None                                  |
| Secrets/env vars    | None — no new secrets/env. The plugin auth challenge is Phase 17-03, not consumed here.                                                                                                                                                                                                  | None                                  |
| Build artifacts     | New `plugin_device_bridge.{hpp,cpp}` must be added to `src/app/CMakeLists.txt` in BOTH the library source block AND the AUTOMOC/header block (mirror `lighting_service`, per the Phase-14/15 plan convention). A stale build without this re-run will not compile the new QObject's moc. | Add to CMake source + AUTOMOC blocks. |

**Nothing found in the first four categories:** verified — Phase 19 introduces no persisted, OS-registered, service-side, or secret state.

## Common Pitfalls

### Pitfall 1: Dependency seams are PLANNED, not EXECUTED (the blocking pitfall)

**What goes wrong:** The bridge is written against `actionReceived`/`sendEvent`/`StreamDockControlService`/`StreamDockInputService`, but on disk (as of 2026-05-23) NONE of these exist in their final form: `sd_plugin_server.cpp` still has `kStandardActions[13]` and no `sendEvent`; `stream_dock_control_service.*` and `stream_dock_input_service.*` are absent; no `*-SUMMARY.md` exists for phases 14/15/17/18.
**Why it happens:** v1.3 was "planned ahead from scratch" (STATE.md) before execution. Phase 19 `depends_on: [14,15,17,18]` are all unexecuted.
**How to avoid:** The Phase-19 plan MUST open with a STOP-condition check (mirroring 18-04's pattern): read `14-02-SUMMARY.md`, `15-01-SUMMARY.md` (and `15-02-SUMMARY.md` for Application wiring), `17-01-SUMMARY.md` + `17-02-SUMMARY.md`, and `18-*-SUMMARY.md`. If any is absent, STOP and report that the dependency phase must be executed first — do NOT reimplement the seam. Re-read each seam's finalized signature from the SUMMARY (executor discretion may have changed `assignKeyImage`'s arg shape, the profile-accessor seam, the synthetic-encoder-release hook, etc.).
**Warning signs:** `grep sendEvent src/app/src/sd_plugin_server.hpp` returns 0; `ls src/app/src/stream_dock_control_service.hpp` fails; `find .planning/phases/1[4578] -name '*SUMMARY*'` returns nothing.

### Pitfall 2: keyIndex base mismatch (1-based device vs 0-based Elgato coordinates)

**What goes wrong:** The device backend uses **1-based** keyIndex (1..10; `keyIndexInRange` rejects 0). Elgato `coordinates {row,column}` are **0-based** (verified against Elgato SDK docs). Naively passing the device keyIndex as a column, or sending 1-based coordinates to the plugin, paints/reports the wrong key — off by one in both directions.
**Why it happens:** Two conventions meet exactly at the bridge; the seam docs disagree (`IDisplayCapable` doxygen even says "zero-based" but the SHIPPING backend is 1-based — the backend wins, per CLAUDE.md RE-is-truth and the 14-02 interfaces note).
**How to avoid:** Convert at the bridge boundary, once, in named helpers: `coordsForKeyIndex(uint8 oneBased) -> {row=(k-1)/keyCols, col=(k-1)%keyCols}` and `keyIndexForCoords(row,col) -> row*keyCols+col+1`. Source `keyCols`/`keyRows` from `displayInfo()`, not a literal. Unit-test the round-trip (key 1\<->{0,0}, key 10\<->{1,4}).
**Warning signs:** setImage paints the neighbor key; keyDown reports column 3 for a press on the 3rd key (should be column 2).

### Pitfall 3: Two plausible inbound-event hooks with different information

**What goes wrong:** Hooking only the Phase-15 `plugin` ActionExecutor gives you the dotted `Action.id` and `settingsJson` but NOT the raw coordinates/ticks/pressed; tapping only a raw `DeviceEvent` gives coordinates/ticks but not the binding (which plugin/context). Picking one alone yields incomplete §4.4 payloads.
**Why it happens:** Phase 15 routes a `DeviceEvent` through the `ActionEngine`, which invokes the `plugin` executor per `ActionKind::Plugin` step — that path has lost the originating coordinates by the time it reaches the executor.
**How to avoid:** Resolve in the plan against the 15-01-SUMMARY. Likely the cleanest design is a **`DeviceEvent` tap** (the bridge subscribes to the same event stream the input service dispatches) so it has full coordinates/ticks, then it consults the context registry (built from the active Profile's bindings) to find the owning plugin/context. The `plugin` ActionExecutor stub can remain a logged no-op, or be wired to call the bridge with the dotted id as a cross-check. Document the chosen hook + why.
**Warning signs:** `dialRotate` arrives with `ticks` always 0/1; `keyDown` has no `coordinates`; the event reaches a plugin that doesn't own the key.

### Pitfall 4: Resolving the owning plugin uuid from the action UUID

**What goes wrong:** A bound `Action.id` is the dotted action UUID (`com.x.plugin.action1`); the registered plugin uuid is the prefix (`com.x.plugin`). A wrong split routes events to the wrong (or no) plugin.
**Why it happens:** There is no explicit action-UUID -> plugin-uuid table; it's a reverse-DNS prefix relationship, and the plugin's registered uuid is what `registerPlugin` carried (tracked in `SdPluginServer::pluginRegistered`).
**How to avoid:** Match the bound action's dotted id against the set of REGISTERED plugin uuids (longest-prefix match), rather than naively trimming the last dotted segment (action UUIDs and plugin UUIDs are both reverse-DNS and a plugin may declare multi-segment action ids). Maintain the registered-uuid set from `pluginRegistered`/`pluginDisconnected`. Only emit events to currently-registered plugins.
**Warning signs:** `sendEvent` returns false (no live socket) for a key that visibly has a plugin image; events silently dropped.

### Pitfall 5: Static-destruction crash in the loopback test harness

**What goes wrong:** Allocating a fresh `QCoreApplication` per TEST_CASE (or stack-allocating it) races QtWebSockets' background thread at process exit and SIGSEGVs.
**Why it happens:** ctest splits each TEST_CASE into a subprocess; the WS stack tears down on a background thread.
**How to avoid:** Reuse the established `ensureQCoreApp()` leaked-singleton + `waitForSpy()`/`pump()` helpers from `tests/unit/test_sd_plugin_server.cpp` (do NOT allocate a new QCoreApplication).
**Warning signs:** Intermittent SIGSEGV at test-process exit; flaky CI on the WS-gated target.

### Pitfall 6: Placeholder-on-decode-failure must not crash and must not fall through to a real image

**What goes wrong:** A malformed `data:` URI (bad base64, truncated PNG) makes `loadFromData` return false; if the bridge then proceeds to scale/paint a null QImage, it crashes or paints garbage.
**Why it happens:** The decode is a multi-step pipeline; any step can fail.
**How to avoid:** Check each step's result; on failure paint a placeholder (spec §5 says vendor sends an "X" via setTitle; for us a solid-color/placeholder key via the control service's `encodeSolid`/`setKeyColor` path is acceptable and crash-free) and do NOT send a failure event back to the plugin (spec §5: no failure event). Make this a tested case (CONTEXT specifics #5).
**Warning signs:** A test feeding a bad data-URI crashes instead of painting a placeholder.

## Code Examples

### setImage decode -> scale -> control-service paint (the PLUGIN-10 core)

```cpp
// Source: design per akp_plugin_sdk.md §5 + image_pipeline.hpp + StreamDockControlService (14-02-PLAN)
// COD-031: app-layer QJson/QImage only.
void PluginDeviceBridge::onSetImage(QString const& pluginUuid, QJsonObject const& action) {
    QString const context = action.value("context").toString();
    auto it = m_byContext.constFind(context);
    if (it == m_byContext.constEnd()) return;            // stale/unknown context -> no-op
    int const target = action.value("payload").toObject().value("target").toInt(0); // 0/1/2 (§4.3)

    QString const dataUri = action.value("payload").toObject().value("image").toString();
    int const comma = dataUri.indexOf(QLatin1Char(','));  // strip "data:image/...;base64,"
    QByteArray const body = (comma >= 0 ? dataUri.mid(comma + 1) : dataUri).toUtf8();
    QByteArray const raw = QByteArray::fromBase64(body);

    QImage img;
    if (!img.loadFromData(raw)) {                          // §5 placeholder on decode failure
        paintPlaceholder(it->deviceId, it->row, it->column); // crash-free; NO failure event back
        return;
    }
    // Let the Phase-14 control service + image_pipeline do the scale + JPEG q85 encode (ARCH-04).
    std::uint8_t const keyIndex = keyIndexForCoords(it->row, it->column); // 1-based device index
    m_controlService->assignKeyImage(keyIndex, img);        // confirm arg shape vs 14-02-SUMMARY
    // target 1/2 (hw-only/sw-only) handling is a refinement; default 0 paints the physical key.
}
```

### DeviceEvent -> §4.4 sendEvent (keyDown + dialRotate)

```cpp
// Source: design per akp_plugin_sdk.md §4.4 + DeviceEvent (15-01-PLAN interfaces) + sendEvent (17-02)
void PluginDeviceBridge::onDeviceEvent(QString const& deviceId, core::DeviceEvent const& ev) {
    using K = core::DeviceEvent::Kind;
    switch (ev.kind) {
    case K::KeyPressed: case K::KeyReleased: {
        auto const [row, col] = coordsForKeyIndex(static_cast<std::uint8_t>(ev.index)); // 0-based
        auto ctx = lookupContext("Keypad", row, col);
        if (!ctx) return;
        QJsonObject payload{{"coordinates", QJsonObject{{"row", row}, {"column", col}}},
                            {"isInMultiAction", false}};
        m_server->sendEvent(ctx->pluginUuid, ev.kind == K::KeyPressed ? "keyDown" : "keyUp", payload);
        break;
    }
    case K::EncoderTurned: {
        auto ctx = lookupContext("Encoder", /*row*/0, /*col*/ev.index); // encoder index as coord
        if (!ctx) return;
        QJsonObject payload{{"ticks", ev.value}, {"pressed", false}, {"controller", "Encoder"}};
        m_server->sendEvent(ctx->pluginUuid, "dialRotate", payload);
        break;
    }
    case K::EncoderPressed: { /* dialDown (+ legacy keyDownCord) */ break; }
    // synthetic release (Phase-15 hook) -> dialUp (+ keyUpCord); TouchStrip tap -> touchTap
    default: break;
    }
}
```

## State of the Art

| Old Approach                                                             | Current Approach                                              | When Changed              | Impact                                                                                                                   |
| ------------------------------------------------------------------------ | ------------------------------------------------------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Vendor host binds `QHostAddress::Any`, unsigned plugins, plaintext relay | Loopback-only bind (`SdPluginServer`), context-scoped routing | v1.3 design (locked)      | The bridge inherits the loopback-only posture; do not widen.                                                             |
| QCefView (CEF 109, dated) for plugin/PI HTML                             | QWebEngineView + QWebChannel                                  | Phase 20 (not this phase) | N/A to Phase 19 (PI is deferred).                                                                                        |
| `kStandardActions[13]` (MVP, 26 AJAZZ unhandled)                         | Sizeless `kRoutedActions` (41 routed)                         | Phase 17-01 (planned)     | The bridge can rely on all visual actions surfacing via `actionReceived` — but ONLY after 17-01 is executed (Pitfall 1). |

**Deprecated/outdated:**

- `IDisplayCapable::setKeyImage` doxygen says "zero-based keyIndex" — OUTDATED; the shipping `Akp05Device` is 1-based and the backend wins (CLAUDE.md). The bridge converts at its boundary (Pitfall 2).

## Assumptions Log

| #   | Claim                                                                                                                                                                                                              | Section                                 | Risk if Wrong                                                                                                                                                                   |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | `sendEvent` final signature is `bool sendEvent(QString const& targetUuid, QString const& eventName, QJsonObject const& payload = {})` exactly as the 17-02 PLAN locks it.                                          | Standard Stack                          | Bridge outbound calls won't compile; must read 17-02-SUMMARY to confirm before coding.                                                                                          |
| A2  | `StreamDockControlService::assignKeyImage` takes `(uint8 keyIndex, QImage)` (1-based). The 14-02 plan leaves "QImage vs RGBA buffer" to executor discretion.                                                       | Standard Stack / Code Examples          | The setImage paint call shape changes; confirm in 14-02-SUMMARY.                                                                                                                |
| A3  | The bridge can tap the Phase-15 `DeviceEvent` stream (not only the `plugin` ActionExecutor) to get full coordinates/ticks. The 15-01 plan exposes the executor stub but the event-tap seam is executor discretion. | Pitfall 3 / Open Q1                     | If only the executor is exposed, payloads lack coordinates; may require a 15-02 follow-up seam. Confirm in 15-01/15-02-SUMMARY.                                                 |
| A4  | Device keys are numbered 1-based, row-major, top-left origin on a 2x5 grid, so `row=(k-1)/5, col=(k-1)%5`.                                                                                                         | Pitfall 2 / Code Examples               | Coordinates off — wrong key reported/painted. Verify against akp05 decode order + a hardware witness (Phase 25).                                                                |
| A5  | Elgato `coordinates {row,column}` are 0-based and the controller field distinguishes Keypad vs Encoder.                                                                                                            | Pattern 4 / Pitfall 2                   | Verified against Elgato SDK docs (CITED); low risk.                                                                                                                             |
| A6  | Phase 16 (page authority) is NOT a hard dependency of Phase 19; `willAppear` can fire per-bound-action on registration/connect for the active (root) page without the full multi-page model.                       | Architectural Map / Pattern 3 / Open Q3 | If willAppear must be page-scoped, lifecycle correctness depends on Phase 16. The CONTEXT discretion allows the simpler per-visible-action behavior; document the chosen scope. |
| A7  | The placeholder on decode failure may be a solid-color key (via the control service / `encodeSolid`) rather than the vendor's literal setTitle "X".                                                                | Pitfall 6                               | Cosmetic divergence from vendor §5; spec only requires "no crash, no failure event" — acceptable.                                                                               |
| A8  | `target` in {0,1,2} (hw+sw / hw / sw): Phase 19 treats 0 (and absent) as "paint the physical key"; 1/2 nuance (sw-only preview surfaces) is a refinement, not gating for PLUGIN-10.                                | Code Examples                           | Minor; sw-only targeting relates to UI mirror surfaces not built until later.                                                                                                   |

## Open Questions

1. **Which inbound-event hook does the bridge use — the Phase-15 `plugin` ActionExecutor, a raw `DeviceEvent` tap, or both?**

   - What we know: the executor has the dotted action id but not raw coordinates; a `DeviceEvent` tap has coordinates but not the binding. The 15-01 plan exposes the executor stub explicitly as the "Phase-19 seam".
   - What's unclear: whether 15-01/15-02 exposes a subscribable `DeviceEvent` signal the bridge can tap.
   - Recommendation: read 15-01-SUMMARY + 15-02-SUMMARY; prefer a `DeviceEvent` tap + registry lookup (full §4.4 payloads). If absent, plan a small additive seam (a Qt signal on the input service) rather than re-deriving coordinates from the executor.

1. **`assignKeyImage` argument shape (QImage vs RGBA span) and whether the control service is QML-exposed.**

   - What we know: 14-02 leaves this to executor discretion; the SUMMARY records the decision.
   - Recommendation: read 14-02-SUMMARY; adapt the setImage paint call accordingly; do not assume.

1. **`willAppear` scope: per-visible-action on the active page (needs Phase 16 page authority) vs all-bound-on-connect.**

   - What we know: CONTEXT discretion says "pick the spec-faithful behavior (per-visible-action)"; ROADMAP lists Phase 19 `depends_on` 14/15/17/18 (NOT 16), and Phase 21 (not 19) depends on 16.
   - Recommendation: implement per-visible-action for the active/root page using whatever page state Phase 15/16 exposes; if the full multi-page model isn't available (Phase 16 unexecuted), scope willAppear to the root page and note the simplification for a Phase 21/16 enrichment.

1. **AKP05E encoder + touch as plugin targets vs key-only scope.**

   - What we know: CONTEXT says Phase 19 routes to keys (and encoders where the binding exists); rich aux-surface targeting is Phase 23. Encoders emit press-only (synthetic release from Phase 15).
   - Recommendation: wire `dialRotate`/`dialDown`/`dialUp` + `touchTap` outbound (the must-have list includes them) but keep inbound `setFeedback`/`setText` (encoder/strip overlays) as route-acknowledged-but-surface-deferred to Phase 23; document the boundary.

## Environment Availability

| Dependency                                       | Required By                                  | Available                                                                               | Version                 | Fallback                                                                      |
| ------------------------------------------------ | -------------------------------------------- | --------------------------------------------------------------------------------------- | ----------------------- | ----------------------------------------------------------------------------- |
| Qt6 (Gui, WebSockets)                            | QImage decode, the WS server/loopback client | Likely (project baseline Qt 6.7+; `src/app` already links these)                        | — (verify at plan time) | —                                                                             |
| Qt6 private headers (`qt6-qtbase-private-devel`) | Build (Phase-10/14 prerequisite)             | Operator action (per STATE.md; system-package install is the operator's, not tooling's) | —                       | Build blocked until installed (no code fallback).                             |
| `executed` dependency phases 14/15/17/18         | The seams this phase composes                | NO — planned, not executed                                                              | —                       | **BLOCKING — no fallback.** STOP-if-SUMMARY-absent (Pitfall 1).               |
| Physical AKP05E                                  | Live witness                                 | NO — hardware-gated to Phase 25                                                         | fw `V3.AKP05E.01.007`   | MockTransport + loopback client (this phase is hardware-free by design).      |
| Real `.sdPlugin`                                 | Live witness                                 | NO — Phase 25 (VERIFY-06)                                                               | —                       | Loopback `QWebSocket` test client (this phase is real-plugin-free by design). |

**Missing dependencies with no fallback:**

- Executed phases 14/15/17/18 (their `*-SUMMARY.md` + the actual code seams). The Phase-19 plan must STOP if any SUMMARY is absent.

**Missing dependencies with fallback:**

- Physical device -> MockTransport + control-service spy.
- Real plugin -> loopback `QWebSocket` client.

## Validation Architecture

### Test Framework

| Property           | Value                                                                                                                   |
| ------------------ | ----------------------------------------------------------------------------------------------------------------------- |
| Framework          | Catch2 (unit) via CTest; offscreen Qt event loop. ~408 ctest cases as of 2026-05-22.                                    |
| Config file        | CMake presets (`CMakePresets.json`); test registration in `tests/unit/CMakeLists.txt`                                   |
| Quick run command  | `ctest --preset linux-release -R PluginDeviceBridge` (tag/name TBD by plan; mirror `-R StreamDockControlService` style) |
| Full suite command | `ctest --preset linux-release`                                                                                          |

### Phase Requirements -> Test Map

| Req ID    | Behavior                                                                                                                                      | Test Type               | Automated Command                                    | File Exists?                                           |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------- | ---------------------------------------------------- | ------------------------------------------------------ |
| PLUGIN-10 | Loopback client `setImage(context, dataUri)` paints the right key -> control-service spy sees a `BAT`+`ULEND` burst for that 1-based keyIndex | integration (offscreen) | `ctest --preset linux-release -R PluginDeviceBridge` | ❌ Wave 0 (`tests/unit/test_plugin_device_bridge.cpp`) |
| PLUGIN-10 | Fed `DeviceEvent` key press delivers `keyDown` to the loopback client with matching `context`+`coordinates`                                   | integration             | same                                                 | ❌ Wave 0                                              |
| PLUGIN-10 | Encoder turn delivers `dialRotate` with signed `ticks`                                                                                        | integration             | same                                                 | ❌ Wave 0                                              |
| PLUGIN-10 | `willAppear` fires with the context when the action's page is active                                                                          | integration             | same                                                 | ❌ Wave 0                                              |
| PLUGIN-10 | Malformed data-URI yields a placeholder, not a crash                                                                                          | integration             | same                                                 | ❌ Wave 0                                              |
| (unit)    | `coordsForKeyIndex`/`keyIndexForCoords` round-trip (key 1\<->{0,0}, key 10\<->{1,4})                                                          | unit                    | same                                                 | ❌ Wave 0                                              |
| (unit)    | action-UUID -> registered-plugin-uuid longest-prefix resolution                                                                               | unit                    | same                                                 | ❌ Wave 0                                              |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R PluginDeviceBridge`
- **Per wave merge:** `ctest --preset linux-release` (full suite green)
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/unit/test_plugin_device_bridge.cpp` — the five e2e assertions + the two unit helpers (PLUGIN-10). Combines the `test_sd_plugin_server.cpp` loopback harness (`ensureQCoreApp`/`waitForSpy`/`pump`, QWebSocket client) + the Phase-14 control-service MockTransport spy + a Phase-15 `DeviceEvent` feed.
- [ ] Register in `tests/unit/CMakeLists.txt` linking `plugin_device_bridge.cpp` + `sd_plugin_server.cpp` + `stream_dock_control_service.cpp` + `stream_dock_input_service.cpp` + the streamdeck device source (mirror the dependency plans' link blocks), inside the `AJAZZ_HAVE_WEBSOCKETS` gate.
- [ ] Add `plugin_device_bridge.{cpp,hpp}` to `src/app/CMakeLists.txt` source + AUTOMOC blocks (QObject moc).
- [ ] No framework install needed (Catch2 + CTest already present).

## Security Domain

### Applicable ASVS Categories

| ASVS Category         | Applies  | Standard Control                                                                                                                                                                                                                                         |
| --------------------- | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | indirect | Plugin auth (salt/challenge) is Phase 17-03, not consumed here; the bridge only routes to ALREADY-registered plugin uuids.                                                                                                                               |
| V3 Session Management | no       | No sessions beyond the WS connection the server owns.                                                                                                                                                                                                    |
| V4 Access Control     | yes      | The context registry IS the access-control surface: a plugin may only paint/receive events for keys whose bound action it owns (UUID-prefix match). An inbound action with an unknown/stale context is a no-op (no cross-plugin key access).             |
| V5 Input Validation   | yes      | `data:`-URI parse, base64 decode, `QImage::loadFromData` malformed-input -> placeholder (no crash); `target`/`state`/`context` read defensively; `coordinates` clamped to grid; `ticks` is a bounded int. Use Qt's validated decoders (Don't Hand-Roll). |
| V6 Cryptography       | no       | None in Phase 19.                                                                                                                                                                                                                                        |

### Known Threat Patterns for the bridge

| Pattern                                                           | STRIDE                             | Standard Mitigation                                                                                                                                                                                                           |
| ----------------------------------------------------------------- | ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Malformed/oversized `data:` URI (decode bomb, truncated image)    | DoS / Tampering                    | Qt validated `fromBase64` + `loadFromData` (returns false); placeholder on failure; no crash; the device backend caps JPEG payload (SEC-008/CWE-190) downstream.                                                              |
| Cross-plugin key targeting (plugin A paints/reads plugin B's key) | Elevation / Information Disclosure | Resolve `context` against the registry; only the owning plugin's uuid receives events; unknown context = no-op.                                                                                                               |
| `sendEvent` to a closed/rejected socket                           | Tampering / DoS                    | `sendEvent` re-resolves the live slot per call and returns false safely (17-02 mitigation); the bridge addresses by uuid, never caches a socket.                                                                              |
| `sendToDevice` raw-byte forwarding (a routed AJAZZ action)        | Tampering                          | Phase 19 does NOT forward raw bytes to the device (deferred device-trust concern, per 17 threat model T-17-DEVTRUST / T-17b). The bridge only handles the visual action set; `sendToDevice` is routed-but-not-fulfilled here. |
| Stale context after page change / device removal                  | Tampering                          | Retire contexts on `willDisappear`/`deviceDidDisconnect`; a paint to a stale context is a no-op.                                                                                                                              |

## Sources

### Primary (HIGH confidence)

- In-tree headers (read this session): `src/app/src/sd_plugin_server.hpp` (signals `actionReceived`/`unhandledEventReceived`, no `sendEvent` yet); `src/devices/streamdeck/src/image_pipeline.hpp` (`encodeForDevice`); `src/core/include/ajazz/core/profile.hpp` (`Action.id`, `Binding`, `EncoderBinding`, `ActionKind::Plugin`); `src/core/include/ajazz/core/capabilities.hpp` (`DisplayInfo`, `IDisplayCapable::setKeyImage`); `src/devices/streamdeck/src/akp05_protocol.hpp` (KeyCount=10, KeyRows=2, KeyCols=5, EncoderCount=4, KeyWidthPx=85, TouchStripRangeX=640); `src/app/src/application.hpp` (composition root + member-order/-Wreorder convention); `tests/unit/test_sd_plugin_server.cpp` (loopback harness: `ensureQCoreApp`/`waitForSpy`/`pump`/QWebSocket).
- Canonical PLAN files (the locked seam contracts): `14-02-PLAN.md` (control service `assignKeyImage`/`repaintFromProfile`, 1-based keyIndex), `15-01-PLAN.md` (`DeviceEvent` kinds, plugin ActionExecutor stub, coalescer, synthetic release, touch zone), `17-01-PLAN.md` (`actionReceived` 41-routed set), `17-02-PLAN.md` (`sendEvent` signature + §4.4 surface), `18-04-PLAN.md` (STOP-if-SUMMARY-absent precedent + sendEvent consumption).
- `docs/protocols/streamdeck/akp_plugin_sdk.md` §4.2 (envelope), §4.3 (`target`, action table), §4.4 (host->plugin events incl. `coordinates`/`ticks`/`pressed`/`controller`), §5 (setImage end-to-end + placeholder).
- `.planning/phases/19-device-plugin-bridge/19-CONTEXT.md` (locked decisions, specifics, deferred).
- `.planning/REQUIREMENTS.md` (PLUGIN-10 / PLUGIN-03 / PLUGIN-04 / VERIFY-06); `.planning/ROADMAP.md` Phase 19; `.planning/STATE.md` (execution state: 14-18 PLANNED not EXECUTED); `CLAUDE.md`.

### Secondary (MEDIUM confidence)

- Elgato Stream Deck SDK docs (verified the `coordinates {row,column}` 0-based convention + Stream Deck + Encoder controller note): https://docs.elgato.com/streamdeck/sdk/guides/keys/ , https://docs.elgato.com/streamdeck/sdk/references/websocket/plugin/

### Tertiary (LOW confidence)

- None — all claims are sourced from in-tree files, the canonical PLANs/SDK doc, or the verified Elgato docs.

## Metadata

**Confidence breakdown:**

- Seam signatures (actionReceived/sendEvent/control/input/image_pipeline): HIGH — read from the canonical PLAN files + in-tree headers. Caveat: PLANs are the contract; the SUMMARY files (not yet written) hold executor-discretion finalizations (A1-A3).
- Architecture / context registry: MEDIUM — Claude's discretion per CONTEXT; no in-tree precedent; design is sound but plan must lock the id scheme + hook point.
- Pitfalls: HIGH — the dependency-state block (Pitfall 1) and the keyIndex base mismatch (Pitfall 2) are verified against disk + the backend's documented 1-based invariant + Elgato's 0-based docs.
- setImage decode/placeholder: HIGH — Qt facilities + spec §5 are well-established.

**Research date:** 2026-05-23
**Valid until:** ~2026-06-22 (stable in-tree domain; but **invalidate immediately upon execution of phases 14/15/17/18** — re-read their SUMMARY files for finalized seam signatures before planning).
