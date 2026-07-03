# Contract — Debug-Control RPC surface (drive & verify this subsystem)

**Impl**: `src/app/src/debug_control_facade.cpp` + `debug_control_qml.cpp`; client `scripts/ajazz-debug`.
**Transport**: newline-delimited JSON-RPC over a `0600` Unix socket at
`$XDG_RUNTIME_DIR/ajazz-control-center-debug.sock`. Enable with `AJAZZ_DEBUG_CONTROL=1`.

This is the **mandatory live-verification path** (constitution Principle V). Per the constitution,
every behavioral change in this plan is "done" only after being driven and observed here.

## Methods relevant to Stream Deck + plugins

### Device

- `device.list` — registered devices + maturity tier.
- `device.setActiveDevice {codename, serial}` — select a device for the session.
- `device.setBrightness {percent}` — 0–100 (sidecar LIG).
- `device.clearAll` — blank all keys + strip.
- `device.renderTest {codename, count, main, encoders}` — headless paint loop.

### Input (synthetic — through the FULL dispatch pipeline; no hardware needed)

- `input.key {index}` — synthetic key press.
- `input.encoder {index, delta}` — rotary tick (±1).
- `input.encoderPress {index}` — encoder button.
- `input.touch {zone, x}` — strip touch (4 zones).

> These inject via `StreamDockInputService::injectSyntheticEvent` → the same Binding→ActionEngine /
> plugin path real input takes. This is how all routing is verified while the encoder/touch **wire
> values** remain hardware-gated.

### Profile

- `profile.list` / `profile.active` / `profile.load {id}` / `profile.create {name}`.
- `profile.commitKeyBinding` / `profile.commitEncoderBinding` / `profile.swapKeyBinding`.

### Plugin

- `plugin.list` / `plugin.installedActions`.
- `plugin.sendEvent {pluginId, event, params}` — host→plugin event injection.
- `plugin.simulateAction {pluginId, actionId, settings}` — run an action with PI config.
- `plugin.simulatePiSettings {pluginId, actionId, settingsJson}` — PI round-trip.
- `plugin.protocolLog {pluginId, tail}` — plugin WS message log.
- `plugin.installFromFile {path}` / `plugin.installFromCatalog {id, version}` / `plugin.rediscover`.

### QML / introspection

- `qml.tree` / `qml.get` / `qml.set` / `qml.invoke` / `qml.click` / `qml.drag` / `screenshot`.

## Known harness gaps this plan closes (D5)

1. **KeyCell selection** — device-canvas key cells lack per-cell `objectName`, so the deferred
   human-UAT walks (Multi/Toggle author, PI render) cannot be driven headlessly. Add `objectName` per
   cell.
1. **Modal Drawer/Popup** — cannot be opened headlessly via `qml.invoke`; add a debug-addressable
   open path.
1. **`qml.click` on `Switch`** emits `clicked()` but not the user `toggled()` side effect — for any
   new setting, expose a dedicated `Q_INVOKABLE` or verify the C++ setter + read-binding separately.

## Contract acceptance

Every parity gap closed in this plan has a corresponding `quickstart.md` scenario expressed purely in
these RPCs, so the gate "can I drive this from `scripts/ajazz-debug`?" is satisfiable for each.
