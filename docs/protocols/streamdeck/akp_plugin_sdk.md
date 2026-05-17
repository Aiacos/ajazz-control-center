# AJAZZ Stream Dock plugin SDK — Elgato-compatible

> Deep RE pass against `Stream Dock AJAZZ.exe` plugin manager
> (`SDPluginManager`) and `SDLibrary1.dll` plugin server (`SDPluginServer`),
> `SDTcpServer`, `SDWebsocketServer`, plus first-party plugins under
> `defaultData/defaultPlugins/`. 2026-05-17. Companion to
> [`akp05_vendor.md`](./akp05_vendor.md), [`akp05_init_sequence.md`](./akp05_init_sequence.md).
>
> The SDK is **almost verbatim Elgato Stream Deck v6**, with a small
> set of AJAZZ-specific extensions. Existing `.sdPlugin` packages
> built for Elgato hardware load on Stream Dock with no edits in most
> cases; the few divergences are documented in §3.

## 1. Bundled default plugins

`<install>/defaultData/defaultPlugins/` contains **12 plugins** (the
"11 default plugins" callout in our prior planning was off by one):

| #   | UUID prefix                               | Name                         | One-line description                                       | Language      |
| --- | ----------------------------------------- | ---------------------------- | ---------------------------------------------------------- | ------------- |
| 1   | `com.hotspot.streamdock.memo`             | Useful notes                 | Per-key notepad + countdown to-do (`action1`, `action2`)   | HTML+JS (CEF) |
| 2   | `com.hotspot.streamdock.myHeadline`       | 我的头条 (News/Stocks/Rates) | Headlines, finance news, Weibo, CN stocks, FX rates        | HTML+JS (CEF) |
| 3   | `com.hotspot.streamdock.system.monitor`   | System Monitor               | CPU/RAM/Disk/Net/GPU stats (`RunAsAdministrator: true`)    | Native `.exe` |
| 4   | `com.mirabox.streamdock.calendar`         | calendar                     | Today's date/lunar/holiday info                            | HTML+JS (CEF) |
| 5   | `com.mirabox.streamdock.dateTime`         | DateTime                     | Configurable date/time clock face                          | HTML+JS (CEF) |
| 6   | `com.mirabox.streamdock.emoji`            | emoji                        | Emoji palette + send action (`Nodejs.Version: 20`)         | Node.js 20    |
| 7   | `com.mirabox.streamdock.pictureEmoticons` | Emoticons                    | Picture emoticon library + select (`Nodejs.Version: 20`)   | Node.js 20    |
| 8   | `com.mirabox.streamdock.PR`               | Premiere Pro                 | 15 hotkey actions for Adobe Premiere Pro                   | Native `.exe` |
| 9   | `com.mirabox.streamdock.switchAudio`      | 切换音频设备                 | Windows audio output device switcher                       | Native `.exe` |
| 10  | `com.mirabox.streamdock.time`             | Time Options                 | World time / timer / countdown                             | HTML+JS (CEF) |
| 11  | `com.mirabox.streamdock.weather`          | Weather query                | Per-city weather lookup                                    | HTML+JS (CEF) |
| 12  | `mkey.com.mirabox.streamdock.calendar`    | Calendar (K1Pro)             | Calendar variant for the K1 Pro keyboard — `IsK1Pro: true` | HTML+JS (CEF) |

The `mkey.*` prefix marks "**plugins for AJAZZ mechanical keyboards**"
(K1 Pro, K-992, etc.) — these load only when the connected device's
codename matches the `IsK1Pro` flag on the action. See
`mkey.com.mirabox.streamdock.calendar.sdPlugin/manifest.json` for the
exemplar.

In addition to the 12 above, the main `Stream Dock AJAZZ.exe` declares
~50 **built-in UUIDs** that are handled in-process (no separate
plugin folder, no node20 spawn) — these are the page navigation,
profile navigation, brightness control, mouse-event, multi-actions,
HTTP browser, plain-text, hotkey, multimedia, OBS Studio, and YouTube
actions. Full UUID list per `Stream Dock AJAZZ.exe` strings:

```
com.hotspot.streamdock.browser
com.hotspot.streamdock.device.brightness
com.hotspot.streamdock.device.k1proLED+-
com.hotspot.streamdock.mouse.event
com.hotspot.streamdock.multiactions
com.hotspot.streamdock.multiactions.LunBo       (multi-action carousel)
com.hotspot.streamdock.network
com.hotspot.streamdock.obsstudio
com.hotspot.streamdock.page.previous
com.hotspot.streamdock.page.next
com.hotspot.streamdock.page.goto
com.hotspot.streamdock.page.indicator
com.hotspot.streamdock.page.change             (Knob controller)
com.hotspot.streamdock.plain.text
com.hotspot.streamdock.profile.openchild
com.hotspot.streamdock.profile.backtoparent
com.hotspot.streamdock.profile.rotate
com.hotspot.streamdock.system.hotkey
com.hotspot.streamdock.system.multimedia
com.hotspot.streamdock.system.volume
com.hotspot.streamdock.vmix                    (vMix integration)
com.hotspot.streamdock.youtube                 (YouTube integration)
com.hotspot.streamdock.pageindicatororgoto
com.hotspot.streamdock.pagebackorforword       (note: 'forword' is the actual vendor typo)
```

## 2. Plugin manifest schema

The schema is **Elgato Stream Deck SDK v6** with three AJAZZ-only
fields. Top-level keys (cross-referenced from the 12 default
manifests):

| Key                  | Type   | Required       | Notes / source                                                                                     |
| -------------------- | ------ | -------------- | -------------------------------------------------------------------------------------------------- |
| `Name`               | string | yes            | Display name (also accepts CJK)                                                                    |
| `Author`             | string | yes            | Author label                                                                                       |
| `Version`            | string | yes            | SemVer-ish, e.g. `"1.0.0"` or `"2.0.1"`                                                            |
| `Icon`               | string | yes            | Category icon path relative to plugin root                                                         |
| `Category`           | string | yes            | Category label shown in plugin browser                                                             |
| `CategoryIcon`       | string | yes            | Category icon (small)                                                                              |
| `Description`        | string | yes            | Long description                                                                                   |
| `URL`                | string | no             | Author homepage                                                                                    |
| `SDKVersion`         | int    | yes            | Always `1` in shipped manifests                                                                    |
| `OS`                 | array  | yes            | `[{Platform: "mac"\|"windows", MinimumVersion: "X.Y"}]`                                            |
| `Software`           | object | no             | `{MinimumVersion: "2.9"}` — minimum Stream Dock app version                                        |
| `CodePath`           | string | yes (or split) | Default executable / index file path                                                               |
| `CodePathWin`        | string | no             | Windows-specific override (e.g. `streamdockSwitchAudio.exe`)                                       |
| `CodePathMac`        | string | no             | macOS-specific override                                                                            |
| `Nodejs`             | object | no             | `{Version: "20"}` — request node20.exe child spawn instead of CEF                                  |
| `RunAsAdministrator` | bool   | no             | Spawn with UAC elevation (system.monitor sets this)                                                |
| `Actions`            | array  | yes            | List of action descriptors (see below)                                                             |
| `APIVersion`         | string | no             | Plugin-author marker, vendor reads it but does not enforce (dateTime uses `"1.0"`)                 |
| `PUUID`              | string | no             | Author-supplied "plugin UUID" alias — vendor reads but does not surface (weather uses `"Weather"`) |

Per-action descriptor:

| Key                        | Type   | Required | Notes                                                                                                                                                                      |
| -------------------------- | ------ | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `UUID`                     | string | yes      | Reverse-DNS, e.g. `com.hotspot.streamdock.memo.action1`                                                                                                                    |
| `Name`                     | string | yes      | Display name (can be CJK)                                                                                                                                                  |
| `Icon`                     | string | yes      | Icon path                                                                                                                                                                  |
| `Tooltip`                  | string | no       | Hover tooltip                                                                                                                                                              |
| `States`                   | array  | yes      | One state object per per-key state. Empty `[{}]` is valid.                                                                                                                 |
| `States[i].Image`          | string | no       | Default key image path                                                                                                                                                     |
| `States[i].FontSize`       | string | no       | e.g. `"10"`. Yes, a string, not int.                                                                                                                                       |
| `States[i].FSize`          | string | no       | Mirabox-specific synonym for `FontSize` (system.monitor uses it)                                                                                                           |
| `States[i].FFamily`        | string | no       | Mirabox-specific synonym for `FontFamily`                                                                                                                                  |
| `States[i].FontFamily`     | string | no       | Elgato-standard                                                                                                                                                            |
| `States[i].FontStyle`      | string | no       | `"Bold"`                                                                                                                                                                   |
| `States[i].TitleColor`     | string | no       | `"#RRGGBB"`                                                                                                                                                                |
| `States[i].TitleAlignment` | string | no       | `"top"`, `"middle"`, `"bottom"`, `"center"`                                                                                                                                |
| `Settings`                 | object | no       | Default settings JSON. PR plugin embeds a `VKeyCode`/`KeyCtrl`/… mapping for system-hotkey actions.                                                                        |
| `Controllers`              | array  | no       | Subset of `["Keypad", "Information", "SecondaryScreen", "Knob"]`. `SecondaryScreen` opts the action into rendering on the touch strip; `Knob` makes it bind to an encoder. |
| `DisableAutomaticStates`   | bool   | no       | If true, the host won't toggle state on press (manual state mgmt)                                                                                                          |
| `SupportedInMultiActions`  | bool   | no       | Defaults false                                                                                                                                                             |
| `UserTitleEnabled`         | bool   | no       | Defaults true; if false, user can't override title text                                                                                                                    |
| `VisibleInActionsList`     | bool   | no       | Defaults true; emoji_send action sets false (used internally by emoji action)                                                                                              |
| `PropertyInspectorPath`    | string | no       | Relative HTML for the Property Inspector pane                                                                                                                              |
| `IsK1Pro`                  | bool   | no       | **AJAZZ extension** — only show on K1Pro family devices                                                                                                                    |

### 2.1 Differences from Elgato

| Field                | Elgato? | AJAZZ? | Note                                                                                                      |
| -------------------- | ------- | ------ | --------------------------------------------------------------------------------------------------------- |
| `Nodejs.Version`     | yes     | yes    | Same shape; AJAZZ bundles `node20.exe` instead of leaving it to PATH                                      |
| `Controllers`        | yes     | yes    | AJAZZ adds `"Information"` and `"SecondaryScreen"` as accepted values (`Keypad`, `Knob` already standard) |
| `States[i].FSize`    | no      | yes    | Mirabox shortcut for FontSize (used in system.monitor manifest)                                           |
| `States[i].FFamily`  | no      | yes    | Mirabox shortcut for FontFamily                                                                           |
| `IsK1Pro`            | no      | yes    | AJAZZ-only — gates an action to the K1 Pro keyboard family                                                |
| `APIVersion`         | no      | yes    | Author hint; not enforced                                                                                 |
| `PUUID`              | no      | yes    | Author-supplied plugin UUID alias                                                                         |
| `RunAsAdministrator` | no      | yes    | UAC elevation on spawn (Windows only)                                                                     |

## 3. Plugin lifecycle

Per `SDPluginManager` symbols and `loadPlugin`/`LoadPrivatePlugin` /
`restartPlugin` / `removePluginCode` decompilation (see
`streamdock_exe_strings.txt:759001..759032`):

1. **Discovery**:
   - `<install>/defaultData/defaultPlugins/*.sdPlugin/manifest.json`
   - `%APPDATA%/HotSpot/Stream Dock AJAZZ/installedPlugins/*.sdPlugin/manifest.json`
   - On startup, `<install>/defaultData/defaultPlugins/defaultPlugins.zip`
     is extracted to `installedPlugins/` if `installedPlugins/` is
     missing or stale (`startUnPackage` in `ZipWorker`, RVA `0x180181104`).
1. **Manifest validation**: rejects if `OS` array does not contain
   the current platform or if `Software.MinimumVersion` exceeds the
   running app's version.
1. **Spawn**:
   - `.exe` / `.app` / `<no-ext>` → `QProcess::start(codePath, [])`
     with optional UAC elevation if `RunAsAdministrator: true`.
   - `.mjs` / `.cjs` / `.js` → `QProcess::start("<install>/node/node20.exe", [codePath, "-port", PORT, "-pluginUUID", UUID, "-registerEvent", "registerPlugin", "-info", INFO_JSON])`.
   - `.html` → loaded directly in an in-process `QCefView` widget; the
     JS calls `connectElgatoStreamDeckSocket(port, uuid, "registerPlugin", info, actionInfo)` to connect back over WS.
