# 260514-1je — Stream Dock features & KeyDesigner research

Research output for quick task `260514-1je-stream-dock-keydesigner`. Audience: the engineer
implementing `src/app/qml/KeyDesigner.qml` and adjacent C++ wiring in this iteration.

Scope: enumerate what every Stream Dock-class device can do, distill the canonical Stream
Deck feature taxonomy from Elgato + OpenDeck, map both onto our current code, recommend the
smallest coherent slice for AKP153, and surface the decisions the implementer must take
before writing code.

______________________________________________________________________

## 1. Stream Dock product landscape

Per-device feature matrix for the AJAZZ family. Resolutions and image formats are pulled
from the protocol headers (`akp153_protocol.hpp:34`, `akp03_protocol.hpp:32`,
`akp05_protocol.hpp:33`) and the runtime `IDisplayCapable::displayInfo()` returns
(`akp03.cpp:312`, etc.).

| Codename             | VID:PID   | Keys (grid)     | Key image           | Encoders           | Touch strip | RGB (per-key) | Firmware query | Notes / closest Elgato analog                                                     |
| -------------------- | --------- | --------------- | ------------------- | ------------------ | ----------- | ------------- | -------------- | --------------------------------------------------------------------------------- |
| `akp153`             | 0300:1001 | 15 (5×3)        | JPEG 85×85          | 0                  | no          | no            | yes (stub)     | International AKP153 / Mirabox HSV293S. Equivalent of Stream Deck **MK.2**.       |
| `akp153e`            | 0300:1002 | 15 (5×3)        | JPEG 85×85          | 0                  | no          | no            | yes (stub)     | China-market AKP153E. Identical protocol.                                         |
| `akp03`              | 0300:3001 | 6 (3×2)         | PNG 72×72           | 1 (pressable)      | no          | no            | yes (stub)     | Mirabox N3. Closest Elgato analog: **Stream Deck Mini** + a knob bolted on.       |
| `akp03_variant_3004` | 0300:3004 | 6 (3×2)         | PNG 72×72 (assumed) | 1 (assumed)        | no          | no            | unknown        | Newly enumerated 2026-05-13, "Ajazz HOTSPOTEKUSB HID DEMO". Pending verification. |
| `akp05`              | 0300:5001 | 5 (5×1) + 4 enc | JPEG 85×85          | 4 (with LCD rings) | yes         | no            | yes (stub)     | "Stream Dock Plus" class. Direct counterpart to **Stream Deck +**.                |

Elgato references for sizing only:

- Stream Deck **Mini**: 6 keys (3×2), 80×80 BMP.
- Stream Deck **MK.2** / Original v2: 15 keys (5×3), 72×72 JPEG.
- Stream Deck **XL**: 32 keys (8×4), 96×96 JPEG.
- Stream Deck **+**: 4 LCD keys (4×2 originally, layout varies) + 4 dials + 800×100 touch strip.

Key takeaway: the AKP153 family is the cleanest analog to the Stream Deck MK.2 — same key
count, same grid, similar 85-vs-72 px square LCD. **Designing the QML editor against
AKP153 first is the right call**: nothing on AKP153 forces UI complexity beyond a single
homogeneous grid.

______________________________________________________________________

## 2. Feature surface — what every Stream Deck-class device does

The taxonomy below merges three sources of truth:

- **Elgato Stream Deck** (canonical UX), per `docs.elgato.com/streamdeck/sdk` and the help
  centre articles.
- **OpenDeck** (Linux/macOS/Windows community client), per
  `github.com/nekename/OpenDeck` — specifically `src-tauri/src/shared.rs` which defines the
  on-disk profile schema.
- **Vendor-specific notes** for AJAZZ, from `docs/research/vendor-protocol-notes.md` and
  `vendor-feature-matrix.md` in this repo.

Implementation-cost tiers used below:

- **T1** — pure UI, no protocol work. ~hours of QML.
- **T2** — UI + thin C++ wire to an existing capability. ~half a day.
- **T3** — UI + new C++ subsystem or new data-model fields. ~1-2 days.
- **T4** — new protocol work or plugin host involvement. ~multi-day spike.

