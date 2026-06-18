# Phase 0 Research — Elgato-Parity Plugin UI for Keys & Dials + One-Click Install

**Feature**: Elgato-parity plugin UI on keys + dials, one-click in-app install (scoped from
[spec.md](./spec.md))
**Date**: 2026-06-18
**Method**: Direct read of the live code (Explore sweep over `PluginStore.qml`,
`plugin_catalog_model.{hpp,cpp}`, `profile.hpp`, `EncoderDial.qml`, `TouchStripLane.qml`,
`DeviceCanvas.qml`, `Inspector.qml`, `profile_controller.hpp`, `encoder_layout_renderer.hpp`)
cross-referenced with the feature-001 artifacts and Elgato Stream Deck SDK / Stream Deck +
domain knowledge. Each entry: **Decision → Rationale → Alternatives considered**.

This document resolves every NEEDS CLARIFICATION and grounds the design. It also satisfies
spec **FR-019** (behaviour researched against the Elgato Stream Deck software).

______________________________________________________________________

## A. How the Elgato Stream Deck software actually presents plugin controls (FR-019)

### A1. Keys

- **Decision**: A plugin-bound key renders the action's **image** with the **title overlaid
  at the bottom**, updating live as the plugin pushes `setImage`/`setTitle`/`setState`.
  Selecting a key shows, below the canvas, the standard **Title / Image** controls plus the
  plugin's **Property Inspector**. The selected key is outlined.
- **Rationale**: This is the Elgato Stream Deck editor layout and is already largely how this
  app renders keys (`KeyCell.qml` paints `image://livekey`, title overlay, selection outline;
  the System-Monitor "CPU 15%" live render was verified in feature 001). The key work is
  **polish + a clean Property-Inspector-shaped selection layout**, not new rendering.
- **Alternatives considered**: A separate "key designer" modal — rejected; Elgato keeps the
  inspector inline below the canvas, which the app already does.

### A2. Dials + touch strip (Stream Deck + class)

- **Decision**: On a Stream Deck + the device has 8 keys, a **touch strip** divided into **4
  segments — one directly above each of the 4 rotary dials** — and the 4 dials. A dial-capable
  plugin action bound to a dial drives **both** the dial interactions **and** the touch-strip
  segment above it: the segment renders the action's **feedback layout** (icon / title / value
  / indicator bar) and a **tap on the segment routes to that dial's action** (`touchTap`).
  Rotating sends `dialRotate` (signed ticks), pressing sends `dialDown`/`dialUp`. The dial and
  its segment are **one control**.
- **Rationale**: This is exactly the Elgato Stream Deck + model — the touch strip is the dials'
  feedback display, not an independent surface; an Elgato dial action declares
  `Controllers: ["Encoder"]` with an `Encoder` block (`layout`, `Icon`, `background`,
  `TriggerDescription`, `StackColor`) and drives the segment via `setFeedbackLayout` +
  `setFeedback`. The app **already has** the renderer (`encoder_layout_renderer`: `$X1`/`$A0`/
  `$A1`/`$B1`/`$B2`/`$C1`), the dial input routing (feature-001 `dialRotate`/`dialDown`/
  `dialUp` + the new `onRelease`), and `setTriggerDescription` routing (feature-001 T025). The
  parity gap is the **UI model**: today the segment is an *independent* touch zone (see B1).
- **Alternatives considered**: Keep the touch strip as a free-form independent surface
  (rejected by the clarification — diverges from Elgato and confuses users); render dial
  feedback in a side panel instead of the strip (rejected — not hardware-faithful, breaks the
  Principle III "match physical reality" rule).

### A3. The feedback layouts already implemented

- **Decision**: Reuse `renderEncoderLayout(layoutId, feedback, target)` for the on-screen
  segment preview exactly as it renders to the device LCD zone. Layouts: `$X1` (icon+title),
  `$A0` (title + full image), `$A1` (title + icon-left + value-right), `$B1`/`$B2` (`$A1` +
  plain/gradient bar), `$C1` (two icon+bar rows). Feedback item keys: `title`, `value`, `icon`,
  `indicator`, `icon2`, `indicator2`.
- **Rationale**: The renderer is the device source-of-truth; using it for the on-screen segment
  guarantees live-preview parity (SC, FR-018). No new rendering needed.
