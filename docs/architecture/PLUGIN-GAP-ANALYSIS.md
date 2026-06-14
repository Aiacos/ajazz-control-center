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

### F3 — `registerPropertyInspector` mishandled · **MEDIUM** · OPEN

`sd_plugin_server.cpp:246` treats `registerPropertyInspector` **identically to
`registerPlugin`**: rekeys the socket, sends `passHello`, emits `pluginRegistered`.
A real Elgato PI opens a **separate** WS with the bound action-instance `context` as
its uuid; our duplicate-UUID impersonation guard then closes it as an impostor. The
bundled PIs avoid this via a QWebChannel `$SD` bridge, but a stock `.sdPlugin` PI
using the WS handshake (canonical doc §5) cannot connect.
**Fix:** model the PI as a distinct connection keyed by instance `context`; route
`sendToPlugin`/`sendToPropertyInspector` between the PI socket and the owning plugin.

### F4 — routed-but-unhandled vendor actions are silent no-ops · **MEDIUM** · OPEN

20+ events in `kRoutedActions` (`sd_plugin_server.cpp:432`) emit `actionReceived` with
no consumer (`sendToDevice`, `getScreenshot`, `clearIcon`, `lockScreen`,
`getUserInfo`, `startAudioCapture`, …). `setBackground` is routed (`:433`) but
`onAction` only checks `setBG` — a dead branch. Plugins calling these get no response
and no error.
**Fix:** implement the safe subset (`setBackground` alias, `clearIcon`), and for the
genuinely-unsupported ones return an explicit `logMessage`/error rather than a silent
drop. `sendToDevice` raw-HID forwarding stays blocked (RE hard rule).

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
3. **F3** — real `registerPropertyInspector` second-connection model. NEXT.
   Security-sensitive: `SdPluginServer` currently models every connection as a
   plugin keyed by uuid (`sd_plugin_server.cpp:240-303`); a real PI must be a
   distinct connection keyed by its action-instance `context`, with
   `sendToPlugin`/`sendToPropertyInspector` routed between the PI socket and the
   owning plugin socket. Touches the impersonation/auth guards — needs its own
   focused pass with the QWebChannel `$SD` PI flow kept working. Deserves a
   dedicated session, not a tail-end change.
4. **F4** — vendor action handlers (`setBackground` alias, `clearIcon`) + an
   explicit `logMessage`/error for genuinely-unsupported routed actions instead of
   the current silent drop (`sd_plugin_server.cpp:432`). `sendToDevice` raw-HID
   stays blocked (RE hard rule).
5. Secondary: B6 (HTML page lifetime), B5 (dispatch contract), B9 (app monitoring),
   B7 (hello deviceInfo). B4 stays hardware-gated.

Each lands atomic + ctest-green + live debug-channel verified (project MANDATORY).

### Session log

- 2026-06-14: canonical protocol doc + this gap analysis + stale-doc banners
  (`7df6957`); F2 (`095b199`); F1 (`522dddf`). All ctest-green (819) and
  live-verified on the real AKP05E. F3/F4 remain.
