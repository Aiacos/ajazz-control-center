# Elgato Stream Deck plugin protocol — canonical wire reference

> **Status: source-grounded specification (2026-06).** This is the authoritative
> wire-protocol reference the AJAZZ Control Center plugin engine implements against.
> It is compiled from the *open-source* Elgato SDK (`elgatosf/streamdeck`,
> `elgatosf/schemas`) and the published JSON schemas, cross-checked against the
> OpenDeck reference implementation (`nekename/OpenDeck`) and our own vendor RE in
> [`akp_plugin_sdk.md`](./akp_plugin_sdk.md).
>
> **Precedence:** for the *Elgato wire format* (manifest, registration, events,
> commands) THIS doc is the source of truth. For *AJAZZ/Mirabox vendor extensions*
> (the `passHello` auth handshake, `setBG`, `getUserInfo`, audio/screenshot,
> `Knob`/`SecondaryScreen` controllers, `PUUID`/`IsK1Pro`) the vendor RE
> `akp_plugin_sdk.md` remains the source of truth. Where they conflict on a wire
> key, this doc wins for compatibility with real `.sdPlugin` packages.
>
> **Design stance (from OpenDeck):** be a *clean subset* of Elgato, not a superset.
> Real `.sdPlugin` packages target Elgato; they never need the vendor extensions.
> Implement the Elgato core faithfully; treat vendor extensions as additive and
> never let them block a standards-compliant plugin.

## Sources

- Official docs: <https://docs.elgato.com/streamdeck/sdk/> (current, SDKVersion 3)
  and legacy <https://docs.elgato.com/sdk/plugins/> (SDKVersion 2) — the canonical
  registration handshake + `RegistrationInfo` shape.
- Node SDK (typed source of truth): `elgatosf/streamdeck` →
  `packages/plugin/src/api/events/{action,keypad,encoder,device,system,ui}.ts`,
  `command.ts`, `target.ts`, `registration/{info,parameters}.ts`.
- Schemas: `elgatosf/schemas` + published
  <https://schemas.elgato.com/streamdeck/plugins/%7Bmanifest,layout%7D.json>.
- OpenDeck (subset reference): `nekename/OpenDeck`
  `src-tauri/src/events/{inbound,outbound}/**`, `plugins/{mod,manifest,info_param}.rs`.
- Device-driver model: `OpenActionAPI/rust`, `ambiso/opendeck-akp05`, `4ndv/mirajazz`.

______________________________________________________________________

## 1. Plugin package format (`.sdPlugin`)

A plugin is a directory whose name **is the plugin UUID** and ends in `.sdPlugin`,
e.g. `com.example.counter.sdPlugin/`. The only required file is `manifest.json`.

```
com.example.counter.sdPlugin/
├── manifest.json          # required
├── bin/ or code/          # plugin code (Node SDK output, native exe, or HTML)
├── imgs/                  # action/category/plugin icons & state images
├── ui/  or  *.html        # property inspector HTML
└── logs/                  # logger output
```

UUIDs use **reverse-DNS, lowercase**, charset `[a-z0-9].-`. Action UUIDs are
**prefixed by the plugin UUID** (e.g. `com.example.counter.increment`). The plugin
UUID is immutable once published.

### 1.1 `manifest.json` — top-level fields

`$schema`: `https://schemas.elgato.com/streamdeck/plugins/manifest.json` (draft-07).

