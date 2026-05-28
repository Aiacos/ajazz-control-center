---
phase: 26-opendeck-shaped-device-editor
gathered: 2026-05-28
status: research-pending
source: Phase 25 partial walkthrough findings (GAP-25A + GAP-25B) + user directive 2026-05-28 13:10
---

# Phase 26: OpenDeck-shaped Device Editor — Context

> Auto-bootstrapped from Phase 25 PARTIAL handoff. Will be refined via
> `/gsd-discuss-phase 26` or `/gsd-spec-phase 26` after `26-RESEARCH.md`
> lands.

<domain>
## Phase Boundary

Phase 26 replaces the generic `KeyDesigner.qml` NxN tile grid with an
**Elgato Stream Deck / OpenDeck-pattern device-shaped editor** that
models the **physical geometry of each AJAZZ / Mirabox SKU** the project
supports. Closes the two structural gaps surfaced by the Phase 25
operator UAT walkthrough 2026-05-28:

- **GAP-25A** — `StreamDockControlService::setActiveDevice()` is declared
  in C++ but never called from any QML file. `m_activeDevice` stays
  null. `repaintPage()` early-returns. The image-upload pipeline cannot
  drive the device end-to-end even with the URL-handling fixes from
  commit `24651a3` in tree.

- **GAP-25B** — `KeyDesigner.qml` is a generic NxN tile grid. No touch-
  strip drop target, no encoder-LCD overlay surface, no drag-and-drop
  from an action library. Operator UAT 2026-05-28 reports "solo Dial" —
  only encoder dials visible in the editor.

**Scope (per user directive 2026-05-28 13:10):** the device-shaped
editor must cover **all AJAZZ / Mirabox SKUs** OpenDeck supports — not
just AKP05E. This includes (per the current `devices.yaml` catalogue +
the OpenDeck reference base):

- **Stream Dock family (AKP05 / N4):** 10 LCD keys (2×5) + 4 endless
  rotary encoders + 1 touch strip below.
- **AKP153 / N3 family:** 15 LCD keys (3×5), no encoders, no touch strip.
- **AKP03 family:** 6 LCD keys (2×3) + 3 encoders (per the AKP03 plan
  notes).
- **AKP815 / Mirabox HSV293S:** to be confirmed against OpenDeck.
- **AK980 PRO (keyboard):** different surface entirely — RGB-mode +
  per-key colour panel, no LCD-key editor.
- **AJ-series 8K mouse:** DPI / polling-rate / per-stage RGB panel.

The editor is **per-SKU geometry-driven**: the QML reads
`DeviceDescriptor` (from `src/devices/streamdeck/src/register.cpp` +
sibling registries) and renders the correct surface set. Drag-drop from
an action library on the left.

</domain>

<decisions>
## Implementation Decisions

### Research-first

This phase is **research-driven**. Before any code lands, we need an
explicit map of:

1. How OpenDeck (`nekename/OpenDeck`, Tauri/SvelteKit) lays out its
   profile editor — per-device geometry, drop-target model, action-
   library pane.
1. How `naerschhersch/opendeck-akp05` (the OpenDeck plugin) wires the
   AKP05 family — including which OpenDeck visual model maps to the
   AKP05's encoder + touch-strip surfaces.
1. How `4ndv/mirajazz` (the underlying Rust library) models the
   per-device key/encoder/strip count + the wire protocol for each.

These three projects together cover essentially every AJAZZ-compatible
device. The research deliverable (`26-RESEARCH.md`) maps each AJAZZ SKU
the project ships to a concrete device-geometry + UI-pattern
recommendation.

### Reuse-first

- Keep `KeyDesigner.qml` reachable for legacy /generic-grid devices
  (e.g., devices not in the SKU geometry map). Do NOT delete it; gate
  the new editor behind a capability check (presence of geometry data
  in DeviceDescriptor) or per-codename routing.
- Reuse the existing `ProfileController` + `Profile` schema; the
  geometry-driven editor still writes the same `Profile::keys` /
  `Profile::encoders` maps. No schema change needed unless OpenDeck's
  model demands one.
- Reuse `StreamDockControlService::assignMainImage / assignEncoderImage / assignTouchStripZone` — the wire path already exists. Phase 26
  wires the missing setActiveDevice + binds the drag-drop drop-target
  to the correct assignment method.

### Out of scope

- **Wire-protocol changes.** Phase 26 is UI / wiring only. The wire
  layer is `assignMainImage` / `assignTouchStripZone` per
  StreamDockControlService — already covered by 26 stream-dock tests.
- **Demo-unit input-streaming gap.** Tests 2-5 + 15-16 stay BLOCKED on
  `0x3004` per CLAUDE.md AKP05E glossary §7.1. Phase 26 does not
  change that.
- **Real-plugin sdPlugin samples.** Plugin discovery + lifecycle is
  Phase 18-22; Phase 26 just makes the binding editor usable.

### Hardware-gating

- The per-device-geometry map should land first as **read-only data**
  (descriptor extension + a static SKU layout table) — that part is
  HW-free and can be unit-tested with MockTransport.