- **Alternatives considered**: A separate QML-side layout renderer (rejected — would drift from
  the device render and duplicate logic).

______________________________________________________________________

## B. The touch-zone → dial data-model change (the core design decision)

### B1. Current state (confirmed by code read)

- `EncoderBinding` (`profile.hpp:112`): `onCw` / `onCcw` / `onPress` / `onRelease` chains +
  `KeyState state` + optional `ActionInstance instance`. Stored in
  `Profile::encoders` (`unordered_map<uint16, EncoderBinding>`).
- `TouchZoneBinding` (`profile.hpp:134`): `onTap` chain + `KeyState state`. Stored
  **separately** in `Profile::touchZones` (`unordered_map<uint8, TouchZoneBinding>`).
- QML: `EncoderDial.qml` (controller **"Encoder"**, `commitEncoderBinding`) and
  `TouchStripLane.qml`/`TouchZoneCell` (controller **"TouchZone"**, `commitTouchZoneBinding`)
  are **two independent drop targets** with separate affordance bits (dial bit 2, zone bit 4).
- So today a touch zone can hold a *different* action than the dial below it — the opposite of
  the Elgato model.

### B2. Decision — the segment belongs to the dial

- **Decision**: On **dial devices**, the touch-strip segment N is owned by **encoder N's
  binding**. There is **no independent touch-zone binding** offered in the UI for dial devices:
  - The segment renders encoder N's bound-action **feedback layout** (via
    `renderEncoderLayout`), and a **tap on segment N routes to encoder N's action** as
    `touchTap` (the existing encoder context `device#<pageId>#Encoder#0#N`; the default page id is
    `root`, so the concrete id is `device#root#Encoder#0#N` — see D1).
  - `EncoderBinding` is the single carrier; the touch interaction is part of the encoder
    action, not a separate binding.
- **Rationale**: Exact Elgato Stream Deck + parity (the clarified decision). It also simplifies
  the mental model (one control = dial + its segment) and removes the cross-controller drag
  confusion.
- **Alternatives considered**: (a) Keep both and let dial-default + independent-override
  coexist (the hybrid option) — rejected by the clarification (most complex, least faithful).
  (b) Delete `TouchZoneBinding` outright — rejected for backward compatibility (B3).

### B3. Decision — backward-compatible read of old profiles

- **Decision**: The profile **reader keeps parsing** a `touchZones` array (no crash, no loss)
  so v2 profiles authored before this change still load. The **writer stops emitting**
  independent touch-zone bindings for dial devices; the UI no longer exposes a separate
  TouchZone drop target. On load, a legacy `touchZones[N]` that carries a binding while
  `encoders[N]` is empty MAY be surfaced as the encoder N segment (best-effort fold), but the
  default is simply to retain it in-model and stop writing new ones (lossless, forward-only).
- **Rationale**: Constitution Principle I (atomic, non-destructive) + the spec constraint
  ("must load without loss/corruption"). The schema doc is the source of truth (Principle VII),
  so the `touchZones` key is marked **deprecated**, not removed.
- **Alternatives considered**: Hard-migrate + drop the key on first save — rejected; silently
  rewriting a user's profile risks data loss and surprises.

### B4. Decision — keyboard/AK-series touch zones (if any) are out of scope

- **Decision**: This change targets **Stream Deck + dial devices** only. If any non-dial device
  exposes touch zones, they are unaffected (the `touchZones` model stays for them).
- **Rationale**: The spec scopes "dials" to the Stream Deck + class; non-dial touch zones are
  not part of the Elgato dial model.

______________________________________________________________________

## C. The install flow (Install replaces Open)

### C1. Current state (confirmed)

- `PluginCatalogModel::install(uuid)` (`plugin_catalog_model.cpp:1302`): if `downloadUrl` is a
  valid `https` URL → download via `QNetworkAccessManager`; **else → `openUpstream(uuid)`**
  (`:1327`) → `QDesktopServices::openUrl(...)` (`:1575`) — the browser launch.
- `PluginStore.qml` (`:905`,`:913`): the button reads **"Install"** when `downloadUrl` is
  non-empty, **"Open page ↗"** otherwise; both onClick call the same `install()`.

### C2. Decision — remove the browser fallback; Install is in-app only