| Field                         | Type   | Req          | Notes                                                   |
| ----------------------------- | ------ | ------------ | ------------------------------------------------------- |
| `UUID`                        | string | yes          | reverse-DNS, lowercase                                  |
| `Name` `Author` `Description` | string | yes          |                                                         |
| `Version`                     | string | yes          | `{major}.{minor}.{patch}.{build}`                       |
| `Icon`                        | string | yes          | path, **extension omitted**; PNG 256² + 512² (@2x)      |
| `CodePath`                    | string | yes          | entry point **with** extension                          |
| `CodePathMac` / `CodePathWin` | string | no           | platform overrides                                      |
| `SDKVersion`                  | number | yes (Elgato) | `2` or `3`. **Vendor manifests ship `1`** — accept it.  |
| `Software.MinimumVersion`     | string | yes          | the real capability gate (we emulate `6.9`)             |
| `OS`                          | array  | yes          | `[{Platform:"mac"\|"windows", MinimumVersion}]`         |
| `Nodejs.Version`              | string | no           | `"20"` / `"24"` for Node-SDK plugins                    |
| `Actions`                     | array  | yes          | §1.2                                                    |
| `Category` / `CategoryIcon`   | string | no           | groups actions in the actions list                      |
| `PropertyInspectorPath`       | string | no           | default PI HTML (overridable per-action)                |
| `Profiles`                    | array  | no           | §1.4                                                    |
| `ApplicationsToMonitor`       | object | no           | `{mac:[...], windows:[...]}` → drives `applicationDid*` |
| `URL` / `SupportURL`          | string | no           |                                                         |

> **Vendor-extension fields** (accept leniently, do not reject): `PUUID`,
> `IsK1Pro`, `APIVersion`, `RunAsAdministrator`, and `States[i].FSize`/`FFamily`.
> See `akp_plugin_sdk.md` §2. A manifest validator MUST NOT use
> `additionalProperties:false` against real-world manifests.

### 1.2 `Actions[]` — `Action`

| Field                     | Type   | Req | Default      | Notes                                                                                                                     |
| ------------------------- | ------ | --- | ------------ | ------------------------------------------------------------------------------------------------------------------------- |
| `UUID`                    | string | yes |              | prefixed by plugin UUID                                                                                                   |
| `Name` `Icon`             | string | yes |              | Icon ext omitted; PNG 20²+40²                                                                                             |
| `States`                  | array  | yes |              | 1 or 2; §1.3                                                                                                              |
| `Controllers`             | array  | no  | `["Keypad"]` | items ∈ `Keypad`/`Encoder` (Elgato). **Vendor adds `Knob`/`SecondaryScreen`/`Information`** — normalize `Knob`→`Encoder`. |
| `Encoder`                 | object | no  |              | required when `Controllers` includes `Encoder`; §1.5                                                                      |
| `Tooltip`                 | string | no  |              |                                                                                                                           |
| `PropertyInspectorPath`   | string | no  |              | per-action PI override                                                                                                    |
| `VisibleInActionsList`    | bool   | no  | `true`       |                                                                                                                           |
| `SupportedInMultiActions` | bool   | no  | `true`       |                                                                                                                           |
| `UserTitleEnabled`        | bool   | no  | `true`       |                                                                                                                           |
| `DisableAutomaticStates`  | bool   | no  | `false`      | stop auto state-toggle on keyUp                                                                                           |
| `OS`                      | array  | no  |              | restrict action to a platform                                                                                             |

### 1.3 `States[]` — `State`

1 entry = single-state action; 2 = toggle action. `Image` is **required**
(extension omitted; key image 72²+144²). Other fields: `Name`, `Title`,
`TitleAlignment` (`top`/`middle`/`bottom`), `TitleColor` (hex), `ShowTitle`,
`FontFamily`, `FontSize`, `FontStyle` (`""`/`Regular`/`Bold`/`Italic`/`Bold Italic`),
`FontUnderline`, `MultiActionImage`.

### 1.4 `Profiles[]` — `Profile`

`Name` (path to `.streamDeckProfile`, ext omitted, **required**), `DeviceType`
(number, §6.3, **required**), `DontAutoSwitchWhenInstalled` (default false),
`AutoInstall` (default true), `Readonly` (default false). A plugin may
`switchToProfile` only to a profile it ships — never to a user profile.

### 1.5 `Encoder` (Stream Deck +)

`Icon` (dial canvas, 72²+144²), `background` (touchscreen slot, 200×100+400×200),
`StackColor` (hex), `layout` (predefined `$X1`/`$A0`/`$A1`/`$B1`/`$B2`/`$C1` or a
custom layout-JSON path), `TriggerDescription` (`{Rotate,Push,Touch,LongTouch}`
interaction hints).

______________________________________________________________________

## 2. Registration handshake

