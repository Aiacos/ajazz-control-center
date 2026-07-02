# MiraBox HTML plugin contract — why "Time Calendar" (20250308000340) stays silent

Date: 2026-07-02. Branch under study: `feat/opendeck-ui-submodule` (read via `git show`; worktree is on `feat/native-hardware-monitor` and was not switched or modified).

## Problem

The MiraBox "Time Calendar" HTML plugin (installed as `20250308000340.sdPlugin`, action
`com.mirabox.streamdock.timeClock.action1`) loads in our QtWebEngine host, connects to our
Elgato-compatible WS server on 57116, registers with `registerPlugin`, and its page *receives*
`willAppear` / `titleParametersDidChange` / `keyDown` / `keyUp` (verified via a `JSON.parse` hook).
Yet its Vue app never draws (`setImage` never sent), never emits the `setSettings` its own
`willAppear` handler contains, and logs no console error. A non-MiraBox HTML plugin
(`com.jk.weather`, installed as `178366994579084.sdPlugin`) works fully in the same host.

## Bundle analysis (CONFIRMED — read from the installed bundle)

Bundle: `…/scratchpad/live-verify/data/Aiacos/AJAZZ Control Center/plugins/20250308000340.sdPlugin/`
(`index.html` ≈342 KB inlined minified Vue app; `manifest.json`; `interval.js` Worker; `utils/`).

### Entry point — one function, four aliases; argument-count selects plugin vs PI

From `index.html` (minified; deminified names in brackets):

```js
window.connectSDSocket = function () {
  window.argv = [arguments[0], arguments[1], arguments[2],
                 JSON.parse(arguments[3]),
                 arguments[4] && JSON.parse(arguments[4])];
  const e = arguments[4] ? sc(zS) : sc(Um);   // 5th arg present -> Property-Inspector app, else plugin app
  e.use(j_); e.use(Lm()).mount("#app"); ...
};
window.connectSocket = window.connectSDSocket;
window.connectElgatoStreamDeckSocket = window.connectSDSocket;
window.connectMiraBoxSDSocket = window.connectSDSocket;
```

Arguments (all aliases identical): `(port, uuid, registerEvent, infoJsonString[, actionInfoJsonString])`.
`window.argv = [port, uuid, registerEvent, parsedInfo, parsedActionInfo?]`. Since `CodePath` and
`PropertyInspectorPath` are the SAME `index.html`, the 5-arg form is the only thing that turns the
page into a PI; a 4-arg call correctly mounts the plugin app. Our host passes 4 args — correct.

### pluginStore — WS + registration + Action registry

```js
jl = nf("pluginStore", () => {
  document.title = window.argv[3].plugin.uuid + " - 插件";       // requires info.plugin.uuid to EXIST
  ...
  i = new WebSocket("ws://127.0.0.1:" + window.argv[0]);
  i.onopen = () => i.send(JSON.stringify({event: window.argv[2], uuid: window.argv[1]}));
  i.onmessage = s => n.value = JSON.parse(s.data);              // message -> reactive ref, no direct dispatch
  ... class Action { setImage/setTitle/setSettings/setState/sendToPropertyInspector/openUrl ... }
});
```

The store only stashes each incoming frame into a Vue ref; ALL dispatch happens in watchers created
by `lf(...)` \[`useWatchEvent`\].

### The dispatcher — a HARD equality gate on `ActionID`

```js
lf = (e, t) => {                       // useWatchEvent(type, MessageEvents)
  const o = jl();
  if (e === "plugin") return je(() => o.message, () => {
    o.message && (o.message.action ||   // plugin-level route: only frames WITHOUT an action field
      (n = t[o.message.event]) == null || n.call(t, ...));
  });
  const r = { willAppear({action, context, payload}) { !o.getAction(context) && o.addAction(action, context, payload.settings); },
              willDisappear..., didReceiveSettings..., titleParametersDidChange... };
  return je(() => o.message, () => {
    if (!o.message || o.message.action !== t.ActionID) return;   // <-- THE GATE
    const n = JSON.parse(JSON.stringify(o.message));
    r[o.message.event]?.call(r, n);      // built-ins (instance registry)
    t[o.message.event]?.call(t, n);      // user handlers (draw, setSettings, ...)
  });
};
```

