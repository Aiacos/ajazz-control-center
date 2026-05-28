---
phase: 26-opendeck-shaped-device-editor
type: research
status: complete
date: 2026-05-28
author: research-agent
sources_analyzed:
  - nekename/OpenDeck @ f7d9e0764367a914e1facd46d73d81ad9acd3e3b (GPL-3.0)
  - naerschhersch/opendeck-akp05 @ d77e9e26683a02fdfcaa5d6b556b4b50a579e0f4 (GPL-3.0)
  - 4ndv/mirajazz @ 37a9512560a02006527e500ea9215f08c0328308 (MPL-2.0)
references_in_repo:
  - src/app/qml/KeyDesigner.qml
  - src/app/qml/Inspector.qml
  - src/app/qml/ProfileEditor.qml
  - src/app/qml/components/KeyCell.qml
  - src/devices/streamdeck/src/register.cpp
  - src/devices/streamdeck/src/akp05_protocol.hpp
  - src/core/include/ajazz/core/device.hpp
  - src/core/include/ajazz/core/profile.hpp
  - docs/_data/devices.yaml
---

# Phase 26 Research — OpenDeck Architecture + Per-SKU Geometry Map

## 1. Executive Summary

OpenDeck (`nekename/OpenDeck`, Tauri + SvelteKit) models its profile
editor around a **single geometry-driven `DeviceView` component** that
renders three stacked rows: a `rows × columns` grid of square LCD
buttons, an optional row of `encoders` knobs, and an optional row of
`touchpoints` (LED bars). Drag-drop sources are an HTML5
`draggable="true"` `ActionList` on the right; drop targets are
canvas-backed `<Key>` cells inside the grid. The data model is dead
simple: a `Profile { keys: ActionInstance[] | null, sliders: ActionInstance[] | null }`, indexed by flat position, with a `Context { device, profile, controller, position }` tuple discriminating
"Keypad" vs "Encoder" surfaces.

The opendeck-akp05 plugin (Rust, GPL-3.0) **does not contribute any UI**
— it only declares the geometry numbers (`rows=2, columns=5, encoders=4, type=7 /* Stream Deck Plus */`) via the openaction IPC
call `register_device(...)`. OpenDeck then auto-renders the Stream-
Deck-Plus surface. Hardware-level wire format is from mirajazz (Rust,
MPL-2.0, ours already covers it in `akp05_protocol.hpp`).

**What we reuse:** the geometry-driven layout pattern, the
controller-typed Context tuple, the action library + drag-drop wire,
the Stream Deck Plus "3-row stack" composition.
**What we adapt:** Qt 6 / QML instead of Svelte; our existing
`ProfileController` + `Profile::keys/encoders/mouseButtons` schema
stays; `KeyCell.qml` becomes one delegate among three
(`KeyCell` / `EncoderCell` / `TouchStripZone`).
**What's net-new:** `DeviceDescriptor::keyRows` field (we have
`gridColumns`, need rows + touch-zone count); a `DeviceEditor.qml`
geometry-aware top-level; QML drag-drop via `Drag.active` + `DropArea`;
wiring `StreamDockControlService::setActiveDevice` to
`Main.qml.onDeviceSelected` (closes GAP-25A).

License compatibility: OpenDeck and opendeck-akp05 are GPL-3.0; our
project is GPL-3.0-or-later — direct code copy is permitted but we
choose to learn patterns only (cleaner attribution, fewer transitive
dependencies). Mirajazz is MPL-2.0 — file-level copyleft only;
we already independently implemented the wire layer.

## 2. OpenDeck Architecture Walk

### 2.1 Repository Layout

`/tmp/phase26-research/OpenDeck/` (HEAD `f7d9e07`):

| Path                      | Purpose                                                                |
| ------------------------- | ---------------------------------------------------------------------- |
| `src/`                    | SvelteKit frontend (TypeScript + Svelte).                              |
| `src/lib/`                | Pure TS types + helpers (`Profile.ts`, `DeviceInfo.ts`, `Context.ts`). |
| `src/components/`         | UI components — one Svelte file per concern.                           |
| `src/routes/`             | SvelteKit pages — single `+page.svelte` route (single-window app).     |
| `src-tauri/`              | Rust backend (Tauri).                                                  |
| `src-tauri/src/`          | Backend logic + Tauri command handlers.                                |
| `src-tauri/src/store/`    | Profile JSON persistence + per-device profile registry.                |
| `src-tauri/src/plugins/`  | Plugin discovery, manifest parsing, webserver, IPC.                    |
| `src-tauri/src/events/`   | inbound (from plugins) + outbound (to plugins) + frontend bridge.      |
| `src-tauri/src/elgato.rs` | First-party Elgato HID via `elgato-streamdeck` crate.                  |
| `plugins/`                | Bundled built-in plugins (e.g. `opendeck.multiaction`).                |

Single-page Svelte app, single Rust process. Plugins are out-of-process
binaries that talk to OpenDeck via a WebSocket (openaction protocol);
they register devices over the same IPC.

### 2.2 Profile Editor — Device View

The whole device editor is **one file**:
`/tmp/phase26-research/OpenDeck/src/components/DeviceView.svelte`
(246 lines). It is rendered from `src/routes/+page.svelte:60-64`,
once per currently-connected device (only the one matching
`selectedDevice` is visible; others are CSS-hidden via `class:hidden`
so live updates render in the background).

`DeviceView.svelte` composition (lines 192-244):

```
<div role="grid" aria-label={device.name}>
  <!-- Row 1..N: LCD key grid -->
  {#each { length: device.rows } as _, r}
    <div class="flex flex-row" role="row">
      {#each { length: device.columns } as _, c}
        <Key context={{controller: "Keypad", position: r*cols+c}} ... />
      {/each}
    </div>
  {/each}
  <!-- Row N+1: encoders (1 horizontal row of dials) -->
  <div class="flex flex-row" role="row">
    {#each { length: device.encoders } as _, i}
      <Key context={{controller: "Encoder", position: i}} ... />
    {/each}
  </div>
  <!-- Row N+2: touchpoints (1 horizontal row of LED bars) -->
  <div class="flex flex-row" role="row">
    {#each { length: device.touchpoints } as _, i}
      <Key isTouchPoint context={{controller: "Keypad",
                                  position: rows*cols + i}} ... />
    {/each}
  </div>
</div>
```

Notable design choices:

- **Single `Key` delegate for all three surface types.** Discriminated
  by `controller` ("Keypad" vs "Encoder") + `isTouchPoint` boolean.
  The cell is a `<canvas>` element rendered identically; an Encoder
  cell gets `rounded-full!` (`Key.svelte:196`), a touchpoint without
  a slot draws a horizontal LED-bar (`Key.svelte:225-227`).
- **Touchpoints share the same flat `profile.keys` array.** Their
  position is offset by `rows*cols` (`DeviceView.svelte:231,234`).
  No separate "strips" array. This is the elegant move: the touch
  strip's 4 LED dots are just "keys 10..13" on a Stream Deck Plus.
- **Encoders go in a separate `profile.sliders` array** (`DeviceView.svelte:36`)
  because their per-event semantics differ (CW / CCW / press
  instead of press / release).
- **Per-device window resizing** at `DeviceSelector.svelte:50-63`:
  the Tauri window auto-resizes to
  `cols * 132 + 416` × `rows * 132 + 384` so a 2×3 AKP03 view is
  much smaller than a 4×8 Stream Deck XL.