### 2.1 Process launch arguments

The host launches the plugin's `CodePath` with **four** args (parse by flag name —
order is not guaranteed):

```
-port <number>          # WebSocket port the host is listening on (loopback)
-pluginUUID <string>    # opaque registration token (NOT the manifest UUID)
-registerEvent <string> # event name to echo back, normally "registerPlugin"
-info <json-string>     # RegistrationInfo (§2.3), JSON in a single shell-escaped arg
```

> **`-pluginUUID` ≠ manifest `UUID`.** It is an opaque per-launch token the host
> mints; the plugin registers with exactly the value it was given. It is **not**
> an auth secret (the legacy doc framing as a "security token" is wrong).

### 2.2 Connect + register

```js
const ws = new WebSocket(`ws://127.0.0.1:${port}`);
ws.onopen = () => ws.send(JSON.stringify({ event: registerEvent, uuid: pluginUUID }));
```

The registration frame is exactly `{ "event": "<registerEvent>", "uuid": "<token>" }`,
no payload. After this the connection is live.

> **No authentication in standard Elgato.** There is no `passHello`, no salt, no
> challenge. The vendor AJAZZ app adds a `passHello`+`sha256(password+salt)`
> handshake (`akp_plugin_sdk.md` §4.5); OpenDeck deliberately omits it. Our engine
> MAY send a `passHello` for vendor-plugin parity but MUST register a standards
> plugin that ignores it (default: no password ⇒ authenticated immediately). Never
> let `passHello` gate a real Elgato plugin.

### 2.3 `-info` payload — `RegistrationInfo`

The complete Elgato shape (a minimal `{application:{version,platform},devices:[]}`
is **insufficient** — version-gated and device-aware plugins read these fields):

```json
{
  "application": {
    "font": "Liberation Sans",
    "language": "en",
    "platform": "mac",
    "platformVersion": "14.5.0",
    "version": "6.9.0.0"
  },
  "colors": {
    "buttonMouseOverBackgroundColor": "#464646FF",
    "buttonPressedBackgroundColor": "#303030FF",
    "buttonPressedBorderColor": "#646464FF",
    "buttonPressedTextColor": "#969696FF",
    "highlightColor": "#0090FFFF"
  },
  "devicePixelRatio": 2,
  "devices": [
    { "id": "<deviceId>", "name": "AJAZZ AKP05E", "size": { "columns": 5, "rows": 2 }, "type": 7 }
  ],
  "plugin": { "uuid": "com.example.counter", "version": "1.0.0.0" }
}
```

- `application.platform` ∈ `mac`/`windows` upstream; we also emit `linux` (parity
  with OpenDeck, which runs on Linux).
- `application.language` ∈ `de|en|es|fr|ja|ko|zh_CN|zh_TW`.
- `devices[].type` is the numeric DeviceType (§6.3). `size` is **action slots
  only** (excludes dials/touchscreen): e.g. SD+ reports `4×2`; AKP05E reports `5×2`.
- `devices` may list currently-disconnected devices — `deviceDidConnect`
  (§3.6) is the authoritative "now active" signal.
- `plugin.uuid`/`plugin.version` are **per-plugin** — the `-info` value must be
  built for the specific plugin being launched, not shared.

______________________________________________________________________

## 3. Events sent TO the plugin (host → plugin)

Action-event envelope: `{ action, context, device, event, payload }`. `context` is
the opaque per-instance handle (§6); `action` is the manifest action UUID; `device`
is the device id.

**Two action-payload shapes:**

- `SingleActionPayload` — `coordinates {column,row}`, `controller`
  (`Keypad`/`Encoder`), `isInMultiAction:false`, `settings`, optional `state`,
  optional `resources` (SD 7.1+).
- `MultiActionPayload` — `controller:"Keypad"`, `isInMultiAction:true`, `settings`,
  optional `state`, **no `coordinates`**.

`coordinates` is 0-indexed. On SD+, dials report `row:0`; disambiguate keys vs dials
by `controller`.

| Event                                              | Payload highlights                                                                                                                                               |
| -------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `didReceiveSettings`                               | instance payload + optional `id` (correlates a `getSettings`)                                                                                                    |
| `didReceiveGlobalSettings`                         | **no context/action/device**: `{event,payload:{settings},id?}`                                                                                                   |
| `keyDown` / `keyUp`                                | instance payload; `controller:"Keypad"`. In a multi-action multi-state context adds `userDesiredState:number` (NOT on normal presses)                            |
| `willAppear` / `willDisappear`                     | instance payload; fires on page/profile/folder enter/leave incl. startup                                                                                         |
| `titleParametersDidChange`                         | single payload **minus `isInMultiAction`**, plus `title` and `titleParameters:{fontFamily,fontSize,fontStyle,fontUnderline,showTitle,titleAlignment,titleColor}` |
| `deviceDidConnect`                                 | top-level `{device,event,deviceInfo:{name,size:{columns,rows},type}}`                                                                                            |
| `deviceDidDisconnect`                              | top-level `{device,event}` (no deviceInfo)                                                                                                                       |
| `applicationDidLaunch` / `applicationDidTerminate` | `{event,payload:{application}}` — only for `ApplicationsToMonitor` entries                                                                                       |
| `systemDidWakeUp`                                  | `{event}`                                                                                                                                                        |
| `propertyInspectorDidAppear` / `…DidDisappear`     | `{action,context,device,event}` — **no payload**                                                                                                                 |
| `sendToPlugin`                                     | `{action,context,event,payload}` — **no `device`** (from the PI)                                                                                                 |
| `dialRotate`                                       | encoder payload + `pressed:bool`, `ticks:number` (signed; + = CW)                                                                                                |
| `dialDown` / `dialUp`                              | encoder payload (no `dialPress` event exists)                                                                                                                    |
| `touchTap`                                         | encoder payload + `hold:bool`, `tapPos:[x,y]` (canvas-relative)                                                                                                  |

Encoder payload = `{controller:"Encoder", coordinates, settings, resources?}` plus
the event-specific fields above.

Newer (SD 7.x, treat as optional): `didReceiveDeepLink` (`payload.url`),
`didReceiveResources`, `deviceDidChange`, `didReceiveSecrets`.

______________________________________________________________________

## 4. Commands sent FROM the plugin (plugin → host)

Contextualized commands carry `context`; plugin-scoped ones do not.

| Command                   | Shape                                                                                                           |
| ------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `setSettings`             | `{context,event,payload:{...}}` → echoes `didReceiveSettings`                                                   |
| `getSettings`             | `{context,event,id?}` → `didReceiveSettings`                                                                    |
| `setGlobalSettings`       | `{context,event,payload:{...}}` (context = registration uuid)                                                   |
| `getGlobalSettings`       | `{context,event,id?}` → `didReceiveGlobalSettings`                                                              |
| `openUrl`                 | `{event,payload:{url}}` (no context)                                                                            |
| `logMessage`              | `{event,payload:{message}}`                                                                                     |
| `setTitle`                | `{context,event,payload:{title?,target?,state?}}` (omit `title`→reset, omit `state`→both)                       |
| `setImage`                | `{context,event,payload:{image?,target?,state?}}` — `image` = plugin-relative path OR `data:image/png;base64,…` |
| `setState`                | `{context,event,payload:{state}}` (0 or 1)                                                                      |
| `setFeedback`             | \`{context,event,payload:{<layout-key>:value                                                                    |
| `setFeedbackLayout`       | `{context,event,payload:{layout}}` (SD+; `$X1`/`$A0`/`$A1`/`$B1`/`$B2`/`$C1` or path)                           |
| `setTriggerDescription`   | `{context,event,payload:{rotate?,push?,touch?,longTouch?}}` (SD+)                                               |
| `showAlert` / `showOk`    | `{context,event}`                                                                                               |
| `switchToProfile`         | `{context,device,event,payload:{profile?,page?}}` (empty profile = previous)                                    |
| `sendToPropertyInspector` | `{context,event,payload:{...}}`                                                                                 |

`Target` enum (`setTitle`/`setImage`/`setState`): **numeric** `0`=HardwareAndSoftware
(default), `1`=Hardware, `2`=Software. (Prose docs saying "hardware/software/both"
strings are wrong.)

> OpenAction/OpenDeck inbound aliases to accept: `switchProfile` (=`switchToProfile`).
> Device-driver inbound (from the sidecar, not action plugins): `registerDevice`,
> `deregisterDevice`, `keyDown`/`keyUp`, `encoderChange` (→outbound `dialRotate`),
> `encoderDown`/`encoderUp`, `rerenderImages`, `deviceBrightness`.

______________________________________________________________________

## 5. Property Inspector (dual-WebSocket model)

The PI is an HTML page (`PropertyInspectorPath`, manifest- or action-level) rendered
in an embedded webview. **It is a SECOND, INDEPENDENT WebSocket client** to the same
host server — separate from the plugin, with its own registration and its own
`context`.

### 5.1 Entry point (5 args — one more than the plugin)

```js
window.connectElgatoStreamDeckSocket =
  function (inPort, inUUID, inRegisterEvent, inInfo, inActionInfo) {
    const info       = JSON.parse(inInfo);        // RegistrationInfo (§2.3)
    const actionInfo = JSON.parse(inActionInfo);  // bound instance (§5.3)
    const ws = new WebSocket(`ws://127.0.0.1:${inPort}`);
    ws.onopen = () => ws.send(JSON.stringify({ event: inRegisterEvent, uuid: inUUID }));
  };
