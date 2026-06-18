# Quickstart — Validating the Plugin UI + Install feature

**Feature**: Elgato-parity plugin UI on keys + dials, one-click in-app install
**Principle**: `ctest` green is necessary but NOT sufficient — every behavioral change is
verified live through the debug-control channel (constitution Principle V).

## Prerequisites

- Build: `cmake --preset linux-release && cmake --build --preset linux-release`
- Test gate: `ctest --preset linux-release` (must be green)
- Launch headless-driven: `AJAZZ_DEBUG_CONTROL=1 nohup build/linux-release/src/app/ajazz-control-center >/tmp/acc.log 2>&1 &`
  then `scripts/ajazz-debug ping`. Kill ALL instances + `rm` the stale socket before relaunch
  (single-instance is keyed on the app-ID).
- A Stream Deck + class device (AKP05E/N4) connected, OR drive synthetically (no dial hardware
  needed for routing — synthetic `input.*` covers it; raw encoder/touch *values* are HW-gated).

## Scenario 1 — One-click in-app install, no browser (US1)

```sh
scripts/ajazz-debug qml.invoke --params '{"objectName":"pluginStoreDrawer","method":"open"}'
scripts/ajazz-debug screenshot > /tmp/store.png   # READ: rows show Install / Installed / disabled "Not installable in-app" — NO "Open page"
scripts/ajazz-debug plugin.installFromCatalog --params '{"id":"<installable-uuid>"}'
scripts/ajazz-debug plugin.list                  # the plugin's actions are now present
```

**Expected**: an installable row installs in-app (progress → Installed) with **no browser
window and no side drawer required**; a non-installable row's button is **disabled with a
"not installable in-app" reason** (never "Open"). Assert zero browser launches on the install
path.

## Scenario 2 — Plugin on a key renders + configures like Elgato (US2)

```sh
scripts/ajazz-debug device.setActiveDevice --params '{"codename":"akp05e"}'
# bind a plugin action to a key (profile-inject if drag-to-bind is not drivable headlessly),
# then drive a press and read the live render:
scripts/ajazz-debug input.key --params '{"index":8}'
scripts/ajazz-debug plugin.protocolLog --params '{"tail":20}'   # setTitle/setImage to the key
scripts/ajazz-debug screenshot > /tmp/key.png   # READ: key shows the action image+title, live
```

**Expected**: the key shows the action's image + title and updates live (the System-Monitor
"CPU n%" stream is the reference); selecting it shows the Elgato-shaped title/image + Property
Inspector layout. (PI-open-via-selection is harness-gated — see Notes.)

## Scenario 3 — Plugin on a dial renders feedback + routes input (US3)

```sh
scripts/ajazz-debug device.setActiveDevice --params '{"codename":"akp05e"}'
# profile-inject an Encoder binding (a dial-capable plugin action) on encoder 0, then:
scripts/ajazz-debug input.encoder --params '{"index":0,"delta":1}'      # dialRotate
scripts/ajazz-debug input.encoderPress --params '{"index":0}'           # dialDown/Up
scripts/ajazz-debug input.touch --params '{"zone":1,"x":64}'            # touchTap on segment 0
scripts/ajazz-debug plugin.protocolLog --params '{"tail":20}'
scripts/ajazz-debug screenshot > /tmp/dial.png   # READ: touch-strip segment 0 renders the feedback layout
```

**Expected**: the touch-strip segment **above dial 0** renders the plugin's feedback layout
(icon/title/value/indicator); rotate, press, and segment-tap all reach the dial's action; there
is **no independent touch-zone** drop target on the dial device (one control = dial + segment).

## Scenario 4 — Drop-type gating + consistent layout (US3/US4)

```sh
# Attempt to drop a Keypad-only action on a dial (and an Encoder-only action on a key):
scripts/ajazz-debug screenshot > /tmp/reject.png   # READ: rejected drop shows the no-go affordance, target unchanged
```

**Expected**: an action not supporting the target control type is rejected with a clear
affordance; selected control highlighted; valid drop targets highlighted during a drag; the
on-screen preview mirrors the device.

## Definition of done (per change in this plan)

1. `ctest --preset linux-release` green; clean on the 3 platform compilers.
1. The matching scenario above passes via `scripts/ajazz-debug` with the screenshot read.
1. New interactive controls (dial drop target, store row states) are `objectName`-addressable.
1. Affected docs/schema updated in the same change (`docs/protocols/streamdeck/**` for the
   touch-zone→dial model; the profile schema `touchZones` deprecation note).

## Notes

- The **modern-PI-open-via-selection** walk is harness-gated (feature-001 T030: synthetic
  click-to-select does not propagate the binding to the Inspector → `loadInspector` does not
  fire). Verify the dial/key PI settings round-trip via `plugin.simulateAction` /
  `plugin.simulatePiSettings`, and flag the live WS-PI registration for a manual session.
- Old profiles carrying independent `touchZones` MUST still load (read-compat); confirm with a
  profile-inject of a legacy `touchZones` entry + `profile.load` (no crash, no loss).
