# Phase 28: AKP05 Plugin Action Completeness + Drag-to-Bind on Keys & Dials - Context

**Gathered:** 2026-05-31
**Status:** Ready for planning
**Mode:** Investigation-led (3 parallel exploration agents: action-enumeration pipeline, drag-drop wiring, OSS/RE reference model) + 3 locked grey-area decisions.

<domain>
## Phase Boundary

Make installed AKP05 `.sdPlugin` plugin actions fully **visible and bindable** from the
device editor. Two user-reported defects to close (verbatim goal, 2026-05-31):

1. *"i plugin per akp05 ... si installano ma non mostrano tutti i tool che dovrebbero
   contenere"* — installed plugins don't surface ALL the tools (actions) they declare.
1. *"si dovrebbero poter trascinare i tool sui pulsanti e sui dial per utilizzarli"* —
   you should be able to drag tools onto the keys AND the encoder dials to use them.

**In scope:** action-library completeness + honest diagnostics; drag-to-bind plugin actions
onto keys / encoder dials / touch-strip zones with the plugin `actionId` persisted routably;
controller-affordance normalization (Knob/Encoder/SecondaryScreen) + strict drop gating; full
`PluginAction` model (VisibleInActionsList, Encoder block, multi-state, default Settings);
ActionContext registration so the bridge actually invokes the plugin; LIVE debug-channel
verification.

**Out of scope / unchanged:** the `plugin_device_bridge` wire mapping (already correct — keyDown/
dialRotate/dialDown/touchTap with `controller` + `coordinates`); the device opcodes/wire format;
the Python OOP host (SEC-003); built-in in-process actions (Phase 21). UI + plugin-app layer only.
</domain>

<decisions>
## Implementation Decisions

### Locked via user Q&A (2026-05-31)

| Decision                                                   | Choice                          | Implication                                                                                                                                                                                                                                             |
| ---------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Repro plugin(s)** for the "missing tools" live diagnosis | **System Monitor + Weather**    | Locate/install both, drive `installedActions()` via `scripts/ajazz-debug`, compare count to each manifest's visible-action count to pin the real data-dependent drop. These are the plugin-install-epic targets (memory `project_plugin_install_epic`). |
| **Action-data model depth**                                | **Full**                        | Parse + model `VisibleInActionsList`, the `Encoder` block (`TriggerDescription` rotate/push/touch + layout), multi-state (`States` count + state index + `DisableAutomaticStates`), and default `Settings` seeding — not just the minimal actionId fix. |
| **Affordance gating strictness**                           | **Strict reject (Elgato-like)** | A `Knob`/`Encoder`-only action can ONLY drop on a dial; `Keypad`-only ONLY on a key; `SecondaryScreen` on touch zones. Mismatched drops rejected with `drag.accepted=false` + reject visual. No silent wrong-controller binds.                          |

### Claude's discretion (resolve during planning/execution)

Wave structure, exact diagnostic-signal shape (log + surfaced count), grouping/category UI in the
library, and fixture-manifest contents — all at Claude's discretion guided by the success criteria
and codebase conventions. Per CLAUDE.md: every new interactive control gets an `objectName`;
verify every change live via the debug channel before "done".
</decisions>

\<code_context>

## Existing Code Insights (3-agent investigation, 2026-05-31 — file:line verified)

### A. Action enumeration pipeline ("not all tools show") — pipeline passes everything; the drop is data-dependent

- **Manifest parse** reads ALL actions, no limit: `src/app/src/plugin_manifest.cpp:172-176`
  (`for (actVal : actionsArray) m.actions.push_back(parseAction(...))`). Struct
  `PluginAction` at `src/app/src/plugin_manifest.hpp:38-47` (has `controllers`; **lacks**
  `VisibleInActionsList`, `Encoder` block, multi-state name/title, default Settings).
- **Enumeration** flattens ALL actions: `src/app/src/plugin_catalog_model.cpp:348-432`
  (`installedActions()` Q_INVOKABLE). Per-action skip gates: empty `UUID`/`Name` →
  silent `continue` at **`:382-384`**; whole-plugin skip on manifest-parse failure →
  silent `continue` at **`:375`**. `controllers` passed through at `:426` but **never
  consumed by QML**.
- **QML surface**: `src/app/qml/ActionLibraryPane.qml:62-81` calls `installedActions()`,
  appends every row, **no controller filter, no VisibleInActionsList filter**. Tile has no
  `visible` gate; only the icon image/glyph is conditional (`:227,:239`).
- **Test** proving multi-action enumeration works: `tests/unit/test_catalog_offline.cpp:225-310`
  (`REQUIRE(actions.size() == 2)` for a 2-action manifest).
- **Conclusion:** the missing tools are NOT a hard-coded limit. Candidates (rank by live
  diagnosis): (a) one+ actions in the real plugins have empty/missing `UUID` or `Name` →
  skipped at `:382`; (b) the whole manifest fails to parse → plugin skipped at `:375`;
  (c) icon resolution leaves rows glyph-only and the user reads them as non-interactive.
  **Must reproduce against System Monitor + Weather to pin which.**

### B. Drag-drop wiring (keys work, dials/zones broken) — CONFIRMED bug, file:line