```

- `inRegisterEvent` = `"registerPropertyInspector"`.
- `inUUID` = the **action-instance `context`** the PI is bound to (NOT the plugin
  UUID). The PI uses this as `context` on every command it sends.

### 5.2 Routing

- **PI → plugin:** PI sends `{action,context,event:"sendToPlugin",payload}`; the
  plugin receives `sendToPlugin` (§3, no `device`).
- **plugin → PI:** plugin sends `sendToPropertyInspector` (§4); the PI receives
  `{action,context,event:"sendToPropertyInspector",payload}`.
- The PI may also send `setSettings`/`getSettings`/`set|getGlobalSettings`/
  `openUrl`/`logMessage` (its instance context). It receives `didReceiveSettings`
  and `didReceiveGlobalSettings`. The PI cannot drive the key
  (`setImage`/`setTitle` are the plugin's job).
- The host emits `propertyInspectorDidAppear`/`…DidDisappear` to the **plugin** when
  a PI opens/closes for one of its instances.

### 5.3 `inActionInfo`

A `willAppear`-style message identifying the bound instance:

```json
{ "action":"com.example.counter.increment", "context":"ABC123", "device":"DEV1",
  "payload": { "controller":"Keypad", "coordinates":{"column":3,"row":1},
               "isInMultiAction":false, "settings":{"count":4}, "state":0 } }
