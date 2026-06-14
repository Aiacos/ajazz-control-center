# Plugin engine — gap analysis & breakage inventory

> **Status: living analysis (2026-06).** Produced by a full reconnaissance of the
> `.sdPlugin` plugin subsystem against the canonical
> [`elgato_plugin_protocol.md`](../protocols/streamdeck/elgato_plugin_protocol.md),
> the open-source Elgato SDK, and the OpenDeck reference. This is the bug/breakage
> register and the foundation-first remediation order. Update as items land.

## TL;DR

The transport (WebSocket server), lifecycle, crash policy, trust gate, and the core
`willAppear`/`keyDown`/`settings` path are **solid and well-tested** (816 ctest). The
four historically-noted gaps (willAppear `action` field, owner-prefix match, unsigned
consent persistence, kill-mid-handshake crash) are **all already closed**. The GSD
phasing (v1.3 17→18→19→22→25, v2.0 30→35) is correctly foundation-first.

What actually blocks correct execution of a *real* Elgato/OpenDeck `.sdPlugin` is a
small set of **foundational** gaps, in priority order below.

## Foundational gaps (priority order)

### F1 — `-info` registration payload is incomplete · **HIGH** · ✅ DONE (522dddf)

`PluginManager::buildInfoJson()` (`src/app/src/plugin_manager.cpp:206`) emits only
`{application:{version,platform}, devicePixelRatio:1, devices:[]}`. The real Elgato
`-info` (canonical doc §2.3) carries `application.{font,language,platform,platformVersion,version}`,
a **per-plugin** `plugin.{uuid,version}` block, `colors.{…}`, and a **populated**
`devices[]` with real `{id,name,size:{columns,rows},type}`. A version- or
device-gated real plugin reads these at startup and **refuses to run** on the minimal
payload. The empty `devices:[]` is the most damaging omission.
**Fix:** build the full payload; source `devices[]` geometry from the
`DeviceDescriptor` (see F2); thread the per-plugin manifest into `buildInfoJson`.

### F2 — `keyCols` hardcoded to 5 · **HIGH** · ✅ DONE (095b199)

`kDefaultKeyCols = 5` is a literal at `plugin_device_bridge.cpp:650`, `:1026`, and
the geometry feeding `populateContextsForActivePage`, `renderToggleState`, and
`deviceDidConnect` (`:1609`) is hardcoded AKP05E 5×2+4. Any non-5-column SKU
(AKP153 = 3 cols, AKP03, AKP815 5×3 portrait) routes events and renders to the
**wrong key**. The code comments concede "Phase 23 sources from registry". The
geometry already exists on `core::DeviceDescriptor` (`gridColumns`, `keyRows`,
`keyCount`, `encoderCount`, `hasTouchStrip`) — the bridge just doesn't read it.
**Fix:** source columns/rows from the active device's `DeviceDescriptor`; this same
source feeds F1's `devices[]` and `deviceDidConnect`. One coherent change.

### F3 — `registerPropertyInspector` mishandled · **MEDIUM** · ✅ SERVER MODEL DONE (6a32191) · PI-launch bootstrap OPEN

`sd_plugin_server.cpp` *used to* treat `registerPropertyInspector` **identically to
`registerPlugin`**: rekey the socket, send `passHello`, emit `pluginRegistered`.
A real Elgato PI opens a **separate** WS with the bound action-instance `context` as
its uuid; the duplicate-UUID impersonation guard then closed it as an impostor, and
the false `pluginRegistered(<context>)` wired the device backends to a PI's instance
context as if it were a plugin (a security footgun).

