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

### F3 — `registerPropertyInspector` mishandled · **MEDIUM** · ✅ SERVER MODEL DONE (6a32191) · ✅ PI-launch bootstrap DONE (T024)

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

**Done (PI-launch bootstrap, T024):** `PropertyInspectorController::loadInspector`
now builds the modern entry-point call `connectElgatoStreamDeckSocket(port, context,
"registerPropertyInspector", info, actionInfo)` (`buildModernPiBootstrapJs` in
`pi_bootstrap.hpp`) and `PIWebView` runs it on `LoadSucceeded`; `Application` injects
the `SdPluginServer` loopback port after the server starts (port 0 ⇒ legacy `$SD`
bridge only). A modern WS-only PI (e.g. `com.jk.weather`) now registers over the F3
server path. The mandatory **double-`propertyInspectorDidAppear`** reconciliation
landed **first** (T023): a `PiAppearGate` keyed on the instance context makes both the
PI-04 `inspectorOpened` path and the F3 `propertyInspectorRegistered` path emit exactly
one appear/disappear per open (unit-locked, `[pi-appear]`). The builder is unit-tested
(`[t024]`); live end-to-end WS registration is harness-gated (needs a PI-bearing key
selected — see DEFERRED).

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
| B5 | `plugin_manager.cpp` dispatch | ✅ **DONE (T027):** `PluginManager::dispatch` now builds a well-formed `{event, action, …payload}` envelope (event from `payload["event"]`, actionId in `action`) instead of mislabeling the actionId as the event name. Latent (no production `.sdPlugin` caller — real events flow via `PluginDeviceBridge`); regression-tested `[b5]`. |
| B6 | `plugin_manager.cpp` HTML path | ✅ **DONE (T028):** `m_htmlPages` is keyed by plugin UUID and torn down on disable + shutdown (was a flat vector that only grew). `htmlPageCountForTesting` seam; regression-tested `[b6]` (full page destruction live-gated on WebEngine). |
| B7 | `sd_plugin_server.cpp` passHello | ✅ **DONE (T029):** `passHello.deviceInfo` is populated from the device geometry via an injected resolver (`PluginDeviceBridge::deviceInfoFor`, the same shape `deviceDidConnect` sends); empty `{}` fallback without a resolver. Regression-tested `[b7]`. |
| B9 | `plugin_manager.cpp:1006` | ~~`monitorsApplication()` implemented but no caller sends `applicationDidLaunch/Terminate`~~ **RETIRED (false as of 2026-06-18):** `applicationDidLaunch/Terminate` IS dispatched via `app_event_dispatch.cpp` (focus-approximated, not OS process lifecycle). The remaining nuance — focus-approximation vs true process monitoring — is tracked as a known semantic gap, not a missing caller. |

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
5. ✅ Secondary: **B6** (T028, HTML page lifetime), **B5** (T027, dispatch contract),
   **B7** (T029, hello deviceInfo) all DONE; **B9** retired (false). B4 stays
   hardware-gated.
6. ✅ **F3 PI-launch bootstrap** (T024) + its **didAppear dedup** prerequisite (T023).
7. ✅ **setTriggerDescription** (T025/D3) routed + ownership-gated
   `triggerDescriptionChanged` signal; **showAlert/showOk** visual surface confirmed
   already implemented (T026, stale note).

Each lands atomic + ctest-green + live debug-channel verified where the harness allows.

## Deferred / hardware-gated (explicit — NOT silently dropped, T035)

These are tracked, not forgotten. None block the achievable parity work; all are
gated on hardware, an OS, or UI/plumbing out of this milestone's scope.

- **`userDesiredState`** (multi-action multi-state, US4/T031): the `instanceChildrenToChain`
  adapter and `core::Action` carry no per-step desired state, so a Multi Action driving
  a multi-state child cannot emit `userDesiredState` on `keyDown` (`isInMultiAction:true`).
  Needs a `core::Action` state field + executor plumbing + multi-action-step config UI.
- **Retail-AKP05E encoder/touch wire values** (B4 / T013 provisionals): encoder
  ticks/polarity, `touchTap.tapPos.y` (hardcoded 0), DRA/ENC zone geometry — PROVISIONAL,
  verified only on the `0x3004` demo unit which emits no input. All routing is verified
  headless via synthetic `input.*`; the raw values need a retail AKP05E / Mirabox N4.
- **Live modern-PI WS registration** (T024/T030): the bootstrap is unit-tested and the
  wiring is in place, but driving a real WS-only PI to register needs a PI-bearing key
  to be SELECTED so the Inspector calls `loadInspector` — the synthetic click-to-select
  harness path does not propagate the binding (`inspector.binding` stays null), a
  pre-existing Wayland synthetic-input limitation. Needs a manual/hardware session.
- **Windows VendorDll Wine launch** (WINPLG-03): native vendor-DLL plugins on Linux/Wine.
- **Optional per-device `SupportedDevices` SKU enforcement** (research D6): a plugin could
  declare which SKUs it supports; not enforced today (all devices accepted).

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
- 2026-06-18 (Spec Kit US2/US4/Polish): **B5** (T027), **B6** (T028), **B7** (T029) all
  fixed with regression tests; **propertyInspectorDidAppear dedup** (T023, `PiAppearGate`)
  + the **modern-PI WS-launch bootstrap** (T024) landed; **setTriggerDescription** routed
  (T025); **T026** showAlert/showOk confirmed already done. US4 toggle-state persistence
  test added (T033); `userDesiredState` documented DEFERRED (T031). ctest 843 green; the
  System-Monitor `setTitle "CPU 15%"` → key repaint re-verified live (screenshot read).
  Deferred items catalogued above.
  Remaining: F3 modern-PI WS-launch bootstrap (own session; dedup didAppear first).