### 2.1 Per-key configuration

| Sub-feature                                   | Elgato                                                                                                 | AJAZZ vendor                                                                               | OpenDeck                                                                                  | Tier |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------- | ---- |
| Action assignment (single)                    | Drag plugin action onto a key.                                                                         | Same — vendor app uses categorised action list.                                            | Same — `ActionInstance.action` per slot.                                                  | T2   |
| Action assignment (multi-step / multi-action) | Key Logic + Multi-Action: chain of N actions, each with delay, optional press/release/long-press slot. | Per `docs/research/vendor-feature-matrix.md`, vendor app exposes the same multi-action UI. | Built-in action `"opendeck.multiaction"` with `children: Vec<ActionInstance>`.            | T3   |
| Title / label text                            | Per-state text, font family, size, alignment (top/middle/bottom), colour, stroke.                      | Vendor app exposes title text + font size, alignment.                                      | `ActionState { text, family, style, size, colour, stroke_colour, alignment, underline }`. | T2   |
| Custom icon                                   | Upload image file; library of stock icons; auto-resize; "key creator" generator.                       | Vendor app has bundled icon library + file picker.                                         | `ActionState.image` is a path/URL/data URI string.                                        | T2   |
| Per-state icons (toggle on / off)             | Each action has N states (typ. 2 for toggles); each state has its own image, title, colour.            | Vendor app supports the same.                                                              | `states: Vec<ActionState>` per action; `current_state: u16` indexes it.                   | T3   |
| Background / foreground colour fill           | Solid colour fallback when no image is set.                                                            | Same.                                                                                      | `background_colour`, `colour`.                                                            | T1   |
| Long-press vs tap distinction                 | "Key Logic": single-press, double-press, press-and-hold can each fire a different action.              | Mirajazz exposes the press-state flag; vendor app exposes long-press in some modes.        | Single fire model; long-press requires plugin logic.                                      | T3   |

### 2.2 Multi-action behaviour

Elgato's multi-action is a flat ordered list of children with optional inter-step delays.
OpenDeck mirrors this through a single special action UUID (`opendeck.multiaction`) whose
`children: Option<Vec<ActionInstance>>` holds the chain. No conditionals; no branching.

This repo already encodes the right model in `Profile::Binding`:

- `Binding { onPress: Vec<Action>, onRelease: Vec<Action>, onLongPress: Vec<Action> }`
  (`src/core/include/ajazz/core/profile.hpp:96`).
- Each `Action` has `delayMs` for inter-step delays
  (`profile.hpp:63`).
- `ActionKind::Sleep` exists as an explicit step.

**No model changes are needed to ship multi-action support**, only an editor UI. **Tier:
T3** (UI alone — adding "list-of-steps" rows is the meaningful effort).

### 2.3 Folders / pages

Elgato: a "folder" is an action that swaps the visible page to a child grid. Back-button is
auto-injected on the first slot. OpenDeck mirrors this: parent–child profile relationships
keyed by id. AJAZZ vendor app supports the same UX.

This repo already encodes folders:

- `ProfilePage { id, name, keys: map<idx, Binding>, children }` at
  `profile.hpp:123`.
- `ActionKind::OpenFolder` + `ActionKind::BackToParent` at `profile.hpp:44-46`.

**Tier: T3** — UI alone. The data model is ready.

### 2.4 Profiles

Elgato: per-device profile, can auto-activate on a specified foreground application
(`applicationHints`-style), or be switched manually. Default profile per device.

This repo: `Profile::applicationHints: Vec<string>` at `profile.hpp:168`. `ProfileController`
(`src/app/src/profile_controller.{hpp,cpp}`) handles load/save. JSON wire-key is `"device"`
per the schema doc.

**Tier: T2** for manual switching (a profile dropdown exists in design). Auto-switch
needs a focused-window watcher → **T4**. Out of scope here.

### 2.5 Brightness / sleep

Elgato: per-profile brightness slider (0–100), sleep timeout, optional dim on idle.