- **Decision**: `install()` performs an **in-app install only**. When a catalog entry has no
  resolvable `downloadUrl`, `install()` does **not** open a browser — the entry is reported as
  **not installable in-app** (a per-entry `installable: bool` + a short `reason`). The
  `openUpstream()` browser path is removed from the install action. The QML row binds its
  primary button to: `Install` (enabled) when installable; **disabled with a "Not installable
  in-app" label/tooltip** otherwise; `Installing… N%`; `Installed` (with uninstall).
- **Rationale**: The explicit user requirement ("Install deve essere al posto di open", no
  browser, no sidebar). Disabled-with-reason (the clarification) keeps the plugin discoverable
  and honest.
- **Alternatives considered**: Keep `openUpstream` behind a tiny separate "ⓘ details" link
  (deferred — not part of the install action; can be added later without re-introducing an
  "Open" install affordance); hide non-installable rows entirely (rejected by the
  clarification — loses discoverability).

### C3. Decision — install scope is UI + flow only (no RE of private sources)

- **Decision**: Do **not** reverse-engineer private/secret download paths (e.g. the Mirabox
  store's CDN base, known-infeasible per project memory `reference_plugin_install_zip64`). A
  source that already exposes a resolvable `downloadUrl` installs in-app; the rest are
  "not installable in-app".
- **Rationale**: Bounds the feature to the achievable UI/flow change; resolving a secret CDN
  base is out of scope and may be impossible (Principle VI honesty).
- **Alternatives considered**: Build a Mirabox URL resolver (rejected — RE-heavy, may fail,
  out of the stated scope).

______________________________________________________________________

## D. Property Inspector for dials (already present — reuse)

### D1. Decision — reuse the existing encoder PI path

- **Decision**: Selecting a dial already drives an **Encoder** Property-Inspector context
  (`Inspector.qml`: `encoderIndex >= 0` → context `device#<pageId>#Encoder#0#N`, emitted as the
  concrete `device#root#Encoder#0#N` for the default page →
  `PropertyInspectorController.loadInspector(...)`). Reuse it; the feature adds the
  Elgato-shaped **dial config controls** (title, icon, feedback layout) around it and ensures
  the PI dock is consistent with the key path.
- **Rationale**: No new PI mechanism needed; feature-001 landed the modern-PI WS bootstrap and
  dedup. This is layout/consistency work.
- **Alternatives considered**: A bespoke dial inspector (rejected — duplicates the PI host).

______________________________________________________________________

## E. Verification approach (Principle V) and the known harness gap

### E1. Decision — verify via synthetic events + profile-inject; flag the residual

- **Decision**: Drive verification through the debug-control channel: `device.setActiveDevice`
  to populate the canvas (feature-001 binding); profile-inject an encoder binding +
  `profile.load` to render a dial segment; `input.encoder`/`input.encoderPress`/`input.touch`
  to exercise routing; `plugin.simulateAction` + `plugin.protocolLog` + `screenshot` to confirm
  feedback render and PI round-trip. The **modern-PI-open-via-selection** walk remains
  harness-gated (synthetic click-to-select does not propagate the binding to the Inspector —
  feature-001 T030); it is flagged for a manual session, not blocking.
- **Rationale**: Honest, repeatable verification within the known harness limits (Principle V +
  the inherited WATCH).
- **Alternatives considered**: Block the feature on a synthetic-input fix (rejected — out of
  scope; the dial render + routing are verifiable without it).

______________________________________________________________________

## F. Resolved unknowns (no NEEDS CLARIFICATION remain)

| Question                                                     | Resolution                                                                                                                     |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| Does the touch strip belong to the dial or stay independent? | **Belongs to the dial** (B2); independent `touchZones` deprecated for dial devices, read-compat retained (B3).                 |
| Where is the browser "Open" launched?                        | C++ `PluginCatalogModel::install()` → `openUpstream()` → `QDesktopServices::openUrl` (C1); removed from the install path (C2). |
| Must non-installable sources be made installable?            | No — UI + flow only, no RE of private paths (C3); such rows are disabled-with-reason.                                          |
| Is dial rendering/PI new work?                               | No — reuse `encoder_layout_renderer` + the existing encoder PI context (A3, D1); the work is the UI model + parity.            |
| How is this verified given the harness gap?                  | Synthetic `input.*` + profile-inject + `plugin.simulateAction` + screenshot; modern-PI-open flagged for manual (E1).           |
