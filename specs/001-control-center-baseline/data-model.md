# Phase 1 Data Model — Stream Deck + Plugin Subsystem

**Feature**: Stream Deck + plugin Elgato/OpenDeck parity
**Date**: 2026-06-18
**Source of truth for wire keys**: the schema docs under `docs/protocols/streamdeck/**` and
`docs/schemas/plugin_manifest.schema.json` (a C++ field name never overrides a documented JSON key —
e.g. `Profile::deviceCodename` ⇄ wire `"device"`). Serialization is hand-rolled and **nlohmann-free**
in `ajazz_core` (COD-031).

This captures the entities, their fields, relationships, validation rules, and state transitions for
the subsystem. It is a design reference, not implementation code.

______________________________________________________________________

## 1. Binding model (in `ajazz_core`)

### Profile

- **Represents**: a named set of bindings for one device.
- **Fields**: `deviceCodename` (⇄ wire `"device"`), `name`, `keys[]` (Binding), `encoders[]`
  (EncoderBinding), `touchZones[]`, `mouseButtons[]`, `pages[]`, `applicationHints[]` (app-id tokens
  for per-app auto-switch).
- **Relationships**: 1 device ↔ many Profiles; 1 Profile ↔ many Bindings; `applicationHints` drive
  US3 per-app switching.
- **Validation**: indices range-checked; unknown wire keys skipped (forward-compatible);
  `deviceCodename` must resolve to a registered descriptor.

### Binding (a key slot)

- **Fields**: legacy `onPress`/`onRelease`/`onLongPress` action chains + `KeyState state` (visual);
  **additive** `std::optional<ActionInstance> instance`.
- **Rule**: when `instance` is present it is authoritative for multi/toggle behavior; the legacy
  chain remains for simple built-in actions. Reader gates on key *presence*, never a schema version.

### EncoderBinding (a dial slot)

- **Fields**: rotate / press chains + visual; **additive** `instance`. **GAP (WR-05)**: add
  `onRelease` — the sidecar emits `EncoderReleased` (AKP03 protocol v3) but the binding has no field
  to carry it, so it is dropped today.
- **Rule**: `onRelease` (once added) routes the real release event; until then encoder-release is
  silently lost.

### ActionInstance / ActionState

- **ActionInstance fields**: `id` (wire `"id"`, reader also accepts `"uuid"`), `states[]`
  (ActionState), `currentState` (clamped to `[0, states.size())`), `settings` (escaped JSON string),
  `children[]` (recursive — Multi Action chain), `delayMs` (optional, Multi Action per-child delay).
- **ActionState fields**: thin wrapper over `KeyState` (per-state image / title / settings visual).
- **State transitions** (Toggle Action): on each press `currentState = (currentState + 1) mod N`;
  the new index **persists** to profile JSON (survives restart). 2-state Keypad actions auto-cycle on
  keyUp unless `DisableAutomaticStates`.
- **Validation**: `states[]` non-empty; legacy singular `state:` folds into a `states[]` of one;
  writer always emits `states[]`; reader recursion depth-guarded (`kMaxDepth=64`, CR-01 DoS guard);
  control chars escaped `\u00XX` (WR-01).
- **Adapter**: `instanceChildrenToChain` flattens a Multi Action children tree into the
  delay-carrying `core::ActionChain` the existing `ActionEngine` walks (no new async runner).

______________________________________________________________________

## 2. Device descriptor + capability surface

### DeviceDescriptor

- **Represents**: a registered Stream Dock SKU (catalog → runtime).
- **Fields**: `codename`, `vid`/`pid`, `family` (AKP05/AKP03/AKP153), `gridColumns`×`keyRows`,
  encoder count, touch-zone count, maturity tier, image format/rotation per family.
- **Rule**: advertised plugin `devices[].size` MUST equal `gridColumns × keyRows` (never a constant);
  `type` maps AKP05E/N4→7 (SD+), AKP153/AKP03→a key-grid type. Sidecar descriptors registered
  **before** `streamdeck::registerAll()` so the sidecar wins the (vid,pid) slot; `registerAll`
  registers only AKP815.

### Device capability interfaces (implemented by `SidecarStreamDockDevice`)

- `IDevice`, `IDisplayCapable` (`setKeyImage`, `setBrightness`, **`keepAlive`** — currently no-op,
  FIX), `IEncoderCapable`, `ITouchStripDisplayCapable`.