This repo: `IDisplayCapable::setBrightness(percent)` works for all four families
(`akp153.cpp:332`, similar in akp03/akp05). No sleep/idle. **Tier: T2** to surface a
slider; **T4** for idle dimming (needs a host-side timer + activity tracker).

### 2.6 Encoder behaviour (AKP03 / AKP05)

Elgato dial events: `onDialDown` / `onDialUp` / `onDialRotate`. The dial also gets a
"layout" panel on the touch strip (Stream Deck +). Encoder display content is set via
`setFeedback()` with named layouts (Icon, Canvas, Value, Indicator, etc.).

AJAZZ: AKP03 has 1 encoder, AKP05 has 4. AKP05 encoders also have per-encoder LCD strips
above them (100×100 in code? — confirm against `akp05_protocol.hpp` constants;
`setEncoderImage()` is wired at `akp05.cpp:448`). AKP03's encoder has no screen.

This repo: `EncoderBinding { onCw, onCcw, onPress, state }` at `profile.hpp:109`. Already
the right shape. **Tier: T3** to ship the editor. Out of scope here (deferred to
follow-up).

### 2.7 Touch strip (AKP05)

Elgato touch strip events: `onTouchTap`, `onLongTouch`, swipe. 800×100 px canvas split into
4 zones (one per dial). Per-zone "layout" rendering.

AJAZZ AKP05: touch strip captures arrive via `DeviceEvent::Kind::TouchStrip` with `value`
encoding gesture + X (`akp05.cpp:357`). The C++ backend supports it; no UI exposes it. **Tier:
T3-T4**. Out of scope here.

### 2.8 RGB

Elgato Stream Deck has no per-key RGB; brightness only. The AJAZZ AKP family also has no
per-key RGB (per `register.cpp` — none of the AKP descriptors set `hasRgb`). RGB is a
keyboard/mouse concern in this codebase, not a Stream Dock concern.

**Decision**: the existing `RgbPicker.qml` tab should be **hidden** for Stream Dock devices.
`ProfileEditor.qml:48-51` already gates it on `capabilities.hasRgb`; the tab will not
appear for any AKP family device.

### 2.9 Firmware

Elgato: in-app firmware update via separate updater. AJAZZ: same architecture — vendor
ships a `FirmwareUpgradeTool.exe` linked against `Qt5SerialPort` (per
`vendor-feature-matrix.md` row "Hardware firmware update").

This repo: `IFirmwareCapable::beginFirmwareUpdate()` throws on AKP153 (`akp153.cpp:345`);
no UI surface. **Tier: T4**. Out of scope here.

### 2.10 Per-key default state vs pressed state

Worth calling out separately because it is **the most commonly misunderstood part of the
Stream Deck data model**:

Elgato and OpenDeck both encode visual appearance as a **list of states**
(`states: Vec<ActionState>`) where each state has its own image, title, colours, font.
"Pressed" is not a separate visual mode; it's "increment `current_state` modulo
`states.length`". A toggle action defines 2 states (off/on). A 4-mode action defines 4
states.

This repo currently has a single `KeyState` per `Binding` (`profile.hpp:77`). That's
sufficient for "always render the same thing" but cannot represent toggle behaviour
without extension. **Decision required** — see section 5.

______________________________________________________________________

## 3. Gap analysis — current AJAZZ Control Center status

Reading the QML and C++ sources cited at the top of section 1, here is the status of every
canonical feature on the codebase today.

### Per-key

