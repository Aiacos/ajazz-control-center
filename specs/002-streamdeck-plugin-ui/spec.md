# Feature Specification: Elgato-Parity Plugin UI for Keys & Dials + One-Click Install

**Feature Branch**: `002-streamdeck-plugin-ui`

**Created**: 2026-06-18

**Status**: Draft

**Input**: User description: "Voglio rimuovere il tasto open quando vado ad installare un plugin, non serve che si apre una barra laterale per installarlo, Install deve essere al posto di open. Voglio che migliori il layout e l'usabilità quando viene caricato un plugin in un pulsante e voglio che ti occupi anche di gestire i dial. I Pulsanti e Dial con i plugin devono avere una UI uguale a quella di Elgato StreamDeck. Ricerca in modo accurato come devono funzionare."

## Overview

Two connected problems with the plugin experience: **(1)** installing a plugin is
inconsistent — some catalog rows install in-app while others show an "Open page"
button that launches an external browser instead of installing; and **(2)** once a
plugin is bound to a control, the on-key and (especially) on-dial presentation and
configuration do not match what users expect from the Elgato Stream Deck desktop
software. This feature makes **install a single in-app action ("Install" replaces
"Open")** and makes a **plugin on a key or a dial look, render, and configure like
Elgato Stream Deck** — including first-class support for **dials** (the Stream Deck +
rotary encoders + touch strip).

The reference experience throughout is the **Elgato Stream Deck desktop software**:
the device canvas in the centre (key grid; for a Stream Deck + / dial device, a touch
strip with one segment above each rotary dial), the action list on the right, and the
selected control's Property Inspector configuration surface. Plugin-bound keys render
the action's image + title with live updates; plugin-bound dials render a feedback
layout (icon / title / value / indicator) in the touch-strip segment above the dial and
respond to rotate, press, and touch.

## Clarifications

### Session 2026-06-18

- Q: For Elgato Stream Deck + parity, how should the touch-strip segment above each dial relate to the dial (vs the current model of 4 independently-bindable touch zones)? → A: The segment belongs to the dial — binding a plugin to a dial drives its touch-strip segment (feedback layout) and the tap-on-segment interaction; dial + its segment are one control (Elgato model). Independent touch-zone bindings are retired for dial devices.
- Q: Should this feature make currently browser-only catalog sources (e.g. Mirabox, behind a private CDN) installable in-app, or limit scope? → A: Limit scope to the UI + install-flow change. Make Install in-app for every source that already exposes a resolvable package; do not reverse-engineer private/secret download paths. Sources that cannot be resolved are marked unavailable.
- Q: When a plugin genuinely cannot be installed in-app, how should its store row appear now that "Open" is removed? → A: The row is shown but its action is disabled, with a short "not installable in-app" reason (discoverable + honest); it is never offered as a browser "Open".

## User Scenarios & Testing *(mandatory)*

### User Story 1 - One-click in-app install (Install replaces Open) (Priority: P1)

A user browses the plugin store, finds a plugin, and installs it with a single
**Install** action that completes **inside the app** — no external browser opens, no
side panel/drawer is required to complete the install. When the install finishes, the
plugin's actions appear in the action list, ready to drop onto a key or dial.

**Why this priority**: This is the user's primary, explicitly-stated request. The
current dual behaviour — "Install" for some rows, "Open page ↗" (browser launch) for
others — is confusing and breaks the "browse → install → use" flow. Removing the
"Open" affordance and making every listed plugin install in-app is the highest-value
change.

**Independent Test**: From the plugin store, click the primary action on any listed
plugin and confirm it installs in-app (progress shown inline, button becomes
"Installed", actions appear in the list) without any browser window or side
drawer opening.

**Acceptance Scenarios**:

1. **Given** a not-yet-installed plugin in the store, **When** the user clicks its
   primary action, **Then** the button reads **Install**, the plugin downloads and
   installs in-app with inline progress, and **no external browser or side drawer
   opens**.
1. **Given** an install completes successfully, **When** the user returns to the
   editor, **Then** that plugin's actions are present in the action list and can be
   dragged onto a key or dial without any extra navigation.
1. **Given** an already-installed plugin, **When** the user views its row, **Then**
   the action reads **Installed** (with an uninstall affordance), never **Open**.
1. **Given** a plugin whose package cannot be resolved for in-app installation,
   **When** the user views its row, **Then** its install action is **disabled and
   labelled with a short "not installable in-app" reason** — never offered as a
   browser "Open".

______________________________________________________________________

### User Story 2 - A plugin on a KEY looks and configures like Elgato Stream Deck (Priority: P1)