______________________________________________________________________

## 3. Plugin runtime entities (app tier, Qt-only — COD-031)

### PluginInfo

- **Fields**: manifest UUID, name, version, code path(s), `winClass` (CodePath classification:
  Node/HTML/native/VendorDll), `monitorsApplication` + `ApplicationsToMonitor`, signing verdict,
  install source, running state, maturity/platform-status chip role.

### PluginConnection (in `SdPluginServer`)

- **Fields**: `uuid`, `socket`, `salt`, `authAttempts`, `authenticated`, `isPropertyInspector`,
  `ownerPluginUuid` (PI→plugin ownership), registration token.
- **Lifecycle / state transitions**: `connect → sentinel-UUID → registerPlugin (rekey) → collision-guard → passHello (optional) → live`. A PI connection: `connect → registerPropertyInspector(context) → owner-resolved → live`. `authenticated = password.isEmpty()`
  ⇒ true immediately with no password.
- **Validation**: bind `QHostAddress::LocalHost` only (PLGSEC-03); length-bounded payloads (V5);
  registered-only delivery (V4); cross-plugin denial on visual/settings/PI relay.

### ContextRegistry entry (in `PluginDeviceBridge`)

- **Key**: `deviceId # page # controller # row # col`.
- **Value**: owning plugin UUID + 0-based coordinates.
- **Rule**: owner match is a **dotted-component longest-prefix** (so `com.foo` does not match
  `com.foobar`) with a stored-owner fallback; ownership gates every inbound/outbound action.

______________________________________________________________________

## 4. The Elgato wire envelopes (see contracts/elgato-plugin-ws.md for full event lists)

### RegistrationInfo (`-info` payload)

- `application{font,language,platform(mac|windows|linux),platformVersion,version}`, `colors{5 hex}`,
  `devicePixelRatio`, `plugin{uuid,version}`, `devices[]{id,name,size{columns,rows},type}`.

### Action-event envelope

- `{action, context, device, event, payload}`. Payload variants:
  - **SingleActionPayload**: `coordinates{column,row}` (0-indexed), `controller` (Keypad|Encoder),
    `isInMultiAction:false`, `settings`, optional `state`.
  - **MultiActionPayload**: `controller:"Keypad"`, `isInMultiAction:true`, **no coordinates**.
- `Target` enum is **numeric**: `0` both, `1` hardware, `2` software.

### manifest.json (validated against the relaxed schema — see research C2)

- **Top-level**: `UUID`, `Name`/`Author`/`Description`, `Version` (`major.minor.patch.build`),
  `Icon`, `CodePath` (+ `CodePathMac`/`CodePathWin`/`CodePathLin`/`CodePaths`), `SDKVersion`
  (accept 1/2/3), `Software.MinimumVersion`, `OS[]`, `Actions[]`; optional `Category`,
  `PropertyInspectorPath`, `Profiles`, `ApplicationsToMonitor`, `Nodejs`.
- **Action**: `UUID` (plugin-UUID-prefixed), `Name`/`Icon`, `States[]` (1=single, 2=toggle),
  `Controllers[]` (normalize `Knob`→`Encoder`; accept `SecondaryScreen`/`Information`), `Encoder{}`
  (required if Encoder controller), flags.
- **State**: `Image` (required) + title styling fields.
- **Encoder**: `Icon`, `background`, `StackColor`, `layout` (`$X1`/`$A0`/`$A1`/`$B1`/`$B2`/`$C1` or
  path), `TriggerDescription`.

______________________________________________________________________

## 5. Sidecar protocol entities (see contracts/sidecar-stdio.md)

- **Command in**: `ping`, `set_brightness{serial,percent}`, `set_image{serial,key,touchzone,width, height,rgba_b64}`, `render_test{serial}`, **`keep_alive{serial}`** (NEW — `CRT CONNECT`).
- **Event out**: `connected{serial,vid,pid,firmware,family,name}`, `ready{device_count, output_allowed}`, `input{serial,code,state,raw}`, `pong`, `ok`, `error`, `device_error`.
- **Input mapping** (`mapSidecarInput`, PROVISIONAL): encoder twist `0xA0/0xA1`, encoder press,
  touch zones `0x40..0x43`, key indices → `core::DeviceEvent`. Wire `code=raw[9]`, `state=raw[10]`;
  ACK frames (ASCII "ACK") filtered.