| Feature                          | Status     | Evidence                                                                                                      |
| -------------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------- |
| Render N×M key grid              | 🟡 partial | `KeyDesigner.qml:36-55` — grid is rendered, but `KeyCell.qml:30-43` is text-only (no image, no live preview). |
| Click → select key               | ✅         | `KeyDesigner.qml:49-53` emits `keySelected` + `keyActivated`.                                                 |
| Inspector form: action / label   | 🟡 partial | `Inspector.qml:67-117` has the form. No bidirectional bind to a profile model — `snapshot()` builds a Map.    |
| Per-key icon upload (file)       | 🔴 missing | `Inspector.qml:99-108` has a TextField for the icon path; no file picker, no preview, no resize pipeline.     |
| Per-key icon library             | 🔴 missing | No qrc-bundled or filesystem-loaded icon library exists.                                                      |
| Per-key label text + font + size | 🟡 partial | `Inspector.qml:88-97` has a label TextField. No font/size/colour controls.                                    |
| Per-state icons                  | 🔴 missing | `KeyState` is single-state in core (`profile.hpp:77`). UI cannot represent toggles.                           |
| Live preview of LCD render       | 🔴 missing | KeyCell shows index text only. No `Image` element bound to a rendered icon.                                   |
| Background / foreground colour   | 🔴 missing | Core supports `KeyState.background`/`foreground`; no UI exposes it.                                           |
| Multi-step action editor         | 🔴 missing | Inspector has 1 action-type combo + 1 params field. No list-of-steps UI.                                      |
| Long-press combo                 | 🟡 partial | `Inspector.qml:110-117` has a combo (Disabled / Repeat / Alternate action), no wire-up.                       |

### Profiles & folders

| Feature                       | Status     | Evidence                                                                                        |
| ----------------------------- | ---------- | ----------------------------------------------------------------------------------------------- |
| Profile load/save             | ✅         | `ProfileController::loadProfile/saveProfile` (`profile_controller.hpp:65,77`).                  |
| Profile JSON schema           | ✅         | `Profile`, `Binding`, `KeyState`, `Action` in `profile.hpp:30-169`.                             |
| Folder pages                  | 🟡 partial | Schema supports `Profile::pages` (`profile.hpp:155`). No UI navigates them.                     |
| Profile dropdown / switcher   | 🟡 partial | `ProfileController::knownProfileIds()` exists (`profile_controller.hpp:86`) but no UI consumer. |
| Auto-switch on foreground app | 🔴 missing | `applicationHints` field exists; no host-side foreground watcher.                               |

### Device-level

| Feature                  | Status     | Evidence                                                                                        |
| ------------------------ | ---------- | ----------------------------------------------------------------------------------------------- |
| Brightness slider        | 🔴 missing | Backend works (`akp153.cpp:332`); no QML control exists in `ProfileEditor.qml`.                 |
| Clear-all-keys           | ✅         | `Akp153Device::clearKey(0xff)` (`akp153.cpp:322`).                                              |
| Firmware version display | 🟡 partial | `IDevice::firmwareVersion()` returns "unknown" (`akp153.cpp:234`). No actual query implemented. |
| Firmware update          | 🔴 missing | Throws unconditionally (`akp153.cpp:346`).                                                      |

### Encoder / touch strip / RGB / mouse

| Feature                     | Status     | Evidence                                                                  |
| --------------------------- | ---------- | ------------------------------------------------------------------------- |
| Encoder grid render         | 🟡 partial | `EncoderPanel.qml:32-49` shows N dials, no per-encoder binding editor.    |
| Encoder CW/CCW/Press editor | 🔴 missing | Core has `EncoderBinding` (`profile.hpp:109`); no UI.                     |
| Touch strip editor (AKP05)  | 🔴 missing | Backend captures events (`akp05.cpp:357`); no UI.                         |
| RGB picker integration      | ⏭️ n/a     | No AKP family has RGB. `RgbPicker.qml` exists for keyboard/mouse devices. |

______________________________________________________________________

## 4. Recommended slice for this iteration

**Target device class: AKP153 / AKP153E (15 keys, 5×3, JPEG 85×85, no encoder, no touch
strip, no RGB).** That is the simplest, most-shipped family and the cleanest analog to the
Stream Deck MK.2 UX baseline.

### What is in the slice

1. **`KeyDesigner.qml` becomes a real editor.** The 5×3 grid stays, but each `KeyCell` now
   renders a live LCD-style preview of its current binding (icon + overlay label),
   not a placeholder index number. The selected cell gets an accent ring (already there).