A user drags a plugin action onto a key. The key immediately shows the action's image
and title and updates live as the plugin pushes new visuals (e.g. a value, an icon).
Selecting the key opens the plugin's configuration (Property Inspector) together with
the standard title/image controls, arranged the way the Elgato Stream Deck software
arranges them, so the experience is familiar and the binding is easy to read and edit.

**Why this priority**: Keys are the most common control and the most visible surface;
faithful rendering + a clean, Elgato-shaped configuration layout is the core of the
"improve layout and usability when a plugin is loaded onto a button" request.

**Independent Test**: Drop a plugin action onto a key and confirm the key shows the
action's image + title, updates live, and that selecting it presents a Property
Inspector + title/image controls in a clean, Elgato-faithful layout.

**Acceptance Scenarios**:

1. **Given** a plugin action in the action list, **When** the user drops it on a key,
   **Then** the key shows the action's image and title and reflects live updates the
   plugin pushes.
1. **Given** a plugin-bound key, **When** the user selects it, **Then** the plugin's
   Property Inspector and the title/image controls are shown together in a layout that
   mirrors the Elgato Stream Deck software (action visual dominant, title overlay,
   selected control highlighted).
1. **Given** a plugin-bound key is selected, **When** the user changes a setting in the
   Property Inspector, **Then** the change takes effect and the key preview reflects it.

______________________________________________________________________

### User Story 3 - A plugin on a DIAL looks and configures like Elgato Stream Deck (Priority: P2)

On a device with rotary dials and a touch strip (Stream Deck + class), a user drags a
dial-capable plugin action onto a dial. The touch-strip segment **above that dial**
renders the plugin's feedback layout (icon, title, value, and/or indicator), and the
dial responds to **rotating, pressing, and tapping** the touch strip. Selecting the
dial opens its Property Inspector and dial-specific controls, arranged like Elgato's
Stream Deck + experience.

**Why this priority**: Dials are currently under-served compared to keys; the user
explicitly asked to "handle the dials" with an Elgato-equivalent UI. It depends on the
same drag-to-bind and Property-Inspector patterns as keys (US2) but adds the
touch-strip feedback surface and the rotate/press/touch interactions, so it follows
keys in priority.

**Independent Test**: Drop a dial-capable plugin action onto a dial of a Stream Deck +
class device and confirm the touch-strip segment above it renders the plugin's feedback
layout, that rotate/press/touch each reach the plugin, and that selecting the dial shows
an Elgato-shaped dial configuration.

**Acceptance Scenarios**:

1. **Given** a dial-capable plugin action, **When** the user drops it on a dial,
   **Then** the touch-strip segment above that dial shows the plugin's feedback layout
   (icon / title / value / indicator) and updates live.
1. **Given** a plugin-bound dial, **When** the user rotates it, presses it, or taps its
   touch-strip segment, **Then** each interaction reaches the bound plugin action.
1. **Given** a plugin-bound dial, **When** the user selects it, **Then** its Property
   Inspector and dial controls (title, icon, feedback layout) are shown in an
   Elgato-faithful layout.
1. **Given** a plugin action that supports only keys (not dials), **When** the user
   tries to drop it on a dial, **Then** the drop is rejected with a clear "not
   supported here" affordance (and vice-versa for dial-only actions on keys).

______________________________________________________________________

### User Story 4 - Consistent, Elgato-faithful editor layout & affordances (Priority: P3)

Across keys and dials, the editor presents a single, coherent, Elgato-faithful layout:
the device canvas centre-stage, the action list to the side, and the selected control's
configuration below; drag-to-bind highlights valid drop targets; the selected control is
clearly highlighted; and a live preview mirrors what the device shows. The result reads
as an equivalent of the Elgato Stream Deck software to anyone familiar with it.

**Why this priority**: This is the connective polish that makes US2/US3 feel cohesive.
It is valuable but builds on the per-control work, so it is lowest priority.

**Independent Test**: Walk the editor with both a key and a dial bound to plugins and
confirm a consistent arrangement, selection highlighting, drag-target highlighting, and
live preview that a Stream Deck user recognizes.

**Acceptance Scenarios**:

1. **Given** the editor is open, **When** the user drags an action over a valid target,
   **Then** the target is highlighted; over an invalid target, a reject state is shown.
1. **Given** any control is selected, **When** the user looks at the canvas, **Then**
   the selected control is visually highlighted and its configuration is shown in a
   consistent location.
1. **Given** a plugin pushes a live visual, **When** the user looks at the on-screen
   control, **Then** the preview matches what the physical device renders.

### Edge Cases