### Action module — `ActionID` is derived from `info.plugin.uuid`, NOT from the manifest

```js
function Km(e) {                                    // registered as Km("action1")
  const t = `${window.argv[3].plugin.uuid}.${e}`,   // ActionID = info.plugin.uuid + ".action1"
        o = jl(), r = af();                          // af() reads window.argv[3].application.language
  lf("action", {
    ActionID: t,
    willAppear({context: i}) {
      const a = o.getAction(i);
      if (!("select" in a.settings)) { a.settings.select = "analog01"; a.setSettings(a.settings); }
      "flash" in a.settings || a.setSettings(a.settings);
      n(i);                                          // <-- the draw trigger
    },
    didReceiveSettings({context: i}) { ...; n(i); },
    willDisappear({context: i}) { o.Unterval(i); o.Unterval(i+1); }
  });
  const n = i => {                                   // draw: SVG clock -> setImage, re-armed at 1 Hz
    const a = o.getAction(i), s = Nm(a.settings);
    a.setImage(`data:image/svg+xml;charset=utf8,${s.type === "clockface" ? Vm(...) : Gm(...)}`);
    o.Interval(i, 1e3, () => n(i));                  // Worker-driven 1 s repaint
  };
}
const Um = me({ __name: "index", setup() {
  Object.entries({"/src/plugin/actions/action1.ts": Km})
    .forEach(([t, o]) => o(t.replace("/src/plugin/actions/","").replace(".ts","")));  // Km("action1")
  jl(); lf("plugin", { deviceDidConnect(){}, deviceDidDisconnect(){} });
  return () => null;                                  // plugin app renders NOTHING to the DOM
}});
```

Facts this pins down:

- **Draw requires the gate to pass.** `n(i)` (SVG `setImage` + 1 Hz interval) runs only from
  `willAppear` / `didReceiveSettings` *after* `message.action === ActionID` matched. There is NO
  vendor "activation" event needed (`didReceiveGlobalSettings` / `deviceDidConnect` /
  `sendUserInfo` are handled on the action-less "plugin" route but are irrelevant to drawing;
  `paramfromPlugin` / `sendValue` / `setActive` do not appear in the bundle).
- **`ActionID` = `info.plugin.uuid + "." + <module name>`** where the module name is `"action1"` —
  the *file name* of the action module, which by MiraBox convention equals the last dot-segment of
  the manifest action UUID (`com.mirabox.streamdock.timeClock.action1`).
- **Therefore the bundle only dispatches when `info.plugin.uuid == "com.mirabox.streamdock.timeClock"`.**
- Other `info` fields used: `application.language` (locale table lookup, safely falls back to `en`),
  `plugin.uuid` (also `document.title`). PI branch additionally uses `argv[4].action`,
  `argv[4].context`, `argv[4].payload.settings`.

### manifest.json

`CodePath` = `PropertyInspectorPath` = `index.html`; single action
`com.mirabox.streamdock.timeClock.action1` with `Controllers: ["Keypad", "Information"]`
(`Information` is the MiraBox extension for the info-screen zone — see
`docs/protocols/streamdeck/akp_plugin_sdk.md` §2, accepted alongside `SecondaryScreen`; it does not
gate registration or dispatch in the bundle). **No top-level `UUID` and no `PUUID` field** — this is
what starves our uuid derivation (below). `OS` lists only `mac`/`windows` (we already ignore that
for HTML plugins on Linux, and events do arrive, so not the blocker).

## Our host behaviour (branch `feat/opendeck-ui-submodule`)

`src/app/src/plugin_manager.cpp` HTML path (`spawn()`, `ext == "html"` block, ~line 564):