| Target                       | DropArea | onDropped → ProfileController                                     | Passes actionId?                       | Routable? |
| ---------------------------- | -------- | ----------------------------------------------------------------- | -------------------------------------- | --------- |
| `KeyCell.qml:170-244`        | yes      | via `DeviceView.qml:316-330` → `commitKeyBinding(...,aid)` `:324` | **yes (6 args)**                       | ✅        |
| `EncoderDial.qml:134-201`    | yes      | direct `commitEncoderBinding(idx,"",label,kind,"")` **`:179`**    | **NO — 5 args, `ap.actionId` dropped** | ❌        |
| `TouchStripLane.qml:228-291` | yes      | direct `commitTouchZoneBinding(zone,"",label,kind,"")` **`:271`** | **NO — 5 args**                        | ❌        |

- The C++ commit methods already accept the optional 6th `actionId`
  (`profile_controller.hpp:207-273`; added commits `b5ca574`/`ccb734a`/`0ce2dd4`). The
  encoder/zone drop handlers were written BEFORE the 6th param landed and never updated.
- Persisted model fully supports plugin routing: `core::Profile` keys/encoders/touchZones maps
  of `Action{kind,id,settingsJson}` at `src/core/include/ajazz/core/profile.hpp:100-132`;
  `Action.id` holds the plugin action UUID. (RE note: pin `Action.id` semantics to the
  **action UUID**; retain plugin UUID; persist controller + state for clean round-trip.)
- **No affordance gating exists** anywhere — encoder-only actions can drop on keys today.

### C. Reference model (Elgato + OpenDeck + AJAZZ RE) — the interop rules

- **Controller tokens:** Elgato uses `Keypad`/`Encoder`; **AJAZZ vendor manifests use `Knob`**
  (not `Encoder`) plus `Information` and `SecondaryScreen` (= touch strip). A correct affordance
  helper MUST treat `Knob` ≡ `Encoder` (dial-capable) and `SecondaryScreen` (touch-zone-capable);
  `Keypad`/absent → key-capable. Source: `docs/protocols/streamdeck/akp_plugin_sdk.md` §2/§2.1;
  vendor manifests under `~/MEGAsync/ajazz-reverse-engineering/raw-workdir/sd-app/plugins/*/manifest.json`.
- **`VisibleInActionsList:false`** is real in the vendor corpus (e.g. `…touchbar.sdPlugin` hides
  Focus-On-Search / DND / Dictation). Currently un-parsed → such actions wrongly LEAK into the
  library. Parsing it is required for correctness (default true).
- **AKP05 = Stream Deck+ analog**: 2×5 keys + 4 pressable encoders, each encoder owns one of the
  4 touch zones. Source: `naerschhersch/opendeck-akp05`; dossier `akp-streamdeck.md` §1.3.
- **Bridge already correct**: `src/app/src/plugin_device_bridge.cpp` maps KeyPressed→keyDown
  (`controller:"Keypad"` + `{row,column}`), EncoderTurned→dialRotate (`ticks`,
  `controller:"Encoder"`), EncoderPressed/Released→dialDown/Up, TouchUp→touchTap; `ContextRegistry`
  keys contexts by `(deviceId, controller, row, column)`; `keyIndexForCoords`/`coordsForKeyIndex`
  convert 1-based device index ↔ 0-based Elgato `{row,column}`. **Likely gap:** a binding created
  by a QML drop / loaded from a profile may NOT register an `ActionContext` (only a plugin's own
  `willAppear` round-trip does) — so the bridge has nothing to invoke. Verify LIVE.

### Live-verification harness (autonomous-capable)

- `AJAZZ_DEBUG_CONTROL=1` + `scripts/ajazz-debug` drives the running app. Synthetic input:
  `input.encoder`/`input.encoderPress`/`input.key`/`input.touch` inject through the full pipeline
  with NO working input hardware (memory `project_akp05_autonomous_input_test`). `plugin.installFromFile`
  - `plugin.rediscover` RPCs exist (commit `825eccd`) for install→spawn smoke without restart.
- The 0x3004 demo unit's input streaming is unreachable from hardware, but synthetic injection
  covers the dial round-trip end-to-end. Output (image render) works.
  \</code_context>

<specifics>
## Specific Ideas (success criteria — see ROADMAP Phase 28 for the authoritative list)

1. Library shows exactly the `VisibleInActionsList != false` actions per plugin; skipped/malformed
   actions surface a countable diagnostic; proven live (installedActions count == manifest visible
   count) for System Monitor + Weather.
1. Drag-to-bind persists `actionId` for keys (already) + dials + touch zones; fix `EncoderDial.qml:179`
   - `TouchStripLane.qml:271`; `Action.id` non-empty + survives restart on all three controllers.
1. Affordance normalizer (Keypad / Knob≡Encoder / SecondaryScreen) rides the drag payload; STRICT
   reject of mismatched drops; unit-tested across token variants.
1. Full `PluginAction` model: VisibleInActionsList + Encoder block + multi-state + default Settings;
   Catch2 over a fixture manifest.
1. ActionContext registered on drop/profile-load; LIVE: bind plugin action to a dial → synthetic
   `input.encoder` → plugin receives `dialRotate`/`dialDown` with `controller:"Encoder"`.
1. `ctest --preset linux-release -E qml` ≥ 714, 0 failed; COD-031 clean; no protocol/wire changes;
   Python OOP host + built-in actions untouched.
   </specifics>

<deferred>
## Deferred Ideas

- The v1.2/v1.3 milestone-boundary formal split (audit `MILESTONE-BOUNDARY` blocker) — separate
  bookkeeping task, not this phase.
- Phases 9-12 operator Wireshark captures (out of agent scope) — unchanged.
- Phase 25 input-streaming tests on a retail AKP05E / Mirabox N4 — hardware-gated, unchanged.
- Any new device opcode / wire change — explicitly OUT (bridge is already correct).
  </deferred>