- A plugin's package cannot be resolved for in-app install (no direct/resolvable
  download — e.g. a vendor store exposing only a relative path behind a private CDN):
  the row's install action is shown **disabled with a "not installable in-app" reason**,
  not as a browser "Open" (the feature does not RE private download paths).
- An install fails mid-download (network/extraction error): the row shows an inline
  failure message and returns to an installable state; nothing partially-installed is
  left usable.
- A plugin action is dropped on the wrong control type (key-only action on a dial, or
  dial-only on a key): the drop is rejected with a clear affordance, leaving the target
  unchanged.
- A plugin provides no image/title for a state: the key/dial shows a sensible
  placeholder rather than an empty or broken visual.
- A dial-capable plugin provides no recognized feedback layout: the touch-strip segment
  falls back to a default icon+title presentation.
- The selected device has no dials (key-only device): dial-specific UI is simply absent;
  the key experience is unchanged.
- A plugin is uninstalled while one of its actions is bound to a control: the control
  reverts to an unbound state with a clear indication, without crashing the editor.

## Requirements *(mandatory)*

### Functional Requirements

#### Plugin install (US1)

- **FR-001**: The plugin store MUST present a single primary install action per plugin
  with states **Install → Installing (with inline progress) → Installed**, and MUST NOT
  present an "Open"/"Open page" action that launches an external browser.
- **FR-002**: Installing a plugin MUST complete entirely in-app, without opening an
  external browser or requiring a separate side panel/drawer to perform the install.
- **FR-003**: After a successful install, the plugin's actions MUST appear in the
  editor's action list without any further user navigation.
- **FR-004**: The store MUST show inline per-row install **progress** and a clear
  **success or failure** outcome; a failed install MUST return the row to an installable
  state and leave nothing partially-installed usable.
- **FR-005**: An already-installed plugin MUST show an **Installed** state with an
  uninstall affordance; uninstalling MUST remove its actions from the action list.
- **FR-006**: A plugin that cannot be resolved to an in-app-installable package MUST be
  shown with its install action **disabled and labelled with a short "not installable
  in-app" reason**; it MUST NOT be offered as a browser "Open". The feature MUST NOT
  reverse-engineer private/secret download paths to force-resolve such sources (scope
  limit — see Assumptions).

#### Plugin on a key (US2)

- **FR-007**: A key bound to a plugin action MUST display the action's image and title,
  and MUST update live when the plugin pushes new image/title/state.
- **FR-008**: A user MUST be able to bind a plugin action to a key by dragging it from
  the action list onto the key, with the key highlighted as a valid drop target during
  the drag.
- **FR-009**: Selecting a plugin-bound key MUST present the plugin's Property Inspector
  together with the standard title/image controls, in a layout consistent with the
  Elgato Stream Deck software (action visual dominant, title overlay, selection
  highlight).
- **FR-010**: Changes made in a plugin-bound key's Property Inspector MUST take effect
  and be reflected in the on-screen key preview.

#### Plugin on a dial (US3)

- **FR-011**: The system MUST allow binding dial-capable plugin actions to dials
  (rotary encoders) on dial-equipped devices, via drag-to-bind from the action list. On
  dial devices, the **touch-strip segment above a dial is part of that dial's binding**
  (the dial and its segment are one control, the Elgato Stream Deck + model) — it is NOT
  an independently-bindable touch-zone control.
