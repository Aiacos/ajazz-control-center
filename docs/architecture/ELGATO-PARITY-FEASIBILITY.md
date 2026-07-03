<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Feasibility Study — Faithful Elgato Stream Deck Parity (UI + Plugins)

> **Status: feasibility deliverable (2026-06-20).** Produced by two read-only
> research sweeps — a full inventory of the current AJAZZ Control Center plugin
> system + QML editor, and an authoritative reference for the official Elgato
> Stream Deck desktop software + Stream Deck SDK — cross-checked against
> [`PLUGIN-GAP-ANALYSIS.md`](PLUGIN-GAP-ANALYSIS.md),
> [`../plugin-event-parity.md`](../plugin-event-parity.md),
> [`../protocols/streamdeck/elgato_plugin_protocol.md`](../protocols/streamdeck/elgato_plugin_protocol.md),
> and the project [`constitution`](../../.specify/memory/constitution.md).
> The companion implementation plan is [`ELGATO-PARITY-PLAN.md`](ELGATO-PARITY-PLAN.md).
>
> **Parity audit 2026-06-21:** matrix re-verified against live code. Criterion 6
> (Smart Profiles per-app UI) was already shipped at authoring (`52abf5c`,
> 2026-06-08) and is corrected to DONE. Criterion 7 (live encoder preview, Delta
> B) closed the same morning (`000c90f`) and is now DONE. Criteria 4, 5, 8, 9
> remain genuinely open (backend/data layers exist; editor UI surfaces absent).

## Goal

Make the plugin system and editor a faithful equivalent of the official Elgato
Stream Deck software, using the Elgato SDK protocol over the `mirajazz` sidecar
transport — both how the UI **looks** and how the plugins **work**.

## 1. Verdict

**Highly feasible — no architectural blockers.** The load-bearing compatibility
surface (the WebSocket protocol, registration handshake, the 41-event surface,
the Property Inspector, the device-geometry-driven canvas, profiles,
multi-actions, live key rendering, and one-click install) is **already
implemented and tested** (850 ctest as of feature 002 close). What remains for
"feels like Elgato" is concentrated in **editor UI/UX features and a few render
and feature additions** that all build on existing patterns — not new
architecture, and nothing that touches the `mirajazz` transport or the COD-031
boundary.

## 2. Parity matrix — the 12 traits that define the Elgato experience

| #  | Elgato faithfulness criterion                                            | Our status            | Gap                                                                       |
| -- | ------------------------------------------------------------------------ | --------------------- | ------------------------------------------------------------------------- |
| 1  | Live 1:1 device mirroring (canvas = hardware)                            | DONE                  | —                                                                         |
| 2  | Drag-drop bind from a categorized action list + search                   | DONE                  | List sits left (Elgato: right) — cosmetic                                 |
| 3  | Bottom-docked, plugin-rendered HTML Property Inspector (own WebSocket)   | DONE                  | Live modern-PI WS registration is harness/hardware-gated                  |
| 4  | **Pages + nested Folders** with on-canvas navigation                     | DATA MODEL ONLY       | **No UI** (page bar, folder open/back, breadcrumb) — biggest feature gap  |
| 5  | Multi-Actions + **Multi-Action Switch** (two-state sequence toggle)      | PARTIAL               | Multi-action editor done; Switch UI + `userDesiredState` deferred         |
| 6  | **Smart Profiles** (per-app auto-switch) + per-device profiles       | DONE                  | Per-app assign UI shipped `52abf5c` (2026-06-08): `SettingsPage.qml:171`, `ProfileController::addAppProfileMapping` |
| 7  | Stream Deck + encoders; **feedback layouts previewed live**              | DONE                  | Delta B landed `000c90f` (2026-06-21): `LiveEncoderImageProvider` (`application.cpp:1230`), `DeviceView.qml:134` binds `image://liveencoder` |
| 8  | Title styling (font/size/color/align) + per-state icons                  | PARTIAL               | Title text + preview done; no styling controls; toggle renderer deferred  |
| 9  | Marketplace / one-click `.streamDeckPlugin` install                      | DONE                  | Plugins only — no icon-pack / profile install                            |
| 10 | Full WS event/command protocol + handshake (run 3rd-party plugins as-is) | DONE                  | A few AJAZZ-only routed actions unhandled (intentional)                   |
| 11 | Premium dark theme + Elgato-blue accent, rounded tiles, selection state  | COSMETIC              | Material dark exists; needs a visual-fidelity pass (verify live)          |
| 12 | Correct image resolutions/handling (72/144 keys, 96 enc, 200×100 strip)  | DONE                  | Encoder editor-preview sizing follows once #7 lands                       |

## 3. The real deltas, prioritized by impact

Each delta is labelled A–G and carried into the implementation plan.

- **A — Pages/Folders editor UI** *(criterion 4)*. The single biggest missing
  Elgato feature. The core schema (`Profile::pages`, `ProfilePage{id,name,keys,children}`,
  `Action::OpenFolder` / `Action::BackToParent`, the `device#page#controller#row#column`
  context-id scheme) is **complete and round-trips losslessly** through
  `profileFromJson`/`profileToJson`. Only the UI is absent: a page bar with "+",
  folder-open navigation, a back/return affordance, and a breadcrumb.
  **Effort: M–H. Risk: low** (data layer proven).