1. **WebSocket handshake** (driven by the JS shim, but C++ side mirrors):
   - The plugin opens `ws://127.0.0.1:<port>` and immediately sends
     `{"event": "registerPlugin", "uuid": "<pluginUUID>"}`.
   - Host responds with a `passHello` event carrying device list,
     application info, and authentication challenge (see §5.1).
1. **Property Inspector**: for actions with `PropertyInspectorPath`,
   when the user clicks the action in the UI, the host loads the
   relative HTML in a CEF `QWidget` and the PI script calls
   `connectElgatoStreamDeckSocket(port, uuid, "registerPropertyInspector", info, actionInfo)`. PI runs in-process
   in CEF; the plugin process and PI talk via the `sendToPropertyInspector`
   / `sendToPlugin` event pair which the host relays.
1. **Crash policy**: `QProcess::ProcessError` handler logs
   `"The plugin '%1' crashed: %2"` and the host counts crashes. After
   3 within ~30 s, the plugin is disabled and the user gets a
   notification in the in-app notification panel
   (`SDNotificationWidget`, RVA `0x180181104`).
1. **Restart**: user triggers via "Stop plugin" / restart in the UI.
   `SDPluginManager::restartPlugin(QString uuid)` calls `removePluginCode`
   then `loadPlugin` again.
1. **Shutdown**: at app exit, every plugin process gets a `QProcess::terminate()`
   (grace 1 s) then `kill()`. The WebSocket sends `{event: "exitApp"}`
   (string at offset `25121`) before terminating so plugins can persist
   state.

## 4. Transport — WebSocket + TCP dual stack

### 4.1 Servers

Both bound by `SDPluginServer::startListen(uint16_t port)`:

- **`SDWebsocketServer`** (RVA `0x1800379a0`) — `QWebSocketServer`.
  The main plugin transport.
- **`SDTcpServer`** (RVA `0x1800360c0`) — `QTcpServer` listening on
  the same port (different namespace via Qt — note Qt's QTcpServer
  and QWebSocketServer can coexist on adjacent free ports). Used by
  the few legacy plugins that did not pull in a WebSocket library.

Each binds to **`QHostAddress::Any`** (security-sensitive — see
[`akp05_init_sequence.md`](./akp05_init_sequence.md) §5).

The random port pool: `Utilities::getsARandomServerPort()` is called
in a `do { … } while (port == previous)` loop with `QTimer::singleShot(500ms)`
backoff if the requested port is in use. So device ports across runs
are not stable.

### 4.2 Wire format

Every message is a JSON object encoded as a single WebSocket text
frame. The envelope has the same keys as Elgato:

```json
{
  "event":   "<event-name>",
  "context": "<opaque-context-id>",
  "device":  "<device-id>",
  "action":  "<action-UUID>",          // present for action-bound events
  "payload": { … event-specific … }
}
```

Verified envelope keys present in the SDK strings (`event`, `context`,
`payload`, `action`, `device`, `controller`, `coordinates`,
`deviceCoordinates`, `point`, `pressed`, `ticks`, `actionLocation`):
all at offsets `759044..759131`.

### 4.3 Plugin → Host actions (verified from `SDPluginServer` decompile)

From `streamdock_exe_strings.txt:759099..759131`:

| Action                                                            | Standard Elgato? | Notes                                                                                                       |
| ----------------------------------------------------------------- | ---------------- | ----------------------------------------------------------------------------------------------------------- |
| `registerPlugin`                                                  | yes              | First message; `uuid` field carries plugin UUID                                                             |
| `registerPropertyInspector`                                       | yes              | Variant for PI registration                                                                                 |
| `setTitle`                                                        | yes              | Payload: `{title, target, state?}`. `target` ∈ {0=hw+sw, 1=hw, 2=sw}                                        |
| `setImage`                                                        | yes              | Payload: `{image: "<base64-png/jpg-data-uri>", target, state?}`                                             |
| `setState`                                                        | yes              | Payload: `{state}` toggles per-key state                                                                    |
| `setBG`                                                           | **AJAZZ-only**   | Payload: `{color: "#RRGGBB"}` — set per-key background color. Used by memo todo blink animation.            |
| `showAlert`                                                       | yes              | No payload                                                                                                  |
| `showOk`                                                          | yes              | No payload                                                                                                  |
| `getSettings` / `setSettings`                                     | yes              | Per-context settings JSON                                                                                   |
| `getGlobalSettings` / `setGlobalSettings`                         | yes              | Plugin-wide settings JSON                                                                                   |
| `switchToProfile`                                                 | yes              | Payload: `{profile}`. Requires `device` field set                                                           |
| `openTouchbarSecondaryMenu` / `exitTouchbarSecondaryMenu`         | **AJAZZ-only**   | Open/close a context menu on the touch strip; payload: `{actions: [...]}`                                   |
| `enterGatheringEvent`                                             | **AJAZZ-only**   | Tells the host to start aggregating gestures and feed them as one event                                     |
| `sendToPropertyInspector`                                         | yes              | Pass-through to the PI's WS connection                                                                      |
| `sendToPlugin`                                                    | yes              | PI → plugin; host routes it                                                                                 |
| `sendToDevice`                                                    | **AJAZZ-only**   | Plugin requests host to forward a raw byte sequence to the device HID write queue                           |
| `openUrl`                                                         | yes              | Payload: `{url}` opens in system browser                                                                    |
| `logMessage`                                                      | yes              | Payload: `{message}`                                                                                        |
| `setBackground`                                                   | **AJAZZ-only**   | Sets the device-wide background (different from per-key `setBG`)                                            |
| `clearIcon`                                                       | **AJAZZ-only**   | Clears the current key icon to the system default                                                           |
| `registrationScreenSaverEvent` / `unRegistrationScreenSaverEvent` | **AJAZZ-only**   | Opt the plugin into/out of screensaver lifecycle events                                                     |
| `setFeedback`                                                     | yes              | Stream Deck Plus encoder feedback                                                                           |
| `setText`                                                         | **AJAZZ-only**   | Set a text overlay on the encoder/strip                                                                     |
| `getUserInfo`                                                     | **AJAZZ-only**   | Plugin requests the logged-in user's Mirabox account info (returns `loginName`, `loginID`, `loginImageUrl`) |
| `startAudioCapture` / `stopAudioCapture`                          | **AJAZZ-only**   | Host-side audio routing; used by switchAudio plugin                                                         |
| `getScreenshot`                                                   | **AJAZZ-only**   | Host returns a JPEG screenshot of the current screen                                                        |
| `getSystemAudioVolume`                                            | **AJAZZ-only**   | Returns `{leftChannel, rightChannel}`                                                                       |
| `setAcImgTop`                                                     | **AJAZZ-only**   | Set the "always-on top" image layer (touch strip)                                                           |
| `onSwitchToFolderProfile` / `onSwitchFromFolderProfile`           | **AJAZZ-only**   | Plugin notifies host of folder nav (used by `profile.openchild`)                                            |
| `exitFullScreen`                                                  | **AJAZZ-only**   | Plugin requests host to exit fullscreen device mode                                                         |
| `touchTap`                                                        | **AJAZZ-only**   | Synthesise a touch tap (host-injected — debugging/testing)                                                  |
| `getDetectedSensorsData`                                          | **AJAZZ-only**   | System monitor plugin queries sensor data from host                                                         |
| `deleteAction`                                                    | **AJAZZ-only**   | Plugin requests host to remove the action from the current profile                                          |
| `stopBackground`                                                  | **AJAZZ-only**   | Stops the per-device background image animation                                                             |
| `lockScreen` / `unLockScreen`                                     | **AJAZZ-only**   | Lock/unlock the device screen (touch input still works on lockScreen)                                       |
| `sendUserInfo`                                                    | **AJAZZ-only**   | Host pushes `{loginImageUrl, loginName, loginID}` to plugin after login                                     |

### 4.4 Host → Plugin events (verified from same decompile)

From `streamdock_exe_strings.txt:759044..759098`:

| Event                                                          | Standard Elgato? | Payload notes                                                                                                                                          |
| -------------------------------------------------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `passHello`                                                    | **AJAZZ-only**   | Sent immediately after the client's `registerPlugin`. Carries `{device, deviceInfo, authentication: {challenge, salt}, …}`. Client must complete auth. |
| `keyDown` / `keyUp`                                            | yes              | Payload: `{settings, coordinates: {row,column}, state, isInMultiAction}`                                                                               |
| `keyDownCord` / `keyUpCord`                                    | **AJAZZ-only**   | Knob/encoder press as the controller class `"Cord"` (AJAZZ legacy name; modern API is `dialDown`/`dialUp`)                                             |
| `dialDown` / `dialUp` / `dialRotate`                           | yes (SD+)        | Payload: `{ticks, pressed, controller, deviceCoordinates: {point, width, height}, settings}`                                                           |
| `touchTap`                                                     | yes (SD+)        | Payload: `{x, y, hold, settings}`                                                                                                                      |
| `willAppear` / `willDisappear`                                 | yes              | Carries `{settings, coordinates, state, isInMultiAction}`                                                                                              |
| `propertyInspectorDidAppear` / `propertyInspectorDidDisappear` | yes              | No payload                                                                                                                                             |
| `titleParametersDidChange`                                     | yes              | Payload: `{titleParameters: {fontFamily, fontSize, fontStyle, fontUnderline, showTitle, titleAlignment, titleColor}, title}`                           |
| `deviceDidConnect` / `deviceDidDisconnect`                     | yes              | Payload: `{deviceInfo: {name, type, size}}`                                                                                                            |
| `systemDidWakeUp`                                              | yes              | No payload                                                                                                                                             |
| `applicationDidLaunch` / `applicationDidTerminate`             | yes              | Payload: `{application: "<exe-path>"}`                                                                                                                 |
| `didReceiveSettings` / `didReceiveGlobalSettings`              | yes              | Settings round-trip ack                                                                                                                                |
| `sendToPlugin` / `sendToPropertyInspector`                     | yes              | Cross-pair message relay                                                                                                                               |
| `deleteAction`                                                 | **AJAZZ-only**   | Host tells the plugin "this action is going away"                                                                                                      |
| `lockScreen` / `unLockScreen`                                  | **AJAZZ-only**   | Mirror of the action; host can also push these to the plugin                                                                                           |

### 4.5 Authentication handshake

Verified by strings (offsets in `sdlibrary1_strings.txt`):

- `authentication` (25107)
- `challenge` (25118)
- `salt` (25119)
- `auth` (25131)
- `token` (25132)
- `userInfo` (25133)
- `hello` (25104)
- `passHello` (25128)

Sequence:

1. Plugin connects WS, sends `{"event": "registerPlugin", "uuid": "<pluginUUID>"}`.
1. Host evaluates: if the plugin is in its installed list → respond
   with `{"event": "passHello", "device": "…", "deviceInfo": {…}, "salt": "<random>"}`.
   If not, host immediately closes the connection.
1. (Optional, only when password-protected) Plugin computes
   `secret = sha256(password + salt)` and sends `{"event": "authentication", "challenge": "<secret>"}`.
1. Host verifies and either keeps the connection or closes it.

There is **no TLS** — by design (the connection is local-only despite
the listener binding all interfaces, per the JS shim hardcoding
`ws://127.0.0.1:<port>`).

## 5. setImage end-to-end

When a JS plugin calls `$SD.setImage(context, image, target)`:

1. JS shim wraps to `{event: "setImage", context, payload: {image: "data:image/png;base64,…", target: 0}}` and `WebSocket.send()`.
1. Host receives via `SDPluginServer::onTextMessageReceived`.
1. Host parses JSON; resolves `context` to a `(deviceId, page, slot)`
   tuple from its in-memory action map.
1. Host strips the `data:image/png;base64,` prefix, base64-decodes
   the rest, runs it through `QImage::loadFromData` then through a
   QImage scaler to the device's per-key dimensions (e.g. 85×85 for
   AKP05).
1. Host re-encodes as JPEG (or PNG depending on device — see
   `DataFormatConversion` symbols), via `QImage::save` with format
   `"JPG"` and quality 85.
1. Host calls `SDDevice::appendSendData(ImageStruct&, false)` which
   appends to the per-device `_writeDataList` and wakes
   `_writeDataWaitCondition`.
1. The write thread `SDDevice::writeDataToHidDevice` dequeues, splits
   the JPEG into `_packetSize`-byte chunks, prepends the report ID,
   and calls `hid_write` for each chunk.

