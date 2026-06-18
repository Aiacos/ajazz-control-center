# Phase 1 Data Model — Elgato-Parity Plugin UI for Keys & Dials + One-Click Install

**Feature**: Elgato-parity plugin UI on keys + dials, one-click in-app install
**Date**: 2026-06-18
**Source of truth for wire keys**: the profile schema docs + `docs/schemas/**` and
`docs/protocols/streamdeck/**` (a C++ field name never overrides a documented JSON key).
Serialization is hand-rolled and **nlohmann-free** in `ajazz_core` (COD-031).

This captures the entities, fields, relationships, validation, and state transitions the
feature touches. It is a design reference, not implementation code. Only the **deltas** from
today's model are normative; everything else is reused unchanged.

______________________________________________________________________

## 1. Binding model (`ajazz_core`, `profile.hpp`)

### EncoderBinding — now owns the touch-strip segment (CHANGE)

- **Today**: `onCw[]`, `onCcw[]`, `onPress[]`, `onRelease[]`, `KeyState state`,
  `std::optional<ActionInstance> instance`. Stored in
  `Profile::encoders` (`unordered_map<uint16, EncoderBinding>`).
- **Change (this feature)**: the EncoderBinding is the **single carrier for the dial AND the
  touch-strip segment above it** on dial devices:
  - The segment renders this binding's bound-action **feedback layout** (via
    `renderEncoderLayout`); no separate visual struct is introduced — the dial action's
    `instance`/manifest layout drives it, and `state` remains the fallback label.
  - A **tap on segment N** routes to encoder N's action as `touchTap` using the existing
    encoder context `device#<pageId>#Encoder#0#N` (default page id `root` → `device#root#Encoder#0#N`). No new chain field is required for the common
    case (the plugin's `touchTap` handler receives it); an optional `onTap[]` MAY be added to
    `EncoderBinding` only if a built-in (non-plugin) tap action is needed — deferred unless a
    task proves it necessary.
- **Rule**: on dial devices the dial and its segment are one control; binding/clearing the dial
  binds/clears the segment. Reader gates on key presence, never a schema version.

### TouchZoneBinding / `Profile::touchZones` — DEPRECATED for dial devices (read-compat)

- **Today**: `TouchZoneBinding { onTap[]; KeyState state; }` in
  `Profile::touchZones` (`unordered_map<uint8, TouchZoneBinding>`), bound independently via the
  `"TouchZone"` controller + `commitTouchZoneBinding`.
- **Change**: marked **deprecated** for dial devices:
  - **Reader**: keeps parsing `touchZones` losslessly (old profiles load, no crash) — the field
    is retained in-model for forward-compat; a legacy `touchZones[N]` MAY be best-effort folded
    onto encoder N's segment when `encoders[N]` is empty (optional, behind a clear rule).
  - **Writer**: stops emitting **new** independent touch-zone bindings for dial devices; the
    schema doc marks the key `deprecated`.
  - **UI**: the standalone `"TouchZone"` drop target + `commitTouchZoneBinding` path are retired
    for dial devices; the touch-strip lane becomes a render+tap surface owned by the dial lane.
- **Validation**: indices range-checked; unknown wire keys skipped (forward-compatible);
  a profile carrying both `encoders[N]` and a legacy `touchZones[N]` resolves to the encoder
  (the dial wins — Elgato model).

### Binding (key) / ActionInstance / ActionState — UNCHANGED

- Reused as-is from feature 001 (`instance`, `currentState`, `states[]`, multi-action
  `children[]`). The key UI work is rendering/inspector-layout polish, not model change.

______________________________________________________________________

## 2. Catalog / install model (`plugin_catalog_model.{hpp,cpp}`)

### CatalogEntry — add an explicit installability surface (CHANGE)

- **Today**: `… downloadUrl (QUrl, empty = no direct download) …`; `install()` falls back to
  `openUpstream()` → browser when `downloadUrl` is empty.
- **Change**: expose the installability decision to the UI instead of hiding it behind a
  browser fallback:
  - `installableInApp: bool` — true iff a resolvable `https` package URL exists.
  - `unavailableReason: string` — short human text shown when `installableInApp == false`
    (e.g. "not installable in-app"); empty otherwise.
  - These are derived (read-only to QML); no new persisted state.
- **State transitions (per row, surfaced to QML)**:
  `Installable(enabled "Install") → Installing(N%) → Installed(uninstall)`; the failure edge
  returns `Installing → Installable` with an inline error; the
  `NotInstallable(disabled + reason)` state is terminal-until-source-changes and never offers a
  browser "Open".
- **Validation / rules**:
  - `install(uuid)` performs an **in-app install only**; it MUST NOT call `openUpstream()` /
    `QDesktopServices::openUrl`. For a non-installable entry it is a no-op returning false.
  - The `openUpstream()` browser launch is removed from the install path (it MAY survive only as
    an explicit, separate "details" affordance — not the primary install action — if a later
    task wants it; default: removed).
  - Install scope is UI + flow only — no resolution of private/secret download paths.

______________________________________________________________________

## 3. Plugin action capability (existing — used for drop gating)

### PluginAction.controllers — drives drop-target gating (REUSE)

- An action declares supported controllers (`Keypad`, `Encoder`, and the vendor
  `Knob`→`Encoder` normalization). The editor's drop gating MUST:
  - allow a `Keypad`-capable action onto a **key**; reject otherwise.
  - allow an `Encoder`-capable action onto a **dial** (which now includes its segment); reject
    otherwise.
  - There is no independent `TouchZone` drop target on dial devices (folded into the dial).
- The affordance-mask bits used by the QML drop areas are updated accordingly (the dial drop
  target accepts encoder-capable actions; the former independent touch-zone bit is retired for
  dial devices).

______________________________________________________________________

## 4. Property Inspector context (existing — reuse)

> **Wire-key note (source of truth, Principle VII):** the second component is `ctx.pageId`
> (`ContextRegistry::deriveContextId` = `deviceId#pageId#controller#row#col`). The codebase's
> comment convention writes it as the placeholder `#page#`; the **default page's id is `root`**,
> so the concrete id emitted by `Inspector.qml::_contextUuid` is `device#root#…`. Both notations
> below denote the same string with `pageId == "root"`.

- **Key PI context**: `device#<pageId>#Keypad#row#col` (default page → `device#root#Keypad#row#col`).
- **Dial PI context**: `device#<pageId>#Encoder#0#N` (default page → `device#root#Encoder#0#N`;
  unchanged — already produced by `Inspector.qml` when `encoderIndex >= 0`). The dial's
  touch-strip segment shares this context (the segment is the dial), so a `touchTap` and the
  dial's `setFeedback` round-trip through the same PI/owner.

______________________________________________________________________

## 5. Rendering entities (existing — reuse)

- **Feedback layout** (`encoder_layout_renderer`): `$X1` / `$A0` / `$A1` / `$B1` / `$B2` /
  `$C1`; feedback item keys `title`, `value`, `icon`, `indicator`, `icon2`, `indicator2`;
  `renderEncoderLayout(layoutId, feedback, target)`. Reused to render the **on-screen segment
  preview** identically to the device LCD zone (live-preview parity).
- **Live key preview** (`image://livekey/<index>`): reused for keys; an analogous live preview
  is used for dial segments so the canvas mirrors the device (FR-018).