- The drag-drop interactive editor needs the AKP05E physically
  connected to verify the UAT loop. Resume Phase 25 25-UAT.md tests 1,
  6, 14 once Phase 26 lands.

</decisions>

\<code_context>

## Existing Code Insights (from Phase 25 walkthrough)

- `src/app/qml/KeyDesigner.qml` (lines 40-176) — current generic NxN
  grid. Uses `ListModel bindings` + `Repeater { model: bindings; delegate: KeyCell { ... } }`. The grid layout is one Item with a
  GridLayout child; columns = `root.gridColumns`. **Does NOT** model
  touch strip or encoder dials.
- `src/app/qml/components/KeyCell.qml` — single LCD-key tile with
  `required property url iconSource`, `label`, `index`. Reusable for
  the new editor but the **surrounding layout** is what changes.
- `src/app/qml/Inspector.qml` — right-pane per-binding editor (icon
  picker + label + action kind + params). Likely reusable.
- `src/app/src/stream_dock_control_service.{cpp,hpp}` — `setActiveDevice (codename)` declared at line 153 of `.hpp`, defined at line 116 of
  `.cpp`. **Never called from any QML file** (verified by grep). Phase
  26 must wire this from the sidebar.
- `src/devices/streamdeck/src/register.cpp` — `DeviceDescriptor` rows
  for each AKP SKU; carries `keyCount`, `displayInfo`, `hasClock`,
  `hasEncoders`. Phase 26 needs to **extend this** with per-SKU
  layout (rows × cols + encoder positions + touch-strip presence).
- `src/app/qml/ProfileEditor.qml:296,310,318` — `StreamDockControlService .setBrightness/clearAll(codename, ...)` callsites. These work via
  `m_lookup` fallback (no dependency on m_activeDevice). The image-
  upload path SHOULD use the same pattern OR setActiveDevice should
  fire on sidebar selection.

## Existing Tests Likely Affected

- `tests/unit/test_stream_dock_control_service*.cpp` — 26 tests
  exercising the wire layer. Phase 26's setActiveDevice wiring change
  should not break these (the C++ wire layer doesn't change).
- `tests/qml/` — pre-existing latent link issue per CLAUDE.md; the new
  editor's QML tests will need a working linker target (separate
  follow-up).
  \</code_context>

<specifics>
## Specific Ideas (from user directive 2026-05-28 13:10)

> "devi rendere la UI come quella di Elgato StreamDeck / OpenDeck per i
> dispositivi streamdock altrimenti facciamo fatica a interagire con il
> dispositivo"
>
> "fai l'analisi di OpenDeck e l'implementazione con ricerca non solo
> per l'akp05 ma per tutti i dispositivi AJAZZ che opendeck supporta in
> modo da trovarci già pronti in futuro. sii sempre metodico e molto
> preciso nel lavoro. leggi ed analizza tutto"

- The UI must visually resemble Elgato Stream Deck / OpenDeck — the
  user explicitly stated the current generic grid hampers device
  interaction.
- Research must cover **all AJAZZ devices OpenDeck supports**, not just
  AKP05E. "Trovarci già pronti in futuro" = future-proof the UI for
  every SKU the catalogue may grow to.
- "Methodical and precise" = exhaustive, well-cited, source-grounded
  research; not a high-level summary.
  </specifics>

<deferred>
## Deferred Ideas

- **AK980 PRO RGB keyboard view** — Phase 26 may scope this to "stub
  page; live editor follow-up". The keyboard SKU is RGB / clock /
  battery, not LCD-key — its editor is a different shape entirely.
- **AJ-series mouse DPI / polling panel** — similarly stub-able; the
  existing `MousePanel.qml` exists.
- **Plugin action library** — the left-pane "action library" of
  OpenDeck pulls from registered plugin actions. Phase 26 may scope
  the initial drop sources to built-in actions only; plugin-action
  drag-drop is a Phase 20 / Phase 21 follow-up.
  </deferred>

<next-steps>
## Next Steps

1. Spawn deep research agent: produce `26-RESEARCH.md` covering
   OpenDeck (`nekename/OpenDeck`) + `naerschhersch/opendeck-akp05` +
   `4ndv/mirajazz`. Per-SKU device-geometry map + UI-pattern
   recommendation + drag-drop architecture + action-library model +
   key/encoder/strip drop-target model. Cite specific source files +
   line numbers + commits.
1. After research lands, route to `/gsd-spec-phase 26` for SPEC
   ambiguity scoring, then `/gsd-discuss-phase 26` for context
   gathering on the design choices that aren't already locked here.
1. Once CONTEXT + SPEC are firm, `/gsd-ui-phase 26` produces UI-SPEC,
   then `/gsd-plan-phase 26` produces the executable plans.
1. Phase 26 execution: setActiveDevice wiring lands first (closes
   GAP-25A end-to-end so Phase 25 Test 1 can convert FAIL → PASS).
   Per-SKU layout descriptor table lands next. Then the device-shaped
   editor QML.
1. After Phase 26 ships, **resume Phase 25 25-UAT.md** — re-run
   Tests 1, 6, 13, 14. Demo-unit BLOCKED items (2-5, 15-16) remain
   BLOCKED until a different unit arrives.
   </next-steps>