Image format **expected by the device**: JPEG (NOT PNG) for the
AKP153/AKP815/AKP03/AKP05 families. PNG accepted by the host and
re-encoded; large PNGs cause extra latency.

Fallback: if the host fails to load the image, it sends a placeholder
"X" via `setTitle` instead — no failure event back to the plugin.

## 6. Plugin store / Mirabox Space

Per strings `streamdock_exe_strings.txt:760614..760617`,
`760615..760617`, `760687..760695`:

- Plugin index lives at `https://space.mirabox.com/streamdock` (which
  redirects to `https://hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com`
  per `akp05_vendor.md` §8).
- Plugin ZIP fetch URL pattern:
  `https://hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com/plugin/<category>/<plugin-name>/<plugin-name>.zip`
- Plugin metadata fields per `streamdock_exe_strings.txt:760172..760199`:
  `author`, `avatar`, `cartId`, `collection`, `dialSupport`,
  `discountPrice`, `discountPriceRmb`, `freeMembership`, `frequency`,
  `frontPlugin`, `gallery`, `isAudio`, `isCollection`, `isRecommend`,
  `isThumbUp`, `news`, `overview`, `owned`, `price`, `priceRmb`,
  `productType`, `reason`, `reply`, `seniorFrequency`, `superFrequency`.
- **No signature verification** observed — the host extracts the ZIP
  and trusts it. This is a security gap we must not replicate.

## 7. Property Inspector

Property Inspector is the per-action settings UI rendered in CEF. JS
shim handshake:

```js
function connectElgatoStreamDeckSocket(inPort, inUUID,
                                       inMessageType,    // = "registerPropertyInspector"
                                       inApplicationInfo,
                                       inActionInfo) {
    // inActionInfo is the full action payload (UUID, settings, coordinates, …)
    StreamDeck.getInstance().connect(arguments);
    // …
}
```

The PI sees a richer initial event:
`{event: "registerPropertyInspector", uuid: "<pluginUUID>"}` is sent
first; the host responds with `{event: "passHello"}` + the action's
current settings via `didReceiveSettings`.

The PI can call any of `sendToPlugin`, `setSettings`,
`getGlobalSettings`, `openUrl`, `logMessage`. The plugin process
mirrors with `sendToPropertyInspector`.

## 8. QCefView bridge — JS ↔ C++ binding pattern

QCefView.dll exports a small set of methods for JS-to-C++ method
calls. From `qcefview_strings.txt`:

| Symbol                                                                                        | Purpose                                                                                     |
| --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `QCefView::invokeMethod(int browserId, qint64 frameId, QString method, QList<QVariant> args)` | C++ calls a JS method                                                                       |
| `QCefView::executeJavascript(qint64 frameId, QString code, QString url)`                      | Run arbitrary JS                                                                            |
| `QCefView::executeJavascriptWithResult(…)`                                                    | JS call with awaitable string result                                                        |
| `QCefView::broadcastEvent(QCefEvent& evt)`                                                    | Emit a named event to all frames in this browser                                            |
| `QCefView::cefQueryRequest(int browserId, qint64 frameId, QCefQuery& query)`                  | JS calls `cefQuery({request, onSuccess, onFailure})`; host receives a QCefQuery object      |
| `QCefView::responseQCefQuery(QCefQuery&)`                                                     | C++ replies to the query (calls back the JS onSuccess/onFailure)                            |
| `QCefView::addArchiveResource` / `addLocalFolderResource`                                     | Map a CEF URL prefix to a folder or zip — used to serve plugin assets without a HTTP server |

The CEF version embedded is **109.1.18 / Chromium 109.0.5414.120**
(per `qcefview_strings.txt:1315`). This is dated (Jan 2023), missing
recent web-platform features.

For our Qt 6 reimplementation we should **not** use QCefView. The
recommended replacement is **`QWebChannel` with `WebEngineView`** —
Qt 6.7's WebEngine ships with a recent Chromium (~118) and the
JS-to-C++ binding is bidirectional via QWebChannel without needing
the cef_query polyfill. This is already established in our
v1.0 main-window architecture (`CLAUDE.md` Qt 6/QML gotchas section
mentions `Qt6::WebChannelQuick`).