**Done (server model, `6a32191`):** the server now models a PI as a distinct
connection (`isPropertyInspector` + resolved `ownerPluginUuid` via an injected
`setContextOwnerResolver`, backed by the bridge's `ContextRegistry`). It sends no
`passHello`, never emits `pluginRegistered`, is excluded from `connectedPluginCount`,
and emits the new `propertyInspectorRegistered`/`propertyInspectorDisconnected`.
`sendToPlugin` from a PI is forwarded to the owner plugin socket; a plugin's
`sendToPropertyInspector` is forwarded to a stock PI's WS socket **iff the sender owns
the context** (mirrors the bridge's cross-plugin denial), then falls through to
`actionReceived` so the bundled QWebChannel `$SD` flow is preserved byte-for-byte.
Two synthetic-WS e2e tests cover modelling + bidirectional relay + cross-plugin denial.

**Remaining sub-task (own session):** the PI-launch bootstrap. Our PI controller hosts
PI HTML in WebEngine and bridges it via cefQuery → `$SD` (legacy/vendor PIs work). A
*modern* stock Elgato PI uses `connectElgatoStreamDeckSocket(port, context,
"registerPropertyInspector", info, actionInfo)` + a real `new WebSocket`; nobody calls
that entry point on the PI side yet (the HTML-*plugin* path already does — see
`plugin_manager.cpp:606`). Adding a guarded DocumentReady bootstrap that calls it would
make modern PIs register over the now-correct F3 server path — BUT it must first
reconcile a **double-`propertyInspectorDidAppear`**: the existing PI-04 path
(`inspectorOpened` → didAppear, `application.cpp:942`) already fires for every
`loadInspector` regardless of transport, so a WS-registering PI would also trigger the
F3 `propertyInspectorRegistered` → didAppear. Deduplicate before enabling the bootstrap.

### F4 — routed-but-unhandled vendor actions are silent no-ops · **MEDIUM** · ✅ DONE (5ad7578)

20+ events in `kRoutedActions` emit `actionReceived` with no consumer (`sendToDevice`,
`getScreenshot`, `clearIcon`, `lockScreen`, `getUserInfo`, `startAudioCapture`, …).
`setBackground` was routed but `onAction` only checked `setBG` — a dead branch.

**Done (`5ad7578`):** `setBackground` is now the `setBG` alias (added to
`isVisualAction`, dispatched to `onSetBG`); `clearIcon` resets the bound key to a blank
surface and drops its cached title; the genuinely-unsupported set is recognised by
`isUnsupportedVendorAction()` in the host `actionReceived` consumer and logged with an
explicit WARN instead of being a silent drop. `sendToDevice` raw-HID forwarding stays
unimplemented (RE hard rule) — logged, never executed. An e2e test covers the
`setBackground` alias + `clearIcon` paints.

## Secondary bugs (real, lower leverage)

| ID | Site | Failure mode |
|---|---|---|
| B4 | `plugin_device_bridge.cpp:1153` | `touchTap` zone math provisional; `tapPos.y` always 0 (hardware-gated) |
| B5 | `plugin_manager.cpp:1047` | `IPluginHost2::dispatch` forwards `actionId` as the event name — contract mislabeled; live path bypasses it |
| B6 | `plugin_manager.cpp` HTML path | `m_htmlPages` (`QWebEnginePage`) never torn down on disable/uninstall — pages leak for app lifetime |
| B7 | `sd_plugin_server.cpp:324` | `passHello.deviceInfo` is an empty `{}` placeholder; plugins reading it at hello time get nothing (real geometry only via `deviceDidConnect`) |
| B9 | `plugin_manager.cpp:1006` | `monitorsApplication()` implemented but no caller sends `applicationDidLaunch/Terminate` — `ApplicationsToMonitor` parsing is inert |

## Faithfulness divergences (intentional, document don't "fix")

- **`passHello` auth handshake** (`sd_plugin_server.cpp:304`) is an AJAZZ/Mirabox
  vendor extension, not Elgato. It does **not** block standards plugins (default has
  no password ⇒ `authenticated = m_password.isEmpty()` is true immediately,
  `:310`). OpenDeck omits it entirely. Keep it optional and off by default; never
  let it gate a real Elgato plugin.
- **Manifest schema over-strictness** — `docs/schemas/plugin_manifest.schema.json`
  uses `additionalProperties:false`, `Controllers ∈ [Keypad,Encoder]`,
  `SDKVersion ∈ [2,3]`. Real vendor manifests use `Knob`/`SecondaryScreen`,
  extra top-level keys, and `SDKVersion:1`. The *runtime* parser
  (`plugin_manifest.cpp`) is correctly permissive; the *schema doc* is not. Relax
  the schema so it validates real-world manifests (or scope it to first-party
  bundled plugins only).

## Are the docs/planning the bottleneck?

**Partially yes.** The protocol RE (`akp_plugin_sdk.md`) and the event-parity map
(`plugin-event-parity.md`) are accurate and sufficient. But the four
architecture/guide docs described the wrong system (Python host / `.acplugin` SDK) —
now corrected with scope banners pointing here and to the canonical protocol doc.
Three protocol details were genuinely under-documented and are now fixed by
[`elgato_plugin_protocol.md`](../protocols/streamdeck/elgato_plugin_protocol.md):
the full `-info` payload (§2.3), the Property Inspector dual-WebSocket model (§5),
and the device-driver split (§7). The planning itself is sound; the residual risk is
that protocol correctness rested on synthetic loopback tests + a demo unit that emits
zero input, never a real third-party plugin on real hardware (Phase 25 VERIFY-06,
still hardware-gated).

## Remediation order

1. ✅ **F2** — device-geometry resolver feeding coordinate math, `deviceDidConnect`
   size/type, killing the hardcoded `keyCols=5` (commit `095b199`).
2. ✅ **F1** — complete `-info` RegistrationInfo: application/colors/plugin blocks +
   provider-backed `devices[]` (commit `522dddf`).
3. ✅ **F3 server model** (`6a32191`) — `registerPropertyInspector` second-connection
   model: distinct PI connection keyed by instance `context`, owner resolved via
   `setContextOwnerResolver`, `sendToPlugin`/`sendToPropertyInspector` routed between
   the PI socket and the owning plugin socket with cross-plugin denial. QWebChannel
   `$SD` flow preserved. **Open sub-task:** the modern-PI WS-launch bootstrap (see F3
   above) — needs the double-`didAppear` dedup first.
4. ✅ **F4** (`5ad7578`) — `setBackground` alias, `clearIcon`, explicit WARN for
   genuinely-unsupported routed actions. `sendToDevice` raw-HID stays blocked.
5. Secondary: B6 (HTML page lifetime), B5 (dispatch contract), B9 (app monitoring),
   B7 (hello deviceInfo). B4 stays hardware-gated.

Each lands atomic + ctest-green + live debug-channel verified (project MANDATORY).

### Session log

- 2026-06-14: canonical protocol doc + this gap analysis + stale-doc banners
  (`7df6957`); F2 (`095b199`); F1 (`522dddf`). All ctest-green (819) and
  live-verified on the real AKP05E. F3/F4 remain.
- 2026-06-14 (cont.): **F4** done (`5ad7578`) — setBackground alias, clearIcon,
  unsupported-action WARN, +e2e test. **F3 server model** done (`6a32191`) —
  PI-as-distinct-connection, owner resolver, sendToPlugin/sendToPropertyInspector
  relay with cross-plugin denial, +2 synthetic-WS e2e tests. ctest 822 green.
  Live-verified on the real AKP05E: sidecar opens the device (fw V3.AKP05E.01.007),
  `device.renderTest` paints; binding `com.ajazz.sysmon.cpu` to a key streams
  `setTitle "CPU n%"` → key renders. UI confirmed already Stream-Deck-shaped
  (actions right, inspector bottom, canvas centre, selection outline, brightness).
  Remaining: F3 modern-PI WS-launch bootstrap (own session; dedup didAppear first).