- **FR-012**: A dial bound to a plugin action MUST render the plugin's feedback layout
  (icon / title / value / indicator, per the action's declared layout) in the
  touch-strip segment positioned above that dial, matching the Stream Deck + presentation,
  and MUST update live.
- **FR-013**: A plugin-bound dial MUST route **rotate**, **press**, and **touch-strip
  tap** interactions to the bound plugin action.
- **FR-014**: Selecting a plugin-bound dial MUST present its Property Inspector and
  dial-specific controls (title, icon, feedback layout) in an Elgato-faithful layout.
- **FR-015**: The editor MUST reject binding a plugin action to a control type the
  action does not support (key-only action onto a dial, or dial-only onto a key) with a
  clear "not supported here" affordance, leaving the target unchanged.

#### Cross-cutting layout & usability (US4)

- **FR-016**: The editor MUST use one coherent, Elgato-faithful arrangement for plugin
  controls: device canvas centre-stage (key grid and, where present, a touch strip with
  one segment above each dial), action list alongside, and the selected control's
  configuration in a consistent location.
- **FR-017**: The currently-selected control MUST be visually highlighted on the canvas,
  and valid drop targets MUST be highlighted (and invalid ones shown as rejected) during
  a drag.
- **FR-018**: The on-screen representation of a plugin-bound key or dial MUST mirror what
  the physical device renders (live preview parity).
- **FR-019**: The behaviour and presentation MUST be researched against and faithful to
  the Elgato Stream Deck software's handling of plugin-bound keys and Stream Deck + dials
  (constitution Principle VI — research-backed; Principle III — UX consistency).

### Key Entities *(include if feature involves data)*

- **Plugin (catalog entry / installed package)**: a distributable bundle of actions; in
  the store it has an availability/installability state (installable in-app vs
  unavailable) and an installed/not-installed state.
- **Plugin Action**: a capability the plugin exposes; declares which control types it
  supports (key, dial, and/or touch), an image/title, and — for dial actions — a
  feedback layout. Drives both the action-list entry and what renders on the bound
  control.
- **Key binding**: the association of a plugin action to a key position; carries the
  rendered image/title/state shown on the key.
- **Dial binding**: the association of a plugin action to a rotary dial; **owns the
  touch-strip segment above that dial** (its feedback layout + tap interaction) as a
  single control, plus the rotate/press routing. On dial devices there is no separate
  touch-zone binding — the segment is always part of its dial (Elgato Stream Deck +
  model; supersedes the prior independently-bindable touch-zone control).
- **Feedback layout**: the structured visual a dial action renders in its touch-strip
  segment (icon, title, value, indicator/bar variants).
- **Property Inspector**: the per-control plugin configuration surface shown when a
  plugin-bound key or dial is selected.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user can install any listed, available plugin and have its actions ready
  to use with a **single click** and **zero external-browser or side-drawer openings**.
- **SC-002**: **100%** of plugin store rows present a consistent Install / Installing /
  Installed / unavailable state — **zero** rows present an "Open"/browser action.
- **SC-003**: A plugin bound to a key shows its image and title within **1 second** of
  binding and reflects live plugin updates.
- **SC-004**: A plugin bound to a dial renders its feedback in the touch-strip segment
  above the dial and routes **all three** interactions (rotate, press, touch tap) to the
  plugin.
- **SC-005**: In usability review, users familiar with the Elgato Stream Deck software
  recognize the key and dial editing experience as **equivalent** (layout, affordances,
  live preview) with no retraining needed.
- **SC-006**: After a failed install, **100%** of the time the row returns to an
  installable state and no partially-installed plugin is left usable.
- **SC-007**: Binding an action to an unsupported control type is rejected **100%** of
  the time with a clear affordance, with **zero** silent or broken bindings.

## Assumptions

- **"Dials"** refers to the rotary encoders + touch strip of the **Stream Deck + class**
  devices (the AKP05E / N4 family with 4 dials and a touch strip). Key-only devices are
  unaffected by the dial-specific requirements. **The touch-strip segment above each dial
  belongs to that dial** (one control = dial + segment; Elgato Stream Deck + model) —
  this supersedes the project's prior model of 4 independently-bindable touch zones for
  dial devices (clarified 2026-06-18).
- **"Elgato Stream Deck UI"** means the layout, affordances, and rendering conventions of
  the **Elgato Stream Deck desktop software**: centred device canvas (key grid; touch
  strip with one segment above each dial for dial devices), action list alongside, and a
  per-control Property Inspector configuration surface — to be confirmed by accurate
  research during planning (Principle VI).
- **Install scope (clarified 2026-06-18)**: the feature is limited to the **UI +
  install-flow** change. Install becomes in-app for every catalog source that already
  exposes a resolvable package; the feature does **not** reverse-engineer private/secret
  download paths (e.g. the Mirabox store's CDN base) to force-resolve browser-only
  sources. A source that cannot be resolved is **shown with its install action disabled
  and a short "not installable in-app" reason** — never a browser "Open". (This is why
  the spec keeps the plugin visible-but-disabled rather than hiding it.)
- The **existing plugin runtime and contract** (Elgato/OpenDeck plugin protocol, the
  out-of-process device rendering path, the Property Inspector mechanism, and the dial
  feedback-layout rendering) are **reused**; this feature is UI/UX + install-flow work,
  not a new plugin engine.
- **Live device rendering parity** relies on the existing mechanism that mirrors device
  renders onto the on-screen control.
- This feature targets the desktop application's editor and plugin store; no change to
  the plugin wire protocol is assumed.

## Dependencies

- The installed catalog / store data source(s) and their per-plugin package
  resolvability determine which rows are "available"; making every desired source
  in-app-installable may require resolving direct package URLs for sources that today
  only expose an upstream page.
- The dial feedback-layout rendering and the Property Inspector surface already exist and
  are depended upon for US3.