```

> **Engine note.** A from-scratch host MUST track the PI as a distinct connection
> keyed by its instance `context` (NOT collapse it onto the plugin's socket). Our
> current `SdPluginServer` mishandles `registerPropertyInspector` as a second
> `registerPlugin` and closes the PI as a duplicate-UUID impostor — see
> [`PLUGIN-GAP-ANALYSIS.md`](../../architecture/PLUGIN-GAP-ANALYSIS.md) F3. The
> bundled PIs sidestep this via a QWebChannel `$SD` bridge, but a stock `.sdPlugin`
> PI that uses the WS handshake will not connect until this is fixed.

______________________________________________________________________

## 6. Context / coordinates instance model

- **`context`** — opaque per-**instance** handle minted by the host for each placed
  action (each key/dial slot, each multi-action entry). The routing key for
  everything. Different placements of the same `action` UUID get different
  `context`s. Treat as opaque; never parse. The PI shares its instance's `context`.
- **`action`** — manifest action UUID; identifies the type, not the instance.
- **`device`** — device id; pair with `coordinates` to locate a physical slot;
  required for `switchToProfile`.
- **`coordinates {column,row}`** — 0-indexed slot within the device `size`; present
  on single-action instances only. SD+ dials always `row:0`.
- **`controller`** — `Keypad` vs `Encoder`.

**Multi-actions:** `isInMultiAction:true`, **no `coordinates`**, `controller`
forced to `Keypad`. Multi-state actions add `userDesiredState` on `keyDown`/`keyUp`.

Canonical lifecycle: track instances by `context` from `willAppear`, drop on
`willDisappear`. A `context` never survives a page/profile switch.

______________________________________________________________________

## 7. Device-driver split (sidecar ↔ core)

OpenDeck's best decision, and ours: **the hardware driver knows nothing about
actions, profiles, or Elgato event names.** Our mirajazz sidecar (`streamdock-host`,
JSON-over-stdio) is the device driver. It reports raw input
(`{device,position,ticks}`-style) and accepts raw output (`setImage`,
`deviceBrightness`). The **C++ core** owns the `(device,position)`→bound-action
lookup and synthesizes the rich Elgato `keyDown`/`dialRotate` events with
`context`/`coordinates`/`settings`. Image pipeline: core scales format-agnostically;
the sidecar applies device-specific encode (`ImageMode::JPEG` + `ImageRotation` +
per-key size) via mirajazz `set_button_image`. Route output by a stable device-id
scheme so multi-device/hotplug is unambiguous (OpenDeck uses a 2-char namespace
prefix).

______________________________________________________________________

## 8. Version gates worth hard-coding

| Capability                                                   | Min SD |
| ------------------------------------------------------------ | ------ |
| `controller` on `willAppear`/`willDisappear` multi-action    | 6.5    |
| `isInMultiAction` exposed to PI                              | 6.7    |
| `deviceDidChange`, `CodePath*` override behavior             | 7.0    |
| `resources` payload, `didReceiveResources`, get/setResources | 7.1    |

Treat `resources` as optional; ignore if absent. We emulate **6.9** — gate-aware
plugins targeting ≤6.9 run; 7.x-only payload additions are simply not emitted.

### 6.3 DeviceType enum (for `info.devices[].type` and `Profile.DeviceType`)

| #   | Device              | Slots                 |
| --- | ------------------- | --------------------- |
| 0   | Stream Deck         | 5×3                   |
| 1   | Stream Deck Mini    | 3×2                   |
| 2   | Stream Deck XL      | 8×4                   |
| 5   | Stream Deck Pedal   | 3                     |
| 7   | Stream Deck +       | 4×2 + touch + 4 dials |
| 9   | Stream Deck Neo     | 4×2 + 2 touch         |
| 10  | Stream Deck Studio  | 16×2 + 2 dials        |
| 11  | Virtual Stream Deck | ≤8×8                  |

> **AJAZZ mapping.** We advertise AKP05E/N4 as `type:7` (Stream Deck +: key grid +
> dials + touch strip), since that is the closest Elgato geometry a plugin will
> reason about. AKP153/AKP03 (key-only) map to a key-grid type. The advertised
> `size` MUST be the device's real action-slot grid from its `DeviceDescriptor`
> (`gridColumns` × `keyRows`), NOT a hardcoded constant.

______________________________________________________________________

## 9. Correctness checklist for the engine

1. `-info` MUST carry the full §2.3 shape with **real** `devices[]` geometry and a
   **per-plugin** `plugin{}` block. A minimal payload breaks version/device-gated
   plugins.
1. `resources` is 7.1+ and optional — never require or emit for ≤6.9 targets.
1. `userDesiredState` only on multi-action multi-state `keyDown`/`keyUp`.
1. Multi-action payloads have no `coordinates`; force `controller:"Keypad"`.
1. `get*Settings` carry optional `id` echoed on the matching `didReceive*`.
1. `Target` is numeric (0/1/2).
1. The PI is a **separate** WS connection registering with the instance `context`
   via `registerPropertyInspector`; its entry point has **5** args.
1. Parse launch args by flag name; `-info` is JSON-in-a-string.
1. `setTitle`/`setImage`/`setState` `state` only matters for multi-state; omitting
   it targets all states.
1. Coordinate ↔ key-index conversion MUST source columns from the active device's
   `DeviceDescriptor.gridColumns`, never a literal.