- Loads `index.html` via `file://` into a shared `QWebEngineProfile("ajazz-html-plugins")` carrying
  the shim from `src/app/src/plugin_mirabox_shim.{hpp,cpp}` (`makeMiraboxShim()`, DocumentCreation,
  MainWorld) which pre-defines
  `window.connectMiraBoxSDSocket = function(){ return window.connectElgatoStreamDeckSocket.apply(window, arguments); }`.
  Harmless here — the bundle *itself* defines all four aliases; the shim is only a safety net and is
  immediately overwritten by the page's own definitions.

- On `loadFinished`, injects:

  ```js
  (function(){var f=window.connectElgatoStreamDeckSocket||window.connectSocket;
  if(typeof f==='function'){ f(<port>,'<regUuid>','registerPlugin',JSON.stringify(<infoJson literal>)); }})();
  ```

  4 args → plugin app mounts (correct); `regUuid` = `pluginUuid` = `manifest.puuid` if set, else
  `pluginId` = the `.sdPlugin` directory name **with suffix**, i.e. `"20250308000340.sdPlugin"`.

- `buildInfoJson()` (~line 206) emits the full Elgato RegistrationInfo envelope
  (`application{font,language,platform,platformVersion,version}`, `colors`, `devicePixelRatio`,
  `devices[]`, `plugin{uuid,version}`) — with

  ```cpp
  QString pluginUuid = manifest.puuid;                 // empty: timeClock has no PUUID/UUID
  if (pluginUuid.isEmpty()) {
      pluginUuid = QFileInfo(manifest.sourceDir).fileName();   // "20250308000340.sdPlugin"
      if (pluginUuid.endsWith(".sdPlugin")) pluginUuid.chop(...); // -> "20250308000340"
  }
  ```

  So the plugin receives **`info.plugin.uuid = "20250308000340"`**.