1. **`Inspector.qml` becomes the per-key form** (it largely already is; just needs to be
   wired to a real model and to gain a file-picker + colour swatch).
1. **A new lightweight QML-side model** — `KeyBindingModel.qml` or equivalent — holds an
   array of 15 `KeyBinding` objects (one per cell) and exposes
   `iconSource: url`, `label: string`, `actionKind: enum`, `actionParams: string`. The
   model is QML-only; persistence is deferred (see §5).
1. **Brightness slider** in the device header (single new control, 1 backend call).
1. **No multi-action editor, no folder navigation, no per-state icons, no font controls,
   no encoder editor, no touch strip, no profile switcher.**

### What goes in the QML component tree

```
ProfileEditor.qml (existing)
├── PageHeader
├── BrightnessRow (NEW: Label + Slider, binds to deviceController.setBrightness)
├── TabBar
└── StackLayout
    └── Loader → KeyDesigner.qml (UPGRADED)
                 ├── GridLayout (5×3 cells)
                 │   └── Repeater(model: 15)
                 │       └── KeyCell.qml (UPGRADED)
                 │           ├── Image  (preview from binding.iconSource)
                 │           ├── Text   (overlay label from binding.label)
                 │           └── MouseArea / focus ring
                 └── Inspector.qml (existing, now binds to selected binding)
                     ├── Action-type ComboBox       (existing, no change)
                     ├── Action-params TextField    (existing, no change)
                     ├── Label TextField            (existing, no change)
                     ├── Icon row (NEW)
                     │   ├── Image preview (96×96)
                     │   ├── "Choose file…" Button → FileDialog
                     │   └── "Clear" Button
                     └── Apply / Restore (existing)
```

### Data flow

```
KeyDesigner.selectedIndex  ─┐
                            ▼
                       KeyBindingModel.get(selectedIndex) ──► Inspector form fields
                            ▲                                       │
                            │                              user edits + Apply
                            └───────────── KeyBindingModel.set(selectedIndex, …)
                                                       │
                                            (deferred) ▼
                                            ProfileController.saveProfile()
```

For this slice, the QML-side `KeyBindingModel` is the **session-scoped** source of truth.
Persistence to `Profile`/`Binding` via `ProfileController` is the next quick-task slice.
That keeps the diff small and lets the user see real visual feedback today.

### C++ touchpoints

Minimal:

- Expose a Q_INVOKABLE `setBrightness(int)` on the existing `DeviceController` /
  `DeviceModel` (use whichever already proxies to `IDisplayCapable`). One method, ~20 LoC.