The Property Inspector + plugin index.html bridge pattern translates
cleanly:

| QCefView call                       | Qt 6 / QWebChannel equivalent                                           |
| ----------------------------------- | ----------------------------------------------------------------------- |
| `cefQuery({request, onSuccess, …})` | `channel.objects.bridge.invoke(json).then(…)`                           |
| `invokeMethod(id, frame, m, args)`  | `bridge.signal_<m>(...)` emitted from C++; bound in JS as a `connect()` |
| `broadcastEvent(QCefEvent)`         | `bridge.signal_<name>(payload)` to multiple connected pages             |

## 9. Code corrections required

| File / target                                                             | Change                                                                                                                                                                                                               | Breaking? | Tests needed                                                   |
| ------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | -------------------------------------------------------------- |
| `src/host/plugin-host/` (does not exist yet)                              | New module. Implements `QWebSocketServer` on **`QHostAddress::LocalHost`** (NOT `Any`); random free port via `QTcpServer::listen(LocalHost, 0)` then `serverPort()`. Authentication challenge/salt as in §4.5.       | additive  | `PluginHostTest::startsLocalOnly_notBoundToLan`                |
| `src/host/plugin-host/src/sd_plugin_protocol.cpp`                         | Implement the 13 standard Elgato events + 26 AJAZZ-specific actions tabulated in §4.3/§4.4. JSON envelope per §4.2.                                                                                                  | additive  | `SDPluginProtocolTest::roundTrip_setImage_decodesAndReencodes` |
| `src/host/plugin-host/src/plugin_manifest.cpp`                            | Parse the schema in §2 including the AJAZZ extensions (`IsK1Pro`, `RunAsAdministrator`, `FSize`/`FFamily`, `Nodejs.Version`, `PUUID`). Reject `OS` mismatch.                                                         | additive  | `PluginManifestTest::rejects_macOnly_onLinux`                  |
| `src/host/plugin-host/src/node_runner.cpp`                                | Spawn user-side Node.js (we do NOT bundle node20 — instead detect a system node ≥ 20 and reject otherwise). CLI: `<node> <codePath> -port <p> -pluginUUID <u> -registerEvent registerPlugin -info <info>`.           | additive  | `NodeRunnerTest::buildsCorrectArgv`                            |
| `src/host/plugin-host/src/cef_replacement.cpp`                            | Replace QCefView with QWebEngineView + QWebChannel. Map the `cefQuery` JS function to a thin polyfill that delegates to the QWebChannel bridge.                                                                      | additive  | `CefPolyfillTest::cefQueryShim_callsBridge`                    |
| `src/host/plugin-host/src/auth.cpp`                                       | Implement salt/challenge per §4.5. SHA-256 of `password + salt`; reject after 5 failed attempts.                                                                                                                     | additive  | `PluginAuthTest::rejectsAfter5BadAttempts`                     |
| `src/host/plugin-host/CMakeLists.txt`                                     | Link `Qt6::WebSockets`, `Qt6::WebEngineQuick`, `Qt6::WebChannelQuick`, `nlohmann_json` (PRIVATE only — COD-031 boundary).                                                                                            | additive  | n/a                                                            |
| `resources/sdpi.css` (already present in some places) — verify we ship it | The plugin Property Inspector HTML uses `static/css/sdpi.css` (Elgato standard). Our plugin host must serve this CSS file under a known URL prefix so that PI pages render correctly without bundling it themselves. | additive  | `PluginAssetTest::sdpi_css_servedFromBuiltinUrl`               |
| `docs/protocols/streamdeck/akp05_vendor.md` (this file's parent)          | Cross-link this doc + the corrected listener-binding posture (§5 of `akp05_init_sequence.md`).                                                                                                                       | additive  | n/a                                                            |
| Compatibility shim                                                        | Map `connectMiraBoxSDSocket(...)` → `connectElgatoStreamDeckSocket(...)` so AJAZZ-only plugins load on our host without code changes. Implement as a JS pre-injection in our CEF replacement.                        | additive  | `CompatTest::miraboxSocket_aliasedToElgato`                    |

**Test coverage estimate**: ~500 lines of test code, ~1500 lines of
production code. The plugin-host module is large and should land in
**at least 4 separate PRs** (transport / manifest / actions / auth)
to stay reviewable.
