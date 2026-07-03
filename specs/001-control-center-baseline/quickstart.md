# Quickstart — Validating the Stream Deck + Plugin Subsystem

**Feature**: Stream Deck + plugin Elgato/OpenDeck parity
**Audience**: anyone (human or agent) implementing or verifying a change in this subsystem.
**Principle**: `ctest` green is necessary but NOT sufficient — every behavioral change is verified
live through the debug-control channel (constitution Principle V). See
[contracts/debug-control-rpc.md](./contracts/debug-control-rpc.md).

## Prerequisites

- Build: `cmake --preset linux-release && cmake --build --preset linux-release`
- Test gate: `ctest --preset linux-release` (must be green; ~822 cases)
- Sidecar built: `cargo build --release` in `streamdock-host/` (or rely on the bundled binary;
  override with `$AJAZZ_STREAMDOCK_HOST`)
- For hardware-optional runs: nothing — synthetic input covers routing. For wire-value confirmation:
  a retail AKP05E / Mirabox N4 (the `0x0300:0x3004` demo unit emits zero input — do not use it for
  input verification).

## Launch (headless live-drive)

```sh
AJAZZ_DEBUG_CONTROL=1 nohup build/linux-release/src/app/ajazz-control-center >/tmp/acc.log 2>&1 &
# kill by exact PID later — never `pkill -f` a pattern that matches your own shell
scripts/ajazz-debug ping
```

## Scenario 1 — Device renders, brightness is live (US1 / FR-004, FR-006)

```sh
scripts/ajazz-debug device.list
scripts/ajazz-debug device.setActiveDevice --params '{"codename":"akp05e"}'
scripts/ajazz-debug device.renderTest --params '{"codename":"akp05e","count":10,"main":true,"encoders":true}'
scripts/ajazz-debug device.setBrightness --params '{"percent":40}'
scripts/ajazz-debug screenshot > /tmp/s1.png   # then READ the screenshot
```

**Expected**: numbered keys + the 4 strip zones render `Rot180`; brightness changes live.

## Scenario 2 — Keep-alive holds the handle (D2, the top device bug)

After implementing the sidecar `keep_alive` command + wiring `SidecarStreamDockDevice::keepAlive()`:

```sh
scripts/ajazz-debug device.setActiveDevice --params '{"codename":"akp05e"}'
# leave idle > the keep-alive interval (~1s timer), then:
scripts/ajazz-debug device.renderTest --params '{"codename":"akp05e","count":10}'
```

**Expected**: still renders after idle (no wedge). **Regression test**: a unit test asserts
`keepAlive()` emits a `keep_alive` command (today it is a silent no-op — that is the bug).

## Scenario 3 — Press → plugin action → key repaint round trip (US2 / FR-008, FR-004)

```sh
scripts/ajazz-debug plugin.installFromCatalog --params '{"id":"<system-monitor-id>"}'
scripts/ajazz-debug plugin.list
# bind the action to key 0, then drive a synthetic press through the full pipeline:
scripts/ajazz-debug profile.commitKeyBinding --params '{"device":"akp05e","keyIndex":0,"action":"<uuid>"}'
scripts/ajazz-debug input.key --params '{"index":0}'
scripts/ajazz-debug plugin.protocolLog --params '{"pluginId":"<uuid>","tail":20}'
scripts/ajazz-debug screenshot > /tmp/s3.png   # READ it: the key shows plugin output
```

**Expected**: `keyDown` reaches the plugin; the plugin's `setImage`/`setTitle` repaints the key.

## Scenario 4 — Property Inspector round trip + single didAppear (D1, top plugin gap)

```sh
scripts/ajazz-debug plugin.simulatePiSettings --params '{"pluginId":"<uuid>","actionId":"<aid>","settingsJson":"{\"foo\":1}"}'
scripts/ajazz-debug plugin.protocolLog --params '{"pluginId":"<uuid>","tail":40}'
```

**Expected**: PI `setSettings` round-trips (`didReceiveSettings` back); `propertyInspectorDidAppear`
appears **exactly once** per open (not twice — verify after the dedup of the two emit sites at
`application.cpp:736`/`:948`). After the modern-PI WS bootstrap lands, a stock WS-only PI also
registers (`registerPropertyInspector`).

## Scenario 5 — Encoder + touch routing (US1 / FR-005, hardware-optional)

```sh
scripts/ajazz-debug input.encoder --params '{"index":0,"delta":1}'
scripts/ajazz-debug input.encoderPress --params '{"index":0}'
scripts/ajazz-debug input.touch --params '{"zone":1,"x":64}'
scripts/ajazz-debug plugin.protocolLog --params '{"pluginId":"<uuid>","tail":20}'
```

**Expected**: `dialRotate`/`dialDown`/`touchTap` reach the plugin with the right `controller` +
payload shape. **Note**: payload *shape* is verified headless; raw `ticks`/polarity and `tapPos`
*values* are hardware-gated (do not hard-commit demo-unit values).

## Scenario 6 — Multi-action and toggle (US4 / FR-017)

```sh
# bind a 2-step multi-action and a 3-state toggle, then press each:
scripts/ajazz-debug input.key --params '{"index":1}'   # multi-action: both steps run once
scripts/ajazz-debug input.key --params '{"index":2}'   # toggle: state advances + key repaints
# relaunch the app and re-read the bound state to confirm persistence
```

**Expected**: multi-action runs N steps on one press; toggle advances `currentState mod N` and
persists across restart.

## Definition of done (per change in this plan)

1. `ctest --preset linux-release` green; clean on the 3 platform compilers.
1. The matching scenario above passes when driven via `scripts/ajazz-debug`, with the screenshot read.
1. New interactive controls are `objectName`-addressable.
1. Affected docs updated in the same change (`plugin-event-parity.md`, manifest schema,
   `docs/protocols/streamdeck/**` where wire facts change).