- If pushing an icon to the device LCD is desired in this slice (it would be a great
  smoke test), wire a `Q_INVOKABLE setKeyImage(int index, QUrl source)` that:
  1. Loads the image via `QImage::load`.
  1. Resizes to 85×85.
  1. Re-encodes to JPEG (Qt's `QImageWriter` with quality 85-90).
  1. Calls `IDisplayCapable::setKeyImage()`.
     This is ~50 LoC of C++; high user-visible value; testable manually with a device
     attached. **Recommend including it** — the slice loses most of its punch without it.

### Manual-test path

1. Build `cmake --build build/linux-debug --target ajazz-control-center`.
1. Launch with an AKP153 attached. (No device → empty-state; that's acceptable.)
1. Sidebar → select AKP153. Centre pane shows 15-cell grid.
1. Drag the brightness slider; LCDs dim / brighten in real time.
1. Click cell 5. Inspector opens with "Editing: Key 5".
1. Type "OBS" in the Label field, Apply. Cell 5 preview updates to show "OBS".
1. Click "Choose file…" → pick a PNG. Cell 5 preview shows the icon. (If C++ image
   pipeline is included: physical key 5 on the device also displays the icon.)
1. qmllint: clean.

### LoC budget

- `KeyDesigner.qml` upgrade: ~80 LoC (grid is already there).
- `KeyCell.qml` upgrade (Image + overlay + selection): ~50 LoC delta.
- `Inspector.qml` icon-row addition + binding hookup: ~60 LoC delta.
- New `KeyBindingModel.qml` (or inline ListModel in KeyDesigner): ~40 LoC.
- New `BrightnessRow.qml` component: ~30 LoC.
- C++ `setBrightness` + (optionally) `setKeyImage` wiring: ~70 LoC.
- **Total: ~330 LoC**, within the 200–400 envelope.

### What is explicitly deferred

| Deferred feature                 | Why deferred                                                                                        | Follow-up quick task       |
| -------------------------------- | --------------------------------------------------------------------------------------------------- | -------------------------- |
| Multi-action chain editor        | Needs a list-rows component + JSON shape decision; adds 200+ LoC alone.                             | "KeyDesigner multi-action" |
| Folder navigation in the grid    | Needs page-stack management + "back" key auto-injection.                                            | "KeyDesigner folders"      |
| Per-state icons (toggles)        | Requires extending `KeyState` to a list or adding `KeyState::states` array. Schema decision needed. | "KeyState multi-state"     |
| Font/size/alignment controls     | Quick to add visually but bloats Inspector; isolate for a separate UX pass.                         | "Inspector typography"     |
| Profile dropdown / new-profile   | Needs profile-library indexing in `ProfileController` (today only the active profile is cached).    | "Profile library"          |
| Encoder editor (AKP03/AKP05)     | Different device class; deserves its own slice.                                                     | "EncoderPanel buildout"    |
| Touch-strip editor (AKP05)       | Same.                                                                                               | "AKP05 touch strip UI"     |
| Profile auto-switch on app focus | Needs Linux/macOS/Windows foreground-window watchers.                                               | "Profile auto-switch"      |
| Firmware update flow             | Whole separate workstream.                                                                          | Tracked in TODO.md.        |

______________________________________________________________________

## 5. Risks and decisions to make before coding

The implementer should resolve these **before** writing the slice, because each one
changes the shape of the file diff.

### D1 — Icon source: bundled qrc vs filesystem

Two competing pulls:

- **Bundled qrc**: ships an icon library with the binary; consistent across installs; small
  surface (one Qt resource file); the user can still add files. Cost: someone has to curate
  ~30-50 icons; they bloat the binary by ~500KB-1MB.
- **Filesystem only**: zero bundled icons; everything is `file://` from a user-chosen
  location. Simplest; matches Elgato's "Key Creator" model which generates icons on demand.
  Risk: empty-state feels barren at first launch.

**Recommendation**: filesystem only **for this slice**. The icon library is a separate quick
task; a `QFileDialog` is 5 LoC. Defer.

### D2 — Per-state icons in the data model

The current `Binding::state` is a single `KeyState`. Elgato/OpenDeck use `states: Vec<…>`.
Three options:

- **Option A**: leave `Binding::state` single, add toggle support later by extending it to
  `std::vector<KeyState> states`. JSON schema becomes `"state": {…}` → `"states": [{…},…]`.
  Backwards-incompatible-ish; the JSON reader can default to a 1-element array.
- **Option B**: leave `Binding::state` single forever; encode toggles as separate
  bindings or via plugin state. Diverges from Elgato.
- **Option C**: leave the field name `state` but make it an alias for `states[currentState]`
  inside the reader. Convoluted.

**Recommendation**: **A**, but **not in this slice**. Defer the schema bump to the
multi-state quick task. The slice supports only single-state visuals.

### D3 — Multi-action shape

The schema (`Binding::onPress: vector<Action>`) is already correct; it's a list. The
editor UI shape is the open question:

- **Inline list of action rows inside Inspector** (Elgato/OpenDeck UX). Compact when short,
  scrollable when long.
- **Separate modal "Multi-Action editor"** triggered by the action-type combo. Cleaner but
  more clicks.

Defer to the next quick task. **Recommendation**: when implemented, go with inline-list —
matches user expectations from Elgato.

### D4 — Profile persistence: JSON shape

Two paths:

- **Match Elgato's `.streamDeckProfile`** (zipped manifest + assets). Lets users
  cross-import. Cost: real implementation work (zip + binary asset packaging + schema
  fidelity).
- **Our own `.ajazzprofile`** JSON — what `profile.hpp` already encodes. Simpler.
  `vendor-feature-matrix.md` notes this is already done (✅) for round-trip.

**Recommendation**: **stay with `.ajazzprofile`** (already implemented). Provide an
**import** path from `.streamDeckProfile` as a separate slice if a user asks; do not
mirror Elgato's shape natively.

### D5 — Encoder secondary actions: include now or defer?

The slice targets AKP153, which has zero encoders. `_showEncoders` in `ProfileEditor.qml`
is already gated on `encoderCount > 0`, so the Encoders tab is invisible for AKP153.

**Recommendation**: **defer**. Out of scope here.

### D6 — Brightness slider placement

Two options:

- **Inside the Keys tab** (top of `KeyDesigner.qml`).
- **In the `ProfileEditor.qml` header**, above the TabBar — applies to all tabs.

The latter is correct: brightness is device-level, not tab-level. **Recommendation**:
add a thin `BrightnessRow` between `PageHeader` and `TabBar` in `ProfileEditor.qml`,
visible only when the device exposes `IDisplayCapable`.

### D7 — Apply / Revert semantics for the per-key form

Elgato's app commits per-keystroke (no "Apply" button). The current Inspector has Restore /
Revert / Apply at the `ProfileEditor` level. For this slice — where the model is QML-only
and not persisted — those buttons are misleading.

**Recommendation**: in the slice, **bind Inspector form fields directly** to the
`KeyBindingModel` so edits show immediately in the cell preview. Keep the Apply button
disabled (or hidden) until the persistence layer is wired in the next slice. Matches
modern UX expectations and avoids dead-button confusion.

### D8 — Send-to-device timing

When the user changes an icon, do we re-encode + push to the device LCD on every change, or
only on a debounce/idle? JPEG encoding 85×85 is cheap (sub-millisecond), but the HID write
takes ~16ms per 512-byte packet × ~30 packets per image = ~500ms.

**Recommendation**: **debounce to 250 ms** after the last change, then push. Avoids
HID-bus thrash during file-picker navigation.

______________________________________________________________________

## 6. Sources

Primary sources consulted while preparing this document.

### In-repo

- `src/core/include/ajazz/core/profile.hpp` — profile schema (`Profile`, `Binding`, `Action`,
  `KeyState`, `ProfilePage`). Lines 30–169.
- `src/core/include/ajazz/core/capabilities.hpp` — `Capability` enum (line 31), `DisplayInfo`
  (line 96), `IDisplayCapable`/`IRgbCapable`/`IEncoderCapable`/`IFirmwareCapable` interfaces.
- `src/core/include/ajazz/core/device.hpp` — `DeviceDescriptor`, `DeviceEvent::Kind`,
  `IDevice`. Lines 52, 78, 112.
- `src/devices/streamdeck/src/akp153.cpp` — AKP153 backend, esp. `displayInfo()` at line 291,
  `setKeyImage()` at line 301, `setBrightness()` at line 332.
- `src/devices/streamdeck/src/akp03.cpp` — AKP03 backend, `jpegEncoded=false` at line 316
  (PNG format), `EncoderInfo` at line 360.
- `src/devices/streamdeck/src/akp05.cpp` — AKP05 backend, touch-strip event emission at
  line 357, `setEncoderImage()` at line 448.
- `src/devices/streamdeck/src/register.cpp` — VID/PID registrations for all five Stream Dock
  variants (lines 37–116).
- `src/app/qml/KeyDesigner.qml` — current 57 LoC stub.
- `src/app/qml/EncoderPanel.qml` — current 51 LoC stub.
- `src/app/qml/Inspector.qml` — per-key form (143 LoC, mostly already in place).
- `src/app/qml/ProfileEditor.qml` — 4-tab parent (172 LoC).
- `src/app/qml/components/KeyCell.qml` — current text-only tile (48 LoC).
- `src/app/src/profile_controller.hpp` — `ProfileController` Qt bridge.
- `docs/research/vendor-protocol-notes.md` — verified vendor app reconnaissance.
- `docs/research/vendor-feature-matrix.md` — current gap-status matrix.

### External — protocol & SDK

- OpenDeck — `github.com/nekename/OpenDeck` — Linux Stream Deck client. Wire-protocol-faithful
  to Elgato's SDK, supports third-party AKP devices via plugins.
- OpenDeck profile schema — `github.com/nekename/OpenDeck/blob/main/src-tauri/src/shared.rs` —
  `Profile`, `ActionInstance`, `ActionState`, `Context`, `DeviceInfo` structs (verbatim
  field names in §2).
- `opendeck-akp03` plugin — `github.com/4ndv/opendeck-akp03` — third-party plugin enabling
  OpenDeck to drive AKP03/Mirabox N3/N3EN + ~10 rebrand variants on Linux (guaranteed),
  macOS (best-effort), Windows (community).
- `opendeck-akp153` plugin — `github.com/4ndv/opendeck-akp153` — third-party plugin for
  AKP153 family.
- `mirajazz` crate — `github.com/4ndv/mirajazz` — hardfork of `elgato-streamdeck` for AJAZZ
  devices. Defines protocol versions v0/v1 (512-byte packets), v2 (1024-byte, unique
  serials), v3 (1024-byte + GIF support + both keypress states).
- `mirajazz` types — `github.com/4ndv/mirajazz/blob/main/src/types.rs` — `DeviceInput`
  (`ButtonStateChange/EncoderStateChange/EncoderTwist`), `ImageMode` (`None/BMP/JPEG`),
  `ImageFormat`.
- `ajazz-sdk` — `github.com/mishamyrt/ajazz-sdk` — alternative Rust SDK; supports AKP153,
  AKP153E, AKP153R, AKP815, AKP03, AKP03E, AKP03R, AKP03RV2 (note: **no AKP05** in this
  SDK, matches our experience that AKP05 protocol is distinct).
- `elgato-streamdeck` — `github.com/OpenActionAPI/rust-elgato-streamdeck` — canonical Elgato
  protocol reference. AJAZZ device support was **removed in v0.11** in favour of
  `mirajazz`.

### External — Elgato references

- Elgato Stream Deck SDK actions — `docs.elgato.com/streamdeck/sdk/guides/actions/` — action
  lifecycle, manifest, controller types (Keys / Dials), `States` array.
- Elgato Stream Deck SDK dials & touch — `docs.elgato.com/streamdeck/sdk/guides/dials/` —
  `onDialDown/onDialUp/onDialRotate/onTouchTap/onLongTouch`, layouts ($X1/$A0/$A1/$B1/$B2/$C1),
  200×100 touch strip per dial.
- Elgato Stream Deck SDK profiles — `docs.elgato.com/streamdeck/sdk/guides/profiles/` —
  `.streamDeckProfile` file format, plugin-bundled profiles, auto-install semantics.
- Elgato help — multi-actions —
  `help.elgato.com/hc/en-us/articles/360027960912-Elgato-Stream-Deck-Multi-Actions` —
  multi-action UX.
- Elgato help — pinned actions & folders —
  `help.elgato.com/hc/en-us/articles/26638431612429-Elgato-Stream-Deck-Pinned-Actions-and-Folders`.

### External — reverse engineering

- Cliff Rowley — Stream Deck HID protocol gist —
  `gist.github.com/cliffrowley/d18a9c4569537b195f2b1eb6c68469e0` — original 72×72 BMP image
  packets, V2 JPEG transition, brightness PWM, feature reports 0x03 (serial) / 0x04
  (firmware).
- Den Delimarsky — Stream Deck Plus RE — `den.dev/blog/reverse-engineer-stream-deck-plus/`
  (HTTP 403 to WebFetch; summarised via search) — touch strip X/Y at bytes 6-9, "Encoder"
  = dial + touch zone combined unit, layouts and triggers.
- Hackaday coverage of Den's RE — `hackaday.com/2024/12/26/stream-deck-plus-reverse-engineered/`.