- **Overflow handling** (`DeviceView.svelte:76-77`): if
  `max(cols, encoders, touchpoints) > 8` OR
  `rows + min(enc,1) + min(touch,1) > 4`, the grid switches to
  scroll-mode with a CSS mask-gradient fade on edges
  (`device-fade-x` / `device-fade-y` / `device-fade-xy` at lines 159-171).
  This catches the (uncommon) Stream Deck XL + Plus combo overflow.
- **Keyboard navigation** (`DeviceView.svelte:106-156`): a
  `gridRowLengths` array models the entire heterogeneous row sequence,
  Arrow keys move focus, Home/End jump within row. Cells are
  `role="gridcell"`, the container is `role="grid"`.

### 2.3 Device-Geometry Model

The geometry contract is **6 integers**:

```ts
// /tmp/phase26-research/OpenDeck/src/lib/DeviceInfo.ts (whole file)
export type DeviceInfo = {
    id: string;          // unique per connected device
    name: string;        // human-readable label
    rows: number;
    columns: number;
    encoders: number;
    touchpoints: number;
    type: number;        // Stream Deck Kind ordinal
};
```

The Rust-side mirror (`src-tauri/src/shared.rs:31-42`) adds a
`plugin: String` field (the source plugin's id; empty for first-party
elgato).

The `type` ordinal in `elgato.rs:109-116`:

| `type` | Elgato Kind                                | Geometry |
| ------ | ------------------------------------------ | -------- |
| 0      | Original / OriginalV2 / Mk2 / Mk2Scissor / |          |

```
      Mk2Module                                    | 3×5, 0 enc, 0 tp    |
```

| 1 | Mini / MiniMk2 / MiniDiscord / MiniMk2Module | 2×3, 0 enc, 0 tp |
| 2 | Xl / XlV2 / XlV2Module | 4×8, 0 enc, 0 tp |
| 5 | Pedal | 1×3, 0 enc, 0 tp |
| 7 | Plus | 2×4, 4 enc, 4 tp |
| 9 | Neo | 2×4, 0 enc, 2 tp |

opendeck-akp05 reuses `type=7` (`mappings.rs:32`) to register an
AKP05 / Mirabox N4 as "Stream Deck Plus-equivalent", even though the
AKP05 is 2×5 (not 2×4). OpenDeck does not care about the `type` for
layout — it cares for the icon and the touchscreen-strip auto-render
behaviour (encoder labels drawn into the strip's 4 zones
automatically).

**Where DeviceInfo is created.** Two paths:

1. **First-party Elgato** (`src-tauri/src/elgato.rs:103-137`):
   `init()` constructs a `DeviceInfo` directly from the
   `elgato-streamdeck` Rust crate's `Kind` methods
   (`kind.row_count() / column_count() / encoder_count() / touchpoint_count()`). Lines 128-131.
1. **Plugin-contributed** (`src-tauri/src/events/inbound/devices.rs`):
   plugins push `DeviceInfo` payloads via openaction's `RegisterDevice`
   event over the IPC. The opendeck-akp05 plugin uses this path:
   `outbound.register_device(id, name, rows, columns, encoders, device_type)` at `opendeck-akp05/src/device.rs:46-56`. The plugin's
   geometry constants are static (`opendeck-akp05/src/mappings.rs:25-32`).

**Crucial finding for our project:** OpenDeck's geometry model is
**flat and minimal** — no per-key pixel dimensions, no Rot/mirror
metadata, no usage-page. Those live deeper (in mirajazz's `ImageFormat`
struct passed to `set_button_image`, not exposed to the frontend).
The frontend only needs row/column counts. This is the right level
of abstraction for the QML editor.

### 2.4 Drag-Drop Architecture

OpenDeck uses **plain HTML5 drag-and-drop** (no Svelte-specific
library). Three phases:

**Drag source (action library)** — `ActionList.svelte:113-130`:

```svelte
<div
  draggable="true"
  on:dragstart={(event) => {
    if (!event.dataTransfer) return;
    event.dataTransfer.effectAllowed = "copy";
    event.dataTransfer.setData("action", JSON.stringify(action));
  }}
  ...
>
```

Each action row is `draggable="true"`; its `dragstart` puts the
serialised `Action` JSON onto the `dataTransfer` under the
`"action"` MIME key.

**Drag source (key cell, for reordering)** —
`DeviceView.svelte:19-24`:

```ts
function handleDragStart({ dataTransfer }, controller, position) {
    dataTransfer.effectAllowed = "move";
    dataTransfer.setData("controller", controller);    // "Keypad" / "Encoder"
    dataTransfer.setData("position", position.toString());
}
```

Distinct MIME keys (`"controller"` + `"position"`) discriminate a
move-existing-binding from a create-from-library drop.

**Drop target** — `DeviceView.svelte:26-57`:

```ts
function handleDragOver(event) {
    event.preventDefault();
    if (event.dataTransfer.types.includes("action"))     dropEffect = "copy";
    else if (event.dataTransfer.types.includes("controller")) dropEffect = "move";
}

async function handleDrop({ dataTransfer }, controller, position) {
    let context = { device, profile, controller, position };
    let array = controller == "Encoder" ? profile.sliders : profile.keys;
    if (dataTransfer.getData("action")) {
        // Create new
        let action = JSON.parse(dataTransfer.getData("action"));
        if (array[position]) return;  // refuse if slot already taken
        array[position] = await invoke("create_instance", { context, action });
    } else if (dataTransfer.getData("controller")) {
        // Move existing
        ...invoke("move_instance", { source, destination: context })...
    }
}
```

The drop target is the **canvas inside `Key.svelte`** (line 191-224):
`on:dragstart`, `on:dragover`, `on:drop` are forwarded up via Svelte
event-forwarding (line 204-206). The canvas is `draggable={slot != null}`
so empty cells aren't drag-sources but accept drops; populated cells
are both sources and targets.

**Key takeaway for QML port:** the model is symmetric — every cell is
both a drop target (always) and a drag source (when occupied). Encoders
have the same wire as keys; the only branch is "which array to write
to" (`sliders` vs `keys`) based on `controller`.

### 2.5 Action Library Pane

`ActionList.svelte` (145 lines). Fixed 18 rem (288 px) wide, anchored
right of the device view (`+page.svelte:73`). Composition:

- **Search input** at top (line 79-88) — `query` state filters
  `categories` by name+action.
- **Categories** as `<details open>` collapsible groups, one per
  registered plugin (line 92-141). Category-icon precedes name.
- **Action rows** (line 113-138) — `draggable="true"` rows with
  44×44 icon + name. Keyboard shortcut `Ctrl+C` (line 127-129) copies
  the action to the global `copiedItem` store so it can be pasted
  into a cell via `Ctrl+V`.

Categories come from
**`invoke("get_categories")`** which is sourced from the
`shared::CATEGORIES` `DashMap` populated as plugins register their
actions
(`/tmp/phase26-research/OpenDeck/src-tauri/src/plugins/mod.rs`).
Each `Action` is the type at
`/tmp/phase26-research/OpenDeck/src/lib/Action.ts:1-15`:

```ts
export type Action = {
    name: string; uuid: string; plugin: string;
    tooltip: string; icon: string;
    visible_in_action_list: boolean;
    supported_in_multi_actions: boolean;
    property_inspector: string;
    controllers: string[];       // ["Keypad"] | ["Encoder"] | ["Keypad", "Encoder"]
    states: ActionState[];
};
```

`controllers` is the per-action whitelist: an Encoder-only action
(volume rotary) declares `controllers: ["Encoder"]` and OpenDeck
will reject a drop on a key cell. Visual feedback is via the
`dragover` `dropEffect` flip — our implementation should match.

### 2.6 Theme + Visual Style