- The install folder got its numeric name from our own installer: the StreamDock-CDN archive is
  named by the numeric store/product id, and both install paths
  (`plugin_catalog_model.cpp` `installFromFile` ~line 1325: archive basename; catalogue path per
  issue #81: manifest UUID *when present*, which it is not here) fall back to it.
  `sdplugin_extractor.cpp` then extracts into exactly that `targetSubdir`.

### Resulting mismatch (the whole story in one line)

| Value                                               | Ours                                       | What the bundle needs                      |
| --------------------------------------------------- | ------------------------------------------ | ------------------------------------------ |
| `info.plugin.uuid`                                  | `20250308000340`                           | `com.mirabox.streamdock.timeClock`         |
| Bundle's computed `ActionID`                        | `20250308000340.action1`                   | `com.mirabox.streamdock.timeClock.action1` |
| `action` field in our `willAppear`/`keyDown` frames | `com.mirabox.streamdock.timeClock.action1` | (same)                                     |

Every action-carrying frame fails `o.message.action !== t.ActionID` and is dropped **silently by
design** — no handler, no error, no draw, no `setSettings`. Exactly the observed symptoms.

### Why `com.jk.weather` works in the same host (natural control experiment)

It is *also* installed under a numeric folder (`178366994579084.sdPlugin`, action
`com.jk.weather.action`) and therefore also receives a wrong `plugin.uuid` — but its
`plugin/main.js` is a classic hand-written Elgato-style plugin that dispatches purely on the
frame's own fields:

```js
websocket.onmessage = function (evt) {
  const jsonObj = JSON.parse(evt.data);
  if (jsonObj["event"] === "keyUp") { ... }   // never reads info.plugin.uuid
};
```

Only plugins built on MiraBox's **SDVueSDK** framework hit the `ActionID` gate.

## Official contract (sources)

1. **Official SDK docs** — registration page documents
   `connectElgatoStreamDeckSocket(inPort, inPluginUUID, inRegisterEvent, inInfo)` (plugin, 4 args)
   / `(…, inActionInfo)` (PI, 5 args), and the `info` envelope with
   `"plugin": {"uuid": "com.mirabox.demo", "version": "1.8"}` — i.e. **`plugin.uuid` is the
   reverse-DNS plugin identifier**, not an install-folder or store id.
   Source: https://sdk.key123.vip/en/guide/registration.html
1. **Official SDVueSDK template** (the framework the timeClock bundle was built from — the shipped
   minified code matches it line-for-line):
   - `SDVueSDK/vue/src/main.ts`: `window.connectSDSocket = function () { window.argv = [...]; const app = arguments[4] ? createApp(Property) : createApp(Plugin); ... }` plus aliases
     `window.connectSocket` / `window.connectElgatoStreamDeckSocket`.
   - `SDVueSDK/vue/src/plugin/actions/action1.ts`:
     `` const ActionID = `${window.argv[3].plugin.uuid}.${name}`; ``
   - `SDVueSDK/vue/src/hooks/plugin.ts` `useWatchEvent`:
     `if (plugin.message.action !== MessageEvents['ActionID']) return;`
     Source: https://github.com/MiraboxSpace/StreamDock-Plugin-SDK (fetched via `gh api`, paths above).
1. **Official plain-JS SDK** (`SDJavaScriptSDK/.../plugin/utils/common.js`) is tolerant:
   `plugin[data.action?.split('.').pop()]?.[data.event]?.(data)` — dispatches on the LAST segment of
   the incoming action UUID, which is why non-Vue MiraBox plugins would survive a wrong
   `plugin.uuid`. Same repo as above.
1. **Official Windows host** (`Stream Dock AJAZZ.exe` v3.10.195, RE corpus
   `~/MEGAsync/ajazz-reverse-engineering/raw-workdir/sd-app/`): binary strings show the CEF
   `ExecuteJavaScript` templates
   `connectMiraBoxSDSocket('%1','%2','%3','%4')` with fallback
   `connectElgatoStreamDeckSocket('%1','%2','%3','%4')` for plugins, and the 5-arg
   `connectSocket('%1','%2','%3','%4','%5')` for PIs (near `PropertyInspectorWebView`); node
   plugins get `-port/-pluginUUID/-registerEvent/-info` argv (same as ours). All arguments are
   passed single-quoted (port as a string — the bundle only concatenates it, so our numeric port is
   equally fine).
1. **Folder-name convention**: the official app's `defaultData/defaultPlugins/` folders are the
   dotted namespace (`com.mirabox.streamdock.time.sdPlugin`, `…dateTime.sdPlugin`, …) and the
   MiraboxSpace/StreamDock-Plugins repo uses `com.mirabox.streamdock.<x>.sdPlugin` names, so in the
   official ecosystem "folder name minus `.sdPlugin`" and "action-UUID prefix" coincide — the
   dir-name heuristic our `buildInfoJson()` uses never breaks *there*. Our StreamDock-CDN installs
   break the coincidence by using the numeric product id as the folder name.
   (PROVISIONAL: we did not decompile how the official exe fills `plugin.uuid` for store installs
   — `SDPluginManager::getPluginUUID` exists in the PDB but was not reversed; the docs' example +
   folder convention + the SDK's client-side requirement make the intended value unambiguous anyway.)

## Root cause

**CONFIRMED (single root cause, high confidence):** our `PluginManager::buildInfoJson()` derives
`info.plugin.uuid` from the install-directory name (`"20250308000340"`), but the SDVueSDK framework
inside the bundle computes `ActionID = info.plugin.uuid + ".action1"` and silently drops every
incoming frame whose `action` (`com.mirabox.streamdock.timeClock.action1`) doesn't string-equal
that ActionID. Handlers never fire → no draw, no `setSettings`, no errors. The WS layer, the shim,
the 4-arg mount, event delivery, and payload shapes are all verified working; only the identity
value is wrong.

No secondary cause is required to explain the symptoms. (Ranked residuals, all low probability:
`application.language` outside {en,zh} merely falls back to `en`; `devices[]` content is unused by
this bundle; the `Information` controller has no client-side effect.)

## Proposed fix

Minimal, surgical, in `PluginManager::buildInfoJson()` (`src/app/src/plugin_manager.cpp` ~line 240):
when `puuid` is empty, prefer the **action-UUID namespace** over the directory name whenever the
directory-derived uuid is not a dot-prefix of the plugin's action UUIDs:

```cpp
// After the existing dir-name fallback:
// MiraBox SDVueSDK plugins compute ActionID = info.plugin.uuid + "." + <last action segment>
// and hard-drop events otherwise. When the install dir is a store id (numeric CDN name),
// derive the uuid from the actions' common reverse-DNS prefix instead.
if (!manifest.actions.empty()) {
    QString const first = manifest.actions.front().uuid;         // "com.mirabox.streamdock.timeClock.action1"
    int const cut = first.lastIndexOf(QLatin1Char('.'));
    if (cut > 0) {
        QString const ns = first.left(cut);                      // "com.mirabox.streamdock.timeClock"
        bool const dirNameIsPrefix =
            !pluginUuid.isEmpty() &&
            first.startsWith(pluginUuid + QLatin1Char('.'));     // Elgato-style dirs keep winning
        bool allShare = true;
        for (auto const& a : manifest.actions) {
            if (!a.uuid.startsWith(ns + QLatin1Char('.'))) { allShare = false; break; }
        }
        if (!dirNameIsPrefix && allShare) {
            pluginUuid = ns;
        }
    }
}
```

Expected payload after fix:
`"plugin": {"uuid": "com.mirabox.streamdock.timeClock", "version": "2.0.1"}` → bundle computes
`ActionID = "com.mirabox.streamdock.timeClock.action1"` → gate passes → `willAppear` runs →
`setSettings` + `n(context)` draw (SVG clock via `setImage`, 1 Hz repaint).

Notes / non-goals:

- **Do NOT change `regUuid`** (the 2nd connect argument / `-pluginUUID`): registration, event
  routing (`sendEvent(owner, …)`), `m_htmlPages` keying, and PI-owner resolution all key on it
  today, and the bundle only echoes it back in the register frame. Changing it is a larger,
  unrelated refactor. (Cosmetic inconsistency worth a later look: `regUuid` keeps the `.sdPlugin`
  suffix while `info.plugin.uuid` chops it.)
- Elgato-format plugins are unaffected: they have a manifest `UUID` (mapped into `puuid` per
  GAP-28C) and never reach the fallback; and for classic dir layouts the dir name IS the prefix so
  `dirNameIsPrefix` keeps today's value.
- Alternative (heavier) fix — install-time renaming of the folder to the action namespace — also
  works and matches the official ecosystem convention, but touches discovery, uninstall,
  quarantine, and existing installs; not needed to unblock rendering.
- Verification recipe: rebuild, launch with `AJAZZ_DEBUG_CONTROL=1`, bind
  `com.mirabox.streamdock.timeClock.action1` to a key, confirm via the plugin page's WS hook that
  the plugin now SENDS `setSettings`/`setImage`, and confirm the key repaints at 1 Hz (clock).
  Also regression-check `com.jk.weather` and a node plugin.

## Open questions

1. What exactly does the official Windows host put in `%4` (`info`) for store-installed plugins
   whose archive folder is numeric — does `SDPluginManager::getPluginUUID` parse the action UUIDs
   like the proposed fix, or does the official store zip actually contain a dotted-named folder?
   (Would need Frida on the vendor app or unzipping an official store download; irrelevant to the
   client-side requirement, which is unambiguous.)
1. Multi-namespace MiraBox plugins (e.g. the official `com.mirabox.streamdock.PR.sdPlugin` mixes
   `com.hotspot.streamdock.pr.*` and `com.hotspot.streamdock.hotkey.pr.*` action UUIDs): the
   longest-common-namespace derivation would bail (`allShare == false`) and keep today's dir-name
   value. If such a plugin is SDVueSDK-based it would still be broken — but it is equally unclear
   how the vendor host serves it; revisit only if one shows up.
1. `Controllers: ["Information"]` semantics on our renderer: the bundle draws via plain `setImage`
   on the bound key context, so nothing extra is needed for the clock; whether we should route
   `Information` actions to an info-screen zone on devices that have one (AKP05 family) is a
   separate feature question.
1. `interval.js` is loaded by the page as `new Worker("interval.js")` relative to the plugin dir —
   works under `file://` today; if the HTML host ever moves to an http asset server, keep the
   worker path reachable.