- **B — On-screen dial/encoder live-feedback preview** *(criterion 7)*. Add an
  `image://liveencoder` provider mirroring the existing `image://livekey`
  pattern (`live_key_image_provider.hpp`, registered in `application.cpp:1217`),
  fed by `encoder_layout_renderer.cpp` which **already produces the device
  frame** for `setFeedback`/`setFeedbackLayout`. Today the editor canvas shows
  static dial icons while the hardware LCD shows the real layout.
  **Effort: M. Risk: low** (direct template exists). Live-on-hardware
  verification is gated on a retail AKP05E (demo unit emits no input); the
  **editor preview is verifiable headlessly**.
- **C — Multi-Action Switch + toggle per-state renderer** *(criterion 5 / 8)*.
  `ActionInstance` (`action_instance.hpp`) already carries `states[]` +
  `currentState` and recursive `children`; needs the Switch UI plus the
  state-visual renderer (reuse the livekey provider). `userDesiredState` for a
  multi-action multi-state child needs a per-step state field on `core::Action`
  — **COD-031 watch**: keep `nlohmann::json` out of `ajazz_core` (use plain
  fields). **Effort: M. Risk: medium.**
- **D — Smart Profiles per-app assignment UI** *(criterion 6)*. The backend
  exists: `IActiveWindowWatcher` (four platform backends), `Profile::applicationHints`,
  and the focus-approximated `applicationDidLaunch/Terminate` fan-out. Missing
  is the per-app → profile assignment UI. Mirror Elgato's documented quirk:
  smart-switching is active only when the app is minimized (editing mode
  suspends it). **Effort: M. Risk: low–medium.**
- **E — Title styling + per-state icon controls in the Inspector** *(criterion 8)*.
  Inspector enhancement: title font/size/color/alignment controls and a
  per-state icon tray (complements C's per-state visuals). The States[] schema
  already carries `TitleColor`/`TitleAlignment`/`FontFamily`/`FontSize`/`FontStyle`.
  **Effort: L–M. Risk: low.**
- **F — Visual-fidelity pass** *(criterion 2 / 11)*. Action-list placement,
  Elgato-blue accent, key-tile treatment, selection highlight. **Must be
  verified via live screenshots** (constitution V). Exact palette hex and region
  proportions are not published by Elgato — sample from a live install/screenshot.
  **Effort: L. Risk: low.**
- **G — Marketplace icon-pack / profile install** *(criterion 9)*. Additive to
  the existing `.streamDeckPlugin` installer + catalog (`PluginStore.qml`,
  `plugin_catalog_model.*`): handle icon packs and profile bundles.
  **Effort: L–M. Risk: low.**

## 4. Constitution constraints that bound the work

- **Transport untouchable** — all work is the QML editor + plugin-runtime/render
  layer; the `mirajazz` sidecar and the removed C++ AKP wire backends are out of
  bounds. No conflict.
- **COD-031** — relevant only if delta C adds a per-step state field to
  `core::Action`; keep `nlohmann::json` out of `ajazz_core`
  (`grep -rn nlohmann src/core/include/` MUST stay 0).
- **Stream-Deck-shaped editor (III)** — already satisfied; preserve
  drag-onto-canvas binding as the central interaction.
- **Live debug verification (V, NON-NEGOTIABLE)** — every new control needs an
  `objectName` + debug-channel drive + screenshot read. **Two paths are
  harness/hardware-gated** and must be called out honestly, never faked: live
  modern-PI WS registration (needs a PI-bearing key selected; Wayland synthetic
  input does not propagate the binding) and real dial feedback on the device
  (demo unit emits zero input; needs a retail AKP05E / Mirabox N4).
- **Cross-platform (II)** — Windows (`/W4 /WX`) and macOS (`-Werror`) must
  compile clean; QML is portable but verify.
- **Atomic commits + ctest-green + GitFlow (I, II, IX)** — each phase lands as
  independently-revertable Conventional Commits with a green
  `ctest --preset linux-release`.

## 5. Conclusion

A faithful Elgato-equivalent is **achievable as an additive, multi-phase effort
on the existing foundation, with zero architectural rework**. The protocol fight
is already won. The honest caveats are: (1) two verification paths are
hardware/harness-gated, and (2) full A–G is a sizeable body of work, best
phased. The companion [`ELGATO-PARITY-PLAN.md`](ELGATO-PARITY-PLAN.md) sequences
it.

## Appendix — authoritative sources

**Elgato SDK / UX (official):** docs.elgato.com Manifest, WebSocket plugin
events/commands, Dials & Touch Strip, Touch Strip Layout, Property Inspector UI,
Keys, WebSocket changelog, Registration Procedure, Distribution; help.elgato.com
Smart Profiles, Pages, Folders, Multi Actions, Preferences, Global Font Settings.

**In-repo (read before implementing):**
[`elgato_plugin_protocol.md`](../protocols/streamdeck/elgato_plugin_protocol.md),
[`akp_plugin_sdk.md`](../protocols/streamdeck/akp_plugin_sdk.md),
[`plugin-event-parity.md`](../plugin-event-parity.md),
[`PLUGIN-GAP-ANALYSIS.md`](PLUGIN-GAP-ANALYSIS.md).