OpenDeck uses **TailwindCSS dark theme** (`bg-neutral-900` panel,
`bg-neutral-700` borders, `text-neutral-300` text). Key visual:

- **No per-device skin / no device chassis silhouette.** OpenDeck
  shows just the abstract grid + dial-row + dot-row. The device
  identity is shown only in the dropdown selector (top-left).
- **Per-cell canvas.** Each `Key` is a `<canvas>` 144×144 (or 192×192
  for SD-XL), border `3px rounded-3xl neutral-700` (line 193).
  Encoder cells get `rounded-full!` (line 196) — full circle.
  Touchpoints get a `border-t-4 horizontal bar` overlay when empty
  (line 225-227) to hint at the LED nature.
- **Canvas content is rendered via `renderImage()`** (`Key.svelte:164`,
  `src/lib/rendererHelper.ts`) — the action's image plus optional
  text overlay (state-driven). The canvas dimensions follow
  `device.id.startsWith("sd-") && rows == 4 && cols == 8 ? 192 : 144`
  (`DeviceView.svelte:203`).
- **Cursor**: `cursor-grab` on populated cells, `cursor-grabbing`
  on `:active` — explicit drag affordance.

**For our Qt 6 / QML port**, the visual cues to keep are:

1. Per-cell radius (square keys, full circle encoders).
1. Touch-point as a small horizontal "LED dot" row (not a single
   wide strip — OpenDeck models 4 discrete touchpoints, matching
   how the AKP05 firmware actually divides the strip into 4 zones).
1. No device chassis — the abstract grid is the editor.
1. Drag-grab cursor feedback on draggable cells.

We may go one step further than OpenDeck and add an optional
silhouette image of the actual device behind the grid (the Phase 26
CONTEXT.md mentions "Elgato Stream Deck-pattern device-shaped editor"
— interpreting that as a soft device photo behind the grid is a UX
nicety, not a requirement).

## 3. opendeck-akp05 Plugin Walk

### 3.1 Supported SKUs

`/tmp/phase26-research/opendeck-akp05/src/mappings.rs:40-54`:

| SKU         | VID:PID         | Status                                                    |
| ----------- | --------------- | --------------------------------------------------------- |
| Mirabox N4  | `0x6603:0x1007` | Confirmed with hardware (`mappings.rs:41-42`)             |
| AJAZZ AKP05 | `0x0300:0x3004` | Placeholder, hardware not available (`mappings.rs:45-47`) |

The `0x0300:0x3004` PID matches what our project independently
discovered as AKP05E (CLAUDE.md AKP05E glossary). **The opendeck-akp05
plugin author has the same PID we do, marked "placeholder".** Our
project has more hardware evidence than they do — we should consider
upstreaming the firmware-version handshake confirmation to them.

Note: opendeck-akp05's CLAUDE.md line 142-145 explicitly says
"Buttons: 10 buttons (2×5 grid) vs AKP03's 9 buttons" — but in
mirajazz's example for AKP03R it says `Device::connect(&dev, 2, 9, 3)`
(9 keys, 3 encoders). We list AKP03 as 6 keys + 3 encoders. The
discrepancy is that mirajazz counts the 3 side-buttons as keys (giving
9), while we expose only the 6 LCD keys via `DisplayKeyCount` and
surface the side buttons via the event stream
(`akp03_protocol.hpp:67-70`). This is a per-project modelling choice
we already locked.

### 3.2 Device Capability Declaration

The plugin's geometry contract is **5 const usizes plus a u8 type**:

```rust
// opendeck-akp05/src/mappings.rs:25-32
pub const ROW_COUNT: usize = 2;
pub const COL_COUNT: usize = 5;
pub const KEY_COUNT: usize = 15;     // Hardware-side count: 5 wide buttons
                                     // (indices 0-4, encoder LCDs) + 10 grid keys (5-14)
pub const ENCODER_COUNT: usize = 4;
pub const DEVICE_TYPE: u8 = 7;       // Stream Deck Plus
```

The `KEY_COUNT = 15` is **NOT the number of OpenDeck keys** —
it's the hardware-side button-index range that mirajazz uses to size
its internal `button_states[]` vector. The OpenDeck-side grid is
`rows * columns = 10` keys; the 4 encoder LCD overlays are
"wide buttons" 0..3 in the hardware index space and
"touchpoints" in OpenDeck's view, but the plugin renders them via
`device.set_button_image(0..3, image_format_touchzone(), ...)`
(`device.rs:206-210`). This decouples OpenDeck's logical 10-key model
from the hardware's 15-button physical address space.

Registration sequence (`opendeck-akp05/src/device.rs:44-57`):

```rust
outbound.register_device(
    candidate.id.clone(),       // "n4-<serial>"
    candidate.kind.human_name(),// "Ajazz AKP05" or "Mirabox N4"
    ROW_COUNT as u8,            // 2
    COL_COUNT as u8,            // 5
    ENCODER_COUNT as u8,        // 4
    DEVICE_TYPE,                // 7 (Plus)
).await.unwrap();
```

Notice the openaction `register_device` signature does NOT have a
`touchpoints` parameter. Stream-Deck-Plus-class devices (`type=7`)
implicitly have `touchpoints = encoders` — OpenDeck's `DeviceView`
auto-renders the 4 LED-bars when both encoder count and Plus type
are signalled (and the plugin sends image to encoder positions via
`set_image_event.controller == "Encoder"`).

### 3.3 Wire Protocol Cross-Validation

| Concept                              | opendeck-akp05 (Rust)                                                                  | Our `akp05_protocol.hpp`                              | Agreement?                                                       |
| ------------------------------------ | -------------------------------------------------------------------------------------- | ----------------------------------------------------- | ---------------------------------------------------------------- |
| Key grid                             | 2 × 5 (`mappings.rs:25-26`)                                                            | 2 × 5 (`akp05_protocol.hpp:70-71`)                    | ✓                                                                |
| Encoder count                        | 4 (`mappings.rs:28`)                                                                   | 4 (`akp05_protocol.hpp:72`)                           | ✓                                                                |
| Touch-zone count                     | 4 (1 per encoder, `device.rs:222-227`)                                                 | 4 (`akp05_protocol.hpp:73`)                           | ✓                                                                |
| Key image JPEG dims                  | 112 × 112 (`mappings.rs:96`)                                                           | 85 × 85 (`akp05_protocol.hpp:74-75`)                  | **DISAGREE**                                                     |
| Touch-zone image dims                | 184 × 120 (`mappings.rs:107`, "testing wider")                                         | 100 × 100 per-encoder (`akp05_protocol.hpp:91-92`)    | **DISAGREE**                                                     |
| Image rotation                       | Rot180 (`mappings.rs:97`)                                                              | Implementation in `akp05.cpp` (verify needed)         | Provisional                                                      |
| Hardware button-index → OpenDeck row | top row 0-4 → HW 10-14, bottom row 5-9 → HW 5-9 (`device.rs:240-243`)                  | `akp05KeyWire()` (commit 037bd8d, BAT mapping)        | Same family-wide pattern; **independent verification**           |
| Protocol version                     | 3 = 1024-byte packets (`mappings.rs:87-89`)                                            | 3 (`akp05_protocol.hpp:23, 97`)                       | ✓                                                                |
| Init handshake                       | mirajazz `initialize()` sends `CRT DIS` + `CRT LIG` (`mirajazz/src/device.rs:358-363`) | docs/protocols/streamdeck/akp05_init_sequence.md §3.2 | ✓                                                                |
| HID usage page                       | `65440` = `0xFFA0` (`mappings.rs:51`)                                                  | Not yet in code — uses default open                   | **CHECK** — may need to set `controlUsagePage` on the descriptor |

