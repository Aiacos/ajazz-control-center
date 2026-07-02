# Contract — Dial + touch-strip UI (Elgato Stream Deck + parity)

**Scope**: how a plugin-bound dial renders, is configured, and routes input on a Stream Deck +
class device (AKP05E/N4: 4 dials + a 4-segment touch strip). Impl surfaces:
`src/app/qml/components/{DeviceCanvas,EncoderDial,TouchStripLane}.qml`, `Inspector.qml`,
`src/core/include/ajazz/core/profile.hpp` (`EncoderBinding`),
`src/app/src/{profile_controller,plugin_device_bridge,encoder_layout_renderer}.*`.

## The control model — dial owns its segment

- A device with dials has, top-to-bottom: the **key grid**, the **touch strip** (one **segment
  per dial**, aligned above it), and the **dials**.
- **Segment N belongs to dial N.** Binding an `Encoder`-capable plugin action to dial N:
  - renders the action's **feedback layout** in **segment N** (live), and
  - routes **rotate / press / touch-tap-on-segment-N** to dial N's action.
- There is **no independent touch-zone binding** on dial devices (the former `"TouchZone"`
  drop target + `commitTouchZoneBinding` are retired here; `Profile::touchZones` is read-compat
  only — see data-model.md §1).

## Render

- Segment N renders via `renderEncoderLayout(layoutId, feedback, target)` using the dial
  action's layout (`$X1`/`$A0`/`$A1`/`$B1`/`$B2`/`$C1`) and the merged `setFeedback` bag
  (`title`,`value`,`icon`,`indicator`,`icon2`,`indicator2`). The **on-screen** segment uses the
  same render as the device LCD zone (live-preview parity, FR-018). No feedback layout ⇒ default
  icon+title (`$X1`-ish) fallback.

## Input routing (reuse feature-001)

| Interaction      | Event to the dial action                              | Notes                                                     |
| ---------------- | ----------------------------------------------------- | --------------------------------------------------------- |
| Rotate           | `dialRotate` (signed `ticks`, `pressed`)              | raw ticks/polarity hardware-gated (provisional)           |
| Press / release  | `dialDown` / `dialUp`                                 | `onRelease` landed in feature 001                         |
| Tap on segment N | `touchTap` (`hold`, `tapPos`)                         | `tapPos.y` hardware-gated; routes to dial N (one control) |
| Trigger hints    | `setTriggerDescription` → `triggerDescriptionChanged` | routed in feature 001 (T025)                              |

## Bind / select / configure

- **Bind**: drag an `Encoder`-capable action from the action list onto dial N (drop target
  highlighted; an action not supporting `Encoder` is rejected with a clear affordance).
- **Select**: selecting dial N highlights it and loads its Property Inspector via the existing
  encoder context `device#<pageId>#Encoder#0#N` (default page id `root` → `device#root#Encoder#0#N`;
  the `<pageId>` slot is `ctx.pageId` per `ContextRegistry::deriveContextId`) (`Inspector.qml`,
  `PropertyInspectorController.loadInspector`).
- **Configure**: the dial's PI + the dial controls (title, icon, feedback layout) are shown in an
  Elgato-faithful layout, consistent with the key path.

## Validation rules

- Binding/clearing a dial binds/clears its segment (one control).
- A `Keypad`-only action MUST be rejected on a dial; an `Encoder`-only action MUST be rejected on
  a key (clear "not supported here" affordance, target unchanged).
- Key-only devices: no dial/touch-strip UI (absent, not broken).

## Debug-channel verification

- `device.setActiveDevice akp05e` populates the canvas; profile-inject an encoder binding +
  `profile.load` to render a segment; `input.encoder` / `input.encoderPress` / `input.touch`
  exercise routing; `plugin.simulateAction` + `plugin.protocolLog` + `screenshot` confirm the
  segment render + routing. The dial PI-open-via-selection walk is harness-gated (feature-001
  T030) → manual session. Every dial/segment control is `objectName`-addressable.