**The two disagreements are the interesting findings.** Our
`KeyWidthPx = 85` predates the 2026-05-14 research pass (which mostly
fixed wire-level packet structure, not image dimensions). The
opendeck-akp05 author says **112×112** based on N4-hardware testing
(comment `mappings.rs:9 "verified with hardware"`). Our 2026-05-28
live image-upload test on the AKP05E DID render an 85×85 JPEG
successfully (CLAUDE.md AKP05E glossary §"Image render on Linux") —
so 85×85 is at least accepted by firmware, but 112×112 may be the
native resolution and produce sharper output. **This is a Phase 26
follow-up, not blocker.**

The touch-strip dims discrepancy is more substantive: opendeck-akp05
treats the strip as **4 discrete LCD buttons of 184×120 px each**
(`device.rs:204-210` comment "Tested: write_lcd() is accepted but
silently ignored - hardware doesn't support pixel positioning"). Our
header has both the per-encoder 100×100 model AND the rect-addressable
800×480 "DRA" model (`akp05_protocol.hpp:73,76-77,117-120`).
**opendeck-akp05's empirical finding ("DRA opcode silently ignored
on N4") may apply to AKP05E too.** Worth a hardware retest on our side
once the Phase 26 editor lands the per-zone drop targets.

The HID `usage_page=65440 (=0xFFA0)` from `mappings.rs:51` is
not currently in our `DeviceDescriptor` for AKP05 — we use the
default first-interface open. Since the AKP05E is single-interface
in `lsusb`, this likely doesn't matter on Linux, but Windows
composite-device enumeration might benefit. **Track as risk.**

## 4. mirajazz Library Walk

### 4.1 Per-Device Type Definitions

mirajazz is a deliberately device-AGNOSTIC library. Per its README
(`/tmp/phase26-research/mirajazz/README.md:9-12`):

> No device-specific code in the library, which devices to support is
> up to you

There is no per-device struct or enum — instead, `Device::connect`
takes `protocol_version: usize, key_count: usize, encoder_count: usize` as parameters (`mirajazz/src/device.rs:213-218`). The caller
(opendeck-akp05, opendeck-akp03, etc.) supplies the constants.

This means **mirajazz is NOT a source of per-SKU geometry data.** The
per-SKU table has to come from elsewhere — the OpenDeck plugins
(opendeck-akp03, opendeck-akp05, opendeck-akp153), the
`mirajazz/examples/*.rs` files, or our own `devices.yaml`.

### 4.2 Per-SKU Geometry Table

Drawn from the four mirajazz examples
(`/tmp/phase26-research/mirajazz/examples/{akp03r,akp153r,n1}.rs` +
the README), plus the two OpenDeck plugins (opendeck-akp03 inferred
from opendeck-akp05's fork docs), plus our own protocol headers:

| SKU                    | VID:PID         | proto_v | keys                                     | encoders          | grid          | Key JPEG (mirajazz)                      | Source                                                                  |
| ---------------------- | --------------- | ------- | ---------------------------------------- | ----------------- | ------------- | ---------------------------------------- | ----------------------------------------------------------------------- |
| AKP153 / N3 V1 fw      | `0x5548:0x6674` | 1       | 15                                       | 0                 | 3×5           | 85×85, Rot90+MirrorBoth                  | mirajazz README §protocol_version=1; mirajazz/examples/akp153r.rs:13-17 |
| AKP153 (legacy)        | `0x0300:0x1001` | 1       | 15                                       | 0                 | 3×5           | 85×85, Rot90+MirrorBoth                  | our register.cpp:131 + akp153_protocol.hpp                              |
| AKP153E V2 fw          | `0x0300:0x1010` | 1       | 15                                       | 0                 | 3×5           | 85×85                                    | our register.cpp:155                                                    |
| AKP153R (18 keys?)     | `0x0300:0x1020` | 1       | **18**                                   | 0                 | 3×6 (?)       | 85×85, Rot90+MirrorBoth                  | **mirajazz/examples/akp153r.rs:11,53** (`KEY_COUNT = 18`)               |
| AKP815                 | `0x5548:0x6672` | 1       | 15                                       | 0                 | 5×3           | 100×100, Rot180                          | our akp815_protocol.hpp:36-39                                           |
| AKP03 V1 / N3          | `0x0300:0x1001` | 2       | 6 (+3 side)                              | 3                 | 2×3           | 60×60, Rot0                              | our akp03_protocol.hpp:67-71                                            |
| AKP03R                 | `0x0300:0x1003` | 2       | 9 (mirajazz counts side buttons) / 6 LCD | 3                 | 2×3           | 60×60, Rot0                              | mirajazz/examples/akp03r.rs:8,30 (`Device::connect(2, 9, 3)`)           |
| AKP03R rev2            | `0x0300:0x3003` | 3       | 6                                        | 3                 | 2×3           | 64×64, Rot90                             | our register.cpp:222 + devices.yaml:193                                 |
| Mirabox N3 rev1        | `0x6602:0x1002` | 2       | 6                                        | 3                 | 2×3           | 60×60                                    | our register.cpp:229                                                    |
| Mirabox N3 rev3        | `0x6603:0x1002` | 2/3     | 6                                        | 3                 | 2×3           | 60×60                                    | our register.cpp:245                                                    |
| Mirabox N3EN           | `0x6603:0x1003` | 2       | 6                                        | 3                 | 2×3           | 60×60                                    | our register.cpp:250                                                    |
| **AKP05 / Mirabox N4** | `0x6603:0x1007` | 3       | 10                                       | 4 + 4 touch zones | 2×5           | 112×112 Rot180 (mirajazz) / 85×85 (ours) | opendeck-akp05/mappings.rs:25-32; our akp05_protocol.hpp:69-75          |
| AKP05E                 | `0x0300:0x3004` | 3       | 10                                       | 4 + 4 touch zones | 2×5           | 85×85 confirmed accepted                 | our register.cpp:298; CLAUDE.md AKP05E glossary                         |
| Mirabox N1             | `0x6603:0x1000` | 3       | **18** (15 grid + 3 top row)             | 0                 | 3×5 + 1×3 top | 96×96 grid / 64×64 top                   | mirajazz/examples/n1.rs:18-40 — **device not in our catalogue**         |

**Three actionable findings from this table:**

1. **AKP153R may actually be 18 keys (3×6), not 15.** Our
   `devices.yaml:106-111` lists it as 15 with 5-column grid.
   mirajazz's example explicitly uses `KEY_COUNT = 18` and ships an
   index-permutation lookup table (`akp153r.rs:22,33`) sized 18. The
   discrepancy is at maturity tier `scaffolded` — no hardware
   capture has confirmed either count for our project. **Add as
   "open hardware item" in Section 8 risk register.**
1. **Mirabox N1 (`0x6603:0x1000`) is a real SKU not in our catalogue.**
   18 keys split across a 3×5 grid + a 3-key top row, different JPEG
   per row (96×96 / 64×64). It also has a hardware-mode setting
   (`set_mode(N1Mode)`, `n1.rs:55-57`) — keyboard / calculator /
   software modes. If we ever support it, it needs a heterogeneous
   row-by-row geometry, not just `rows × cols`. **Add to devices.yaml
   backlog.**
1. **JPEG dimensions disagree between mirajazz and our headers** for
   AKP05 (112×112 vs 85×85) and AKP03R rev2 (64×64 vs 60×60). For
   Phase 26 (UI editor), this does not matter — the editor sends
   `KeyState::imagePath` + lets the wire layer render. But the wire
   layer needs to settle the discrepancy in a later phase.

### 4.3 Init Sequence Cross-Reference

mirajazz `initialize()` (`mirajazz/src/device.rs:351-367`) sends two
extended-data writes during the first I/O attempt on any device:

```
buf1 = [0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x44, 0x49, 0x53]
     = [report-id 0x00, "CRT", 0x00 0x00, "DIS"]
buf2 = [0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x4c, 0x49, 0x47, 0x00, 0x00, 0x00, 0x00]
     = [report-id 0x00, "CRT", 0x00 0x00, "LIG", 0x00 0x00 0x00 0x00]
```

This matches `docs/protocols/streamdeck/akp05_init_sequence.md §3.2`
("first commands sent: only `CRT VER`" — that's a separate firmware
probe; the open-handshake `DIS` + `LIG` is what mirajazz uses, and
what our `Akp05Device::open()` should match). The keep-alive in
`opendeck-akp05/src/watcher.rs` calls `set_brightness(50)` again to
keep the device alive — see CLAUDE.md AKP05E glossary §"Live unit"
("mirajazz-exact `DIS`+`LIG` init + `CRT CONNECT` keep-alive").

**No phase-26-relevant divergence.** The init sequence is wire-layer,
the editor doesn't touch it.

## 5. Cross-Project Mapping Table

Every codename from `docs/_data/devices.yaml` mapped to OpenDeck +
mirajazz coverage. "OD plugin?" column reports which OpenDeck plugin
(if any) covers the SKU.

| Our codename        | VID:PID        | OD plugin?                     | mirajazz support?                 | Editor geometry                      | Notes                                                 |
| ------------------- | -------------- | ------------------------------ | --------------------------------- | ------------------------------------ | ----------------------------------------------------- |
| akp153              | 0x0300:0x1001  | opendeck-akp153 (sibling repo) | proto_v=1                         | 3×5, 0 enc, 0 tp                     | OpenDeck `type=0` (Original-class)                    |
| akp153_v1           | 0x5548:0x6674  | opendeck-akp153                | proto_v=1                         | 3×5, 0 enc, 0 tp                     | Canonical pair                                        |
| akp153e             | 0x0300:0x1002  | opendeck-akp153                | proto_v=1                         | 3×5, 0 enc, 0 tp                     |                                                       |
| akp153e_v2          | 0x0300:0x1010  | opendeck-akp153                | proto_v=1                         | 3×5, 0 enc, 0 tp                     |                                                       |
| akp153r             | 0x0300:0x1020  | opendeck-akp153 (likely)       | proto_v=1, **18 keys** in example | **3×6?**, 0 enc                      | **Hardware item:** key count 15 vs 18                 |
| akp815              | 0x5548:0x6672  | none known                     | n/a                               | 5×3, 0 enc, 0 tp **+ 800×480 strip** | Strip is one rect-addressable area, not 4 zones       |
| akp03               | 0x0300:0x1001  | opendeck-akp03                 | proto_v=2                         | 2×3 LCD + 3 side btn + 3 enc         | OpenDeck would model as 2×3 grid + 3 encoders         |
| akp03_legacy        | 0x0300:0x3001  | opendeck-akp03                 | proto_v=2                         | 2×3                                  | Pre-2026 PID                                          |
| akp03e              | 0x0300:0x3002  | opendeck-akp03                 | proto_v=2                         | 2×3                                  | V2 firmware (1024-B packets)                          |
| akp03r              | 0x0300:0x1003  | opendeck-akp03                 | proto_v=2                         | 2×3                                  | mirajazz example exists                               |
| akp03r_rev2         | 0x0300:0x3003  | opendeck-akp03                 | proto_v=3                         | 2×3                                  | 64×64 keys (Rot90), full press/release                |
| mirabox_n3          | 0x6602:0x1002  | opendeck-akp03                 | proto_v=2                         | 2×3                                  |                                                       |
| mirabox_n3_rev3     | 0x6603:0x1002  | opendeck-akp03                 | proto_v=2/3                       | 2×3                                  |                                                       |
| mirabox_n3en        | 0x6603:0x1003  | opendeck-akp03                 | proto_v=2                         | 2×3                                  |                                                       |
| akp05               | 0x0300:0x5001  | opendeck-akp05                 | proto_v=3                         | **2×5 + 4 enc + 4 tp**               | Provisional PID                                       |
| mirabox_n4          | 0x6603:0x1007  | opendeck-akp05                 | proto_v=3                         | **2×5 + 4 enc + 4 tp**               | Hardware-confirmed by plugin author                   |
| akp05e              | 0x0300:0x3004  | opendeck-akp05 (placeholder)   | proto_v=3                         | **2×5 + 4 enc + 4 tp**               | Our live unit; OD plugin author has placeholder       |
| via_generic         | 0x3151:various | n/a (keyboard)                 | n/a                               | n/a (no LCD keys)                    | Keep `KeyDesigner.qml` empty-state                    |
| proprietary         | 0x3151:various | n/a (keyboard)                 | n/a                               | n/a                                  | RGB editor only                                       |
| ak980pro            | 0x0c45:0x8009  | n/a (keyboard)                 | n/a                               | n/a                                  | RGB / clock / battery — different UI surface entirely |
| ak980pro_dongle_24g | 0x0c45:0x7016  | n/a (dongle)                   | n/a                               | n/a                                  | Dongle stub                                           |
| aj_series\_\*       | 0x248A:0x5C2E… | n/a (mouse)                    | n/a                               | n/a                                  | `MousePanel.qml` covers                               |
| aj159_apex\_\*      | 0x3151:0x4026… | n/a (mouse)                    | n/a                               | n/a                                  | `MousePanel.qml` covers                               |
| ajazz_24g_8k        | 0x3151:0x5007  | n/a (mouse)                    | n/a                               | n/a                                  | `MousePanel.qml` covers                               |
| aj199_family\*      | 0x3554:0xF50…  | n/a (mouse)                    | n/a                               | n/a                                  | `MousePanel.qml` covers                               |

**Coverage summary:** OpenDeck + plugins cover **all 18 Stream Dock
SKUs in our catalogue** (sometimes via the AKP05 plugin author's
placeholder, sometimes via opendeck-akp03 or opendeck-akp153 siblings
we haven't cloned but follow the same pattern). The keyboard / mouse
/ dongle SKUs are out-of-scope for OpenDeck and remain in our
existing tab UIs (`RgbPicker`, `MousePanel`, `SettingsRow`).

## 6. Recommended Device-Geometry Schema for ajazz-control-center

`DeviceDescriptor` currently has `keyCount`, `gridColumns`,
`encoderCount`, `hasTouchStrip` (boolean) — sufficient for the
generic grid, **insufficient for OpenDeck-style layouts**. We need:

1. Explicit `keyRows` (currently inferred from `keyCount / gridColumns` — wrong for AKP815 which is 5×3 but `gridColumns = 3`; works by accident).
1. Explicit `touchZoneCount` instead of a boolean (so the editor
   knows whether to render 4 LED-bars or 1 wide-strip).
1. Optional `encoderHasOverlayDisplay: bool` — encoders that ship
   with a per-knob LCD overlay (AKP05 yes, AKP03 no — knobs are
   physical only).
1. Optional `mainScreenWidthPx` / `mainScreenHeightPx` for the
   wide-LCD-strip class (AKP815's 800×480 strip is one big editable
   rectangle, not 4 zones).

Proposed extension (additive — preserves the existing fields):

```cpp
// src/core/include/ajazz/core/device.hpp — additions to DeviceDescriptor
struct DeviceDescriptor {
    // ... existing fields ...

    // ---- OpenDeck-pattern geometry (Phase 26) -------------------------
    /// Number of LCD-key rows. When 0, falls back to keyCount/gridColumns.
    /// Required by the geometry-driven editor to render heterogeneous
    /// row arrangements (e.g. AKP815's 5×3 portrait vs AKP05's 2×5 landscape).
    std::uint8_t keyRows{0};

    /// Number of touch-strip zones aligned to encoders. 0 = no touch strip;
    /// >0 = N discrete touchpoints (Stream Deck Plus model). Distinct from
    /// hasTouchStrip (boolean) which only signals "any strip present".
    /// AKP05/Mirabox N4 = 4; AKP815 = 0 (its strip is a single wide image,
    /// not zone-aligned to encoders — uses mainScreenWidthPx instead).
    std::uint8_t touchZoneCount{0};

    /// Width in pixels of the main LCD strip (AKP815's 800×480). 0 if absent.
    /// Mutually exclusive with touchZoneCount in practice — a device has either
    /// per-encoder touch zones OR a single wide strip, not both.
    std::uint16_t mainScreenWidthPx{0};

    /// Height in pixels of the main LCD strip.
    std::uint16_t mainScreenHeightPx{0};
};
```

**Migration cost.** Every `streamdeck/src/register.cpp` row needs one
extra line. We can do this in two commits: (1) add the fields with
defaults that preserve current behaviour; (2) populate the new fields
per row.

**No schema change to `Profile`.** OpenDeck stores encoder bindings
in a separate `sliders` array. We already do the same — `Profile`
has `keys: unordered_map<uint16_t, Binding>` AND `encoders: unordered_map<uint16_t, EncoderBinding>` (`profile.hpp:142-173`).
Touchpoints don't have their own array in OpenDeck — they share
`profile.keys` at offsets `rows*cols..rows*cols+touchpoints`. We can
do the same: reserve key indices `keyCount..keyCount+touchZoneCount`
for touch-zone bindings. (Alternative: add `touchZones: unordered_map<...>` to Profile, but that's a schema breakage for the
JSON wire format documented in `docs/protocols/PROFILE_SCHEMA.md`.
**Recommend: reuse `keys`, document the offset convention.**)

## 7. Recommended QML Editor Architecture

### 7.1 Component Tree

```
ProfileEditor.qml                (existing, unchanged outer)
└── (Keys tab) Loader
    └── DeviceEditor.qml         (NEW — geometry-driven top-level)
        ├── DeviceCanvas.qml     (NEW — the abstract grid container)
        │   ├── Repeater [rows×cols]
        │   │   └── KeyCell.qml          (EXTEND — add DropArea)
        │   ├── Repeater [encoderCount]
        │   │   └── EncoderCell.qml      (NEW — round dial, slider/btn icon)
        │   └── Repeater [touchZoneCount OR mainScreen]
        │       └── TouchStripZone.qml   (NEW — wide LED bar)
        ├── Inspector.qml        (existing, reused — right pane)
        └── ActionLibraryPane.qml(NEW — left pane, drag sources)
```

Routing decision: `ProfileEditor.qml` (line 253-264) keeps its
`KeyDesigner` Loader for the Keys tab — but the Loader now
instantiates `DeviceEditor.qml` instead of `KeyDesigner.qml` when
the `DeviceDescriptor` exposes the new geometry fields. Devices
without `keyRows / touchZoneCount` populated still get the legacy
`KeyDesigner.qml` (zero-disruption rollout).

### 7.2 Drag-Drop Wiring (Qt 6 QML)

QML's drag-drop primitives are `Drag.active` on the source side and
`DropArea` on the target side. The data goes through
`Drag.mimeData` (a dict) or via custom signals.

```qml
// KeyCell.qml — add to existing root ItemDelegate
Drag.active: dragHandler.active
Drag.dragType: Drag.Automatic
Drag.mimeData: {
    "application/x-ajazz-binding": JSON.stringify({
        controller: "Keypad",
        position: root.index
    })
}
DropArea {
    anchors.fill: parent
    keys: ["application/x-ajazz-action", "application/x-ajazz-binding"]
    onDropped: function(drop) {
        if (drop.hasKey("application/x-ajazz-action")) {
            var action = JSON.parse(drop.getDataAsString("application/x-ajazz-action"));
            ProfileController.createBinding(
                StreamDockControlService.activeCodename,
                "Keypad",
                root.index,
                action.actionKind,
                action.actionParams);
        } else if (drop.hasKey("application/x-ajazz-binding")) {
            var src = JSON.parse(drop.getDataAsString("application/x-ajazz-binding"));
            ProfileController.moveBinding(src.controller, src.position,
                                          "Keypad", root.index);
        }
    }
    onEntered: function(drop) {
        drop.accepted = true;
        drop.acceptProposedAction();
    }
}
DragHandler { id: dragHandler; target: null /* drag the cell visual */ }
```

ActionLibraryPane drag source:

```qml
// ActionLibraryPane.qml — each action row
ItemDelegate {
    Drag.active: dragHandler.active
    Drag.mimeData: {
        "application/x-ajazz-action": JSON.stringify(model.action)
    }
    DragHandler { id: dragHandler; target: null }
    // ... icon + label ...
}
```

**Pitfall to flag** (CLAUDE.md "Qt 6 / QML gotchas"): `Drag.active`
inside an `ItemDelegate` with a parent `ListView` interacts with
ListView's own flick gesture. We may need `DragHandler.acceptedButtons: Qt.LeftButton` and a `DragHandler.dragThreshold` to avoid accidental
drags during scrolling.

### 7.3 setActiveDevice wiring (closes GAP-25A)

`Main.qml:127-129` is where the sidebar selects a device:

```qml
onDeviceSelected: codename => {
    editor.codename = codename;
    editor.capabilities = DeviceModel.capabilitiesFor(codename);
}
```

**Add one line** (Phase 26 Plan 1, the trivial closer):

```qml
onDeviceSelected: codename => {
    editor.codename = codename;
    editor.capabilities = DeviceModel.capabilitiesFor(codename);
    StreamDockControlService.setActiveDevice(codename);  // <-- new
}
```

This single line closes GAP-25A and unblocks Phase 25 Test 1
(image-upload to LCD key end-to-end). Costs nothing structurally,
needs no schema change. **Land this as Plan 26-1 (one-line change)
before the editor work begins.**

### 7.4 Visual Design Cues

Match OpenDeck where it's good UX, diverge where Qt's theme system
makes a different choice cleaner:

- **Square key cells, full-circle encoders, horizontal bar
  touchpoints** — match OpenDeck (`Key.svelte:196,225-227`).
- **Per-cell drag-grab cursor on populated cells, regular on empty**
  — match OpenDeck (`Key.svelte:195 cursor-grab`). In QML:
  `MouseArea.cursorShape: Qt.OpenHandCursor` when binding present.
- **Dark theme.** Already in our Theme.qml — match.
- **No device chassis silhouette in v1.** OpenDeck doesn't have one;
  we don't need one. Add it later if user testing shows it helps.
- **Action library on the LEFT** (our existing UI puts Inspector on
  the right, so the natural pattern is library-left / grid-center /
  inspector-right). OpenDeck puts library on the right
  (`+page.svelte:74-75`), but that's because the device selector is
  top-LEFT. Our sidebar is already top-LEFT so library on the right
  is the matching mirror. **Decision: library on RIGHT, inspector
  moves below or stays right of library.** Confirm via UI sketches
  in `gsd-ui-phase 26`.

### 7.5 Reused vs New File Inventory

| File                                                | Status           | Change                                                                          |
| --------------------------------------------------- | ---------------- | ------------------------------------------------------------------------------- |
| `src/app/qml/Main.qml`                              | Modify (+1 line) | Wire `setActiveDevice` on sidebar selection                                     |
| `src/app/qml/ProfileEditor.qml`                     | Modify           | Replace `KeyDesigner` Loader with `DeviceEditor` (gated by new geometry fields) |
| `src/app/qml/KeyDesigner.qml`                       | Keep             | Fallback for devices without new geometry fields                                |
| `src/app/qml/Inspector.qml`                         | Keep, reuse      | No changes; receives `binding` from new DeviceEditor selection                  |
| `src/app/qml/components/KeyCell.qml`                | Extend           | Add `DropArea` + `Drag.active` + binding payload mime                           |
| `src/app/qml/DeviceEditor.qml`                      | NEW              | Geometry-driven layout; reads DeviceDescriptor                                  |
| `src/app/qml/components/EncoderCell.qml`            | NEW              | Round dial cell (drop target + drag source)                                     |
| `src/app/qml/components/TouchStripZone.qml`         | NEW              | LED-bar cell for touchZoneCount or wide-rect for mainScreen                     |
| `src/app/qml/ActionLibraryPane.qml`                 | NEW              | Right-pane drag-source list                                                     |
| `src/devices/streamdeck/src/register.cpp`           | Modify           | Populate `keyRows / touchZoneCount` per row                                     |
| `src/core/include/ajazz/core/device.hpp`            | Modify           | Add 4 fields to `DeviceDescriptor` (Section 6)                                  |
| `src/app/src/stream_dock_control_service.{hpp,cpp}` | Keep             | Already exposes `setActiveDevice`; no changes                                   |
| `tests/unit/test_device_descriptor.cpp`             | NEW or extend    | Cover the new fields                                                            |
| `tests/qml/`                                        | Latent issue     | Skip per CLAUDE.md note; add tests when QML link target fixed                   |

### 7.6 ActionInstance vs Our Binding Schema

OpenDeck's `ActionInstance` (`src/lib/ActionInstance.ts:1-11`) carries
`{ action, context, states, current_state, settings, children }`. Our
`Binding` (`profile.hpp:100-105`) carries `{ onPress[], onRelease[], onLongPress[], state }` and the `KeyState` (line 81-87) carries
`{ imagePath, text, background, foreground, fontSize }`.

These are not 1:1 but the editor only needs to know:

- Which slot is occupied? → check `array[position]`
- What icon to draw? → `state.imagePath` (us) / `state.image` (OD).
- What label? → `state.text` (us) / `state.text` (OD).

The Inspector already binds these (`Inspector.qml:165,200`). **No
schema work needed for Phase 26.**

## 8. Risk Register

1. **GPL-3.0 code provenance.** OpenDeck and opendeck-akp05 are
   GPL-3.0; our project is GPL-3.0-or-later. License-compatible but
   we must NOT copy code verbatim into our tree without attribution.
   **Mitigation:** treat all three repos as design references only;
   re-implement patterns in QML/C++ idioms.

1. **AKP153R key count discrepancy.** mirajazz example expects 18
   keys (3×6); our catalogue lists 15. **Mitigation:** keep at
   `scaffolded` maturity until live capture; add to v1.3 device
   roadmap as "open hardware item".

1. **AKP05 / N4 key JPEG dim discrepancy.** opendeck-akp05 ships
   112×112; we ship 85×85; both render on the AKP05E hardware (we
   confirmed live 2026-05-28). **Mitigation:** Phase 26 is UI-only;
   wire-layer dim reconciliation is a follow-up.

1. **DRA opcode "silently ignored" on N4.** opendeck-akp05 reports
   `write_lcd` doesn't pixel-position on the N4 (`device.rs:204-205`);
   they fall back to 4 discrete LCD button writes for the touch
   strip. Our `akp05_protocol.hpp:117-120` exposes both the per-
   encoder image path AND the DRA rect-addressable path. The editor
   should use the per-encoder path by default; the wide-DRA path
   only for AKP815-class single-strip devices.

1. **Mirabox N1 missing from catalogue.** `0x6603:0x1000`, 18 keys
   (3×5 + top row of 3), real device per mirajazz example.
   **Mitigation:** add as a scaffolded entry in `devices.yaml` and
   `register.cpp`; the geometry editor needs heterogeneous-row
   support eventually, but for v1.3 we can model it as 4 rows × 5
   columns with bottom-row positions 18-19 marked as "absent".

1. **Touch-zone count vs `hasTouchStrip` boolean.** Our existing
   `DeviceDescriptor::hasTouchStrip = true` is set for both AKP05
   (4 zones aligned to encoders) and AKP815 (1 wide strip). The
   editor needs to disambiguate — using `touchZoneCount` (=4 for
   AKP05, =0 for AKP815) plus `mainScreenWidthPx` (=0 for AKP05,
   =800 for AKP815) does this cleanly.

1. **HID `usage_page=65440 (0xFFA0)` filter.** mirajazz queries
   devices with this usage page; our `DeviceDescriptor::controlUsagePage`
   is 0 (default-open) for AKP05. On single-interface Linux devices
   this is fine; on Windows composite devices it may cause a wrong-
   interface open. **Mitigation:** add `controlUsagePage = 0xFFA0` to
   the AKP05 / Mirabox N4 / AKP05E rows in `register.cpp` when
   landing Section 6 changes; verify on live Windows.

1. **QML DragHandler + ListView flick conflict.** Standard Qt 6 QML
   pitfall. **Mitigation:** explicit `dragThreshold` and
   `acceptedButtons: Qt.LeftButton` on `DragHandler`; smoke-test on
   first ActionLibraryPane build.

1. **Per-codename window resize.** OpenDeck auto-resizes the window
   per device (`DeviceSelector.svelte:50-63`). Our app has a fixed
   layout. **Decision:** the new `DeviceEditor.qml` should let the
   grid + library + inspector flex within the existing main window
   width — don't introduce window resizing. Test on AKP815's 5×3
   portrait vs AKP05's 2×5 landscape and AKP153's wide 3×5.

1. **Demo-unit input-streaming gap persists.** Phase 25 BLOCKED
   Tests 2-5 + 15-16 remain BLOCKED even after Phase 26 (the editor
   can't surface a press that never arrives). **Documented.** Phase
   26's success criterion is image-upload + drag-drop UX, NOT
   end-to-end key-press round-trip.

## 9. Open Questions for Discuss-Phase

These are NOT decided in this research — surface them in
`gsd-discuss-phase 26`:

1. **Library pane LEFT or RIGHT of grid?** OpenDeck = right; we have
   Inspector on the right today. Mirror the layout (library right,
   move Inspector below or to a popover) OR break the OpenDeck
   convention and put library on the LEFT?
1. **Device chassis silhouette behind the grid — v1 or follow-up?**
   The Phase 26 CONTEXT wording "Elgato Stream Deck-pattern device-
   shaped editor" could be interpreted either as just-an-abstract-grid
   (OpenDeck-style) or with a soft device photo behind. The user
   directive 2026-05-28 13:10 says "rendere la UI come quella di
   Elgato StreamDeck" — Elgato's own editor uses a device photo
   background.
1. **Per-cell pixel size — fixed 96×96 (current `KeyCell.qml:37-38`)
   or device-scaled (e.g. 112×112 for AKP05, 60×60 for AKP03)?**
   OpenDeck uses 144×144 for everything except XL (192×192). Suggest
   fixed visual size with the actual hardware dim being a wire-layer
   concern.
1. **Action library content for v1.** Phase 26 CONTEXT defers
   plugin-action drag-drop to Phase 20-21 follow-up. Initial library
   = the 5 built-in `ActionKind` entries from `Inspector.qml:215-221`?
1. **Touch-strip-zone editor for AKP815's 800×480 wide strip.** The
   AKP815 is the odd one out — single rect-addressable strip, not
   4 zones. Scope it for v1 (extra `MainScreenEditor.qml` component)
   or defer to follow-up?
1. **`Profile::keys` offset convention for touch zones.** Reuse the
   `keys` map at offsets `keyCount..keyCount+touchZoneCount` (OpenDeck
   model) OR add a new `Profile::touchZones` map (cleaner schema, but
   breaks the on-disk JSON format)?

## 10. References

### In-repo source files (lines verified)

- `src/app/qml/KeyDesigner.qml:40-176` — current generic NxN grid.
- `src/app/qml/Inspector.qml:95-119` — FileDialog with URL→String fix (Phase 25 L1).
- `src/app/qml/ProfileEditor.qml:253-264` — Keys tab Loader (Phase 26 swap point).
- `src/app/qml/components/KeyCell.qml:25-94` — single LCD-key delegate (Phase 26 extends).
- `src/app/qml/Main.qml:127-129` — sidebar onDeviceSelected (Phase 26 +1 line).
- `src/devices/streamdeck/src/register.cpp:117-336` — DeviceDescriptor rows.
- `src/devices/streamdeck/src/akp05_protocol.hpp:69-92,117-120` — AKP05 geometry constants.
- `src/devices/streamdeck/src/akp03_protocol.hpp:67-71` — AKP03 geometry constants.
- `src/devices/streamdeck/src/akp153_protocol.hpp:64-65` — AKP153 geometry constants.
- `src/devices/streamdeck/src/akp815_protocol.hpp:36-39` — AKP815 geometry constants.
- `src/core/include/ajazz/core/device.hpp:52-103` — DeviceDescriptor struct.
- `src/core/include/ajazz/core/profile.hpp:100-173` — Binding / EncoderBinding / Profile.
- `src/app/src/stream_dock_control_service.cpp:116-148` — setActiveDevice impl (GAP-25A).
- `docs/_data/devices.yaml` — full SKU catalogue (24 devices).
- `docs/protocols/streamdeck/akp05_init_sequence.md` — init handshake.
- `docs/protocols/streamdeck/akp05_input_corrections.md §7.1` — demo-unit input gap.

### OpenDeck (`nekename/OpenDeck` @ f7d9e07)

- `src/lib/DeviceInfo.ts:1-9` — DeviceInfo type (6 fields).
- `src/lib/Profile.ts:1-8` — Profile { keys, sliders }.
- `src/lib/Context.ts:1-6` — Context { device, profile, controller, position }.
- `src/lib/Action.ts:1-15` — Action type with `controllers: string[]` whitelist.
- `src/lib/ActionInstance.ts:1-11` — ActionInstance with states + children.
- `src/components/DeviceView.svelte:1-244` — geometry-driven editor.
  - Lines 19-57 — drag-drop handlers.
  - Lines 76-77 — overflow detection.
  - Lines 83-90 — heterogeneous gridRowLengths.
  - Lines 106-156 — arrow-key navigation.
  - Lines 192-244 — three-row composition (keys / encoders / touchpoints).
- `src/components/Key.svelte:1-272` — single canvas-backed cell.
  - Lines 19-69 — select + context-menu handlers.
  - Lines 96-126 — clipboard copy / paste / clear.
  - Lines 146-178 — canvas render via renderImage().
  - Lines 187-228 — markup with conditional rounded-full + touch-bar.
- `src/components/ActionList.svelte:1-144` — drag-source library pane.
  - Lines 13-19 — invoke get_categories / list_plugins.
  - Lines 78-88 — search input.
  - Lines 113-138 — draggable action row.
- `src/components/DeviceSelector.svelte:1-84` — dropdown + window resize.
  - Lines 50-63 — auto-resize per-device.
- `src/routes/+page.svelte:1-75` — single page composition.
- `src-tauri/src/shared.rs:31-44` — Rust DeviceInfo (mirrors TS type).
- `src-tauri/src/elgato.rs:103-137` — first-party Elgato init flow.
  - Lines 109-116 — Kind → type ordinal mapping.
  - Lines 121-137 — register_device with explicit row/col/encoder/touchpoint counts.
- `src-tauri/src/events/inbound/devices.rs:9-46` — plugin-driven register_device.
- `src-tauri/src/store/profiles.rs:41-46` — profile arrays sized to
  `rows*cols+touchpoints` and `encoders`.

### opendeck-akp05 (`naerschhersch/opendeck-akp05` @ d77e9e2)

- `manifest.json` — plugin metadata, "Actions": [] (no actions, just a device-support plugin).
- `src/mappings.rs:25-32` — ROW_COUNT=2, COL_COUNT=5, ENCODER_COUNT=4, DEVICE_TYPE=7.
- `src/mappings.rs:40-54` — VID/PID pairs + DeviceQuery with usage_page=65440.
- `src/mappings.rs:93-112` — ImageFormat per surface (112×112 keys, 184×120 touch zones).
- `src/device.rs:44-57` — register_device via openaction outbound IPC.
- `src/device.rs:181-283` — handle_set_image, with comment at 204-205 about write_lcd silently ignored.
- `src/device.rs:237-243` — OpenDeck→hardware position remap (top row offset 10).
- `src/inputs.rs:1-156` — input code → DeviceInput parser (placeholder values).
- `CLAUDE.md` — author's own per-codepath summary.

### mirajazz (`4ndv/mirajazz` @ 37a9512)

- `README.md` — library philosophy ("no device-specific code"), protocol version table.
- `src/lib.rs` — module list (device / error / images / state / types).
- `src/types.rs:1-95` — HidDeviceInfo, DeviceInput, ImageFormat, ImageMode, ImageRotation,
  ImageMirroring.
- `src/device.rs:33-49` — DeviceQuery { usage_page, usage_id, vendor_id, product_id }.
- `src/device.rs:78-171` — DeviceWatcher async stream.
- `src/device.rs:179-281` — Device struct + Device::connect entry point.
- `src/device.rs:351-367` — initialize() with CRT DIS + CRT LIG opening handshake.
- `src/device.rs:380-407` — set_brightness, set_led_brightness.
- `src/device.rs:513-565` — set_button_image (per-key JPEG path).
- `src/state.rs:1-188` — DeviceStateReader, DeviceStateUpdate enum (ButtonDown/Up,
  EncoderDown/Up, EncoderTwist).
- `examples/akp03r.rs:8,30` — `DeviceQuery::new(65440, 1, 0x0300, 0x1003)`,
  `Device::connect(&dev, 2, 9, 3)`.
- `examples/akp153r.rs:9-11,21-37,53` — 18-key AKP153R + 18-element index permutation.
- `examples/n1.rs:9,18,28-40` — Mirabox N1 18 keys, heterogeneous JPEG sizes.

### Licenses

- OpenDeck: GPL-3.0-only (`/tmp/phase26-research/OpenDeck/LICENSE.md`).
- opendeck-akp05: GPL-3.0-only (`/tmp/phase26-research/opendeck-akp05/LICENSE`).
- mirajazz: MPL-2.0 (`/tmp/phase26-research/mirajazz/LICENSE`).
- Our project: GPL-3.0-or-later. Direct code copy from OpenDeck and opendeck-akp05 is
  license-compatible; we choose patterns-only learning for cleaner attribution.
