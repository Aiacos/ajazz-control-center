# UI / UX Consistency Review — AJAZZ Control Center

**Reviewer:** UI consistency subagent
**Scope:** `src/app/qml/*.qml` (Main, DeviceList, ProfileEditor, KeyDesigner, RgbPicker, EncoderPanel, MousePanel) + comparison vs. competitor control-center software.
**Reference mockup:** `ajazz_gui_mockup.png` (target visual language).
**Status of code:** early-alpha scaffolding. The QML files compile and lay out correctly, but they are clearly placeholder code: every screen hard-codes its own palette, no shared components exist, and most action wiring is "not yet implemented." This review treats the QML as a *baseline to refactor*, not as final code.

______________________________________________________________________

## 1. Score table

Each axis is scored 0–10 (10 = best). Scores are based on the current QML *only*, not the mockup.

| #   | Axis                               | Score | One-line rationale                                                                                                                                                                                                                                                                                                                 |
| --- | ---------------------------------- | ----: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Visual coherence                   | **3** | Every file picks its own grey: `#14141a`, `#1e1e23`, `#1a1d24`, `#24242a`, `#2a2a32`, `#2c2c34`. No shared spec.                                                                                                                                                                                                                   |
| 2   | Information hierarchy              | **3** | No primary/secondary button distinction; tab bar undecorated; selection state for keys/devices not rendered.                                                                                                                                                                                                                       |
| 3   | Spacing & grid                     | **4** | Mostly multiples of 4 but inconsistent: `margins: 8/10/16`, `spacing: 2/4/12`. 10 px breaks the 4/8 scale.                                                                                                                                                                                                                         |
| 4   | Component reuse                    | **1** | Zero shared components. `Rectangle{radius;color;border}` is duplicated across `KeyDesigner`, `EncoderPanel`, `DeviceList`.                                                                                                                                                                                                         |
| 5   | Empty / error / loading states     | **2** | Single placeholder ("Select a device on the left") in `ProfileEditor`. No empty-device-list, no loading, no error toasts.                                                                                                                                                                                                          |
| 6   | Keyboard navigation & focus states | **1** | `MouseArea` is used instead of `Button`; no `Keys.onPressed`, no `activeFocusOnTab`, no visible focus ring.                                                                                                                                                                                                                        |
| 7   | Accessibility                      | **1** | No `Accessible.role`/`name`/`description`. `#888` on `#1e1e23` ≈ 4.0 : 1, fails WCAG AA for the 11 px caption.                                                                                                                                                                                                                     |
| 8   | Internationalization readiness     | **6** | `qsTr()` used for static labels (15 instances). Combo-box items (`"Static"`, `"Breathing"`, etc.) are *not* wrapped.                                                                                                                                                                                                               |
| 9   | Responsiveness                     | **2** | Sidebar is fixed at 320 px; key grid uses `centerIn:parent` with hard-coded 96 × 96 cells — won't reflow.                                                                                                                                                                                                                          |
| 10  | Dark-mode quality                  | **5** | Only a dark theme exists; some greys are too close (`#24242a` row vs `#1e1e23` sidebar Δ ≈ 6 in luminance) so rows nearly disappear.                                                                                                                                                                                               |
| 11  | Branding integration               | **1** | `BrandingService` exposes 7 color roles + product name; only `Main.qml` uses any of them. Every other file ignores `branding.*`. This directly contradicts the [BRANDING.md](../architecture/BRANDING.md) claim that "QML reads from `branding.accent`, `branding.bgSidebar`, etc. — no hard-coded hex anywhere in the QML files." |
| 12  | Iconography                        | **0** | No icons rendered anywhere in the live QML. Mockup shows them but no `IconImage`/SVG path exists in code.                                                                                                                                                                                                                          |

**Weighted average: 2.75 / 10** — i.e. the QML is functional scaffolding, not a shippable UI. The mockup is several refactors ahead.

______________________________________________________________________

## 2. Per-screen review

### 2.1 `Main.qml`

```qml
// src/app/qml/Main.qml:13-20
width: 1280
height: 800
visible: true
title: branding ? branding.productName : qsTr("AJAZZ Control Center")
color: branding ? branding.bgBase : "#1a1d24"
```

**Findings**

- ✅ Uses `branding.productName` and `branding.bgBase` correctly with a literal fallback. Good pattern.
- ⚠️ The fallback `#1a1d24` does not match the BrandingService default `#14141a` (`branding_service.cpp:188`). They drift.
- ⚠️ `width: 1280; height: 800` are not declared as minimums (`minimumWidth`/`minimumHeight`). The window can be resized smaller than the layout breakpoints.
- ⚠️ Sidebar width hard-coded to 320 px; should be a theme constant.
- ⚠️ No `menuBar`, no `header`/title-bar widget — competitors (Stream Deck, Synapse 4) all have a top-bar with profile selector + global search ([Razer Synapse 4 review, Windows Central](https://www.windowscentral.com/software-apps/razer-synapse-4-launch)).

**Fix**

```qml
ApplicationWindow {
    id: root
    minimumWidth:  900     // Theme.breakpoint.md
    minimumHeight: 600
    width:  1280
    height: 800
    visible: true
    title: branding.productName
    color:  branding.bgBase

    header: AppHeader { /* product mark, profile selector, search */ }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        DeviceList {
            Layout.preferredWidth: Theme.sidebarWidth   // 320
            Layout.minimumWidth:   Theme.sidebarMinWidth // 240
            Layout.fillHeight: true
            ...
        }
        ...
    }
}
```

______________________________________________________________________

### 2.2 `DeviceList.qml`

```qml
// src/app/qml/DeviceList.qml:10-12
Rectangle {
    id: root
    color: "#1e1e23"          // FINDING: hard-coded; should be branding.bgSidebar
```

```qml
// src/app/qml/DeviceList.qml:25-49
delegate: Rectangle {
    width:  list.width
    height: 56
    radius: 6
    color: ma.containsMouse ? "#2c2c34" : "#24242a"   // FINDING: literal; no selected state
    Column {
        anchors.fill: parent
        anchors.margins: 10                            // FINDING: 10 breaks 4/8 grid
        spacing: 2                                     // FINDING: 2 breaks scale
        Text { text: parent.parent.model
               color: "#f0f0f0"; font.pixelSize: 14 }  // FINDING: fragile parent.parent
        Text { text: "%1 · %2".arg(parent.parent.codename)
                              .arg(parent.parent.connected ? "connected" : "offline")
               color: "#888"; font.pixelSize: 11 }     // FINDING: 11px + #888 = ≈4:1 contrast — fails WCAG AA
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true
                onClicked: root.deviceSelected(parent.codename) }  // FINDING: no keyboard
}
```

**Findings**

- No selected state — the user can't see which device is currently being edited.
- "connected / offline" is a string — should be a colored dot + text for at-a-glance scanning, like [G HUB's tile view](https://www.logitechg.com/en-us/software/guides/g-hub-basics).
- No grouping. With 10+ devices the rail becomes a flat list. Stream Deck/iCUE group by family ([Corsair iCUE overview](https://www.corsair.com/us/en/explorer/gamer/mice/corsair-icue-everything-you-need-to-know/)).
- No empty state ("No AJAZZ devices detected — plug one in or click *Refresh*").
- No icons — even the mockup has a small device thumbnail.
- Strings `"connected"`, `"offline"` are not wrapped in `qsTr()`.
- Uses `MouseArea` not `ItemDelegate`; therefore no Tab-focus, no `Accessible.role: "listitem"`.
- Image fetched as `parent.parent.model` (line 41) — relies on QML's brittle ancestor lookup; will break if the row gets a wrapper.

**Fix sketch**

```qml
// DeviceList.qml — refactor uses Theme + DeviceRow component
ListView {
    id: list
    anchors.fill: parent
    anchors.margins: Theme.space.s2          // 8
    spacing: Theme.space.s1                   // 4
    section.property: "family"
    section.delegate: SectionHeader { text: family }   // groups by family
    delegate: DeviceRow {
        deviceName: model.model
        codename:   model.codename
        connected:  model.connected
        selected:   list.currentIndex === index
        onClicked:  { list.currentIndex = index;
                      root.deviceSelected(model.codename) }
        Accessible.role: Accessible.ListItem
        Accessible.name: deviceName
        Accessible.description: connected
            ? qsTr("%1, connected").arg(codename)
            : qsTr("%1, offline").arg(codename)
    }
}
```

______________________________________________________________________

### 2.3 `ProfileEditor.qml`

```qml
// src/app/qml/ProfileEditor.qml:11-29
Rectangle {
    color: "#14141a"             // FINDING: literal; should be branding.bgBase
    ColumnLayout {
        anchors.margins: 16      // OK (8 grid)
        spacing: 12               // FINDING: 12 not on 8-grid (use 8 or 16)
        Label {
            text: root.codename === ""
                ? qsTr("Select a device on the left")
                : qsTr("Editing: %1").arg(root.codename)
            color: "#e0e0e0"     // FINDING: literal; close to but ≠ branding.fgPrimary "#f0f0f0"
            font.pixelSize: 20    // FINDING: magic number; no type scale
        }
        TabBar { ... }
    }
}
```

**Findings**

- Title `Label` doubles as both **page title** and **empty-state hint**. These should be separate states with different layouts (icon + helper text in the empty state).
- `TabBar` is unstyled — Qt's default TabBar in dark mode shows light grey on light grey.
- The right-side **inspector panel** (Action / Icon / Label / Press behavior) shown in the mockup is *missing entirely* from the live QML. The code shows the entire central area as just the key grid.
- StackLayout inside ColumnLayout: the four child views (KeyDesigner / RgbPicker / EncoderPanel / MousePanel) are always *constructed* even when invisible. Memory + bind-loop cost. Use `Loader { active: tabs.currentIndex === N; sourceComponent: ... }`.
- All four sub-tabs are shown for every device, but only AKP03/05/153 have keys; only keyboards have RGB; only mice have a mouse panel. Tabs should be **conditional on the device family** (`capabilities` from `core/capabilities.hpp`).

**Fix sketch**

```qml
ColumnLayout {
    anchors.fill: parent
    anchors.margins: Theme.space.s4   // 32 (matches mockup breathing room)
    spacing: Theme.space.s3            // 24

    PageHeader {                       // new component
        title: root.codename === ""
            ? qsTr("Welcome")
            : qsTr("Editing: %1").arg(deviceLabel(root.codename))
        subtitle: deviceFamilyLabel(root.codename)
    }

    // Empty state branch
    Loader {
        active: root.codename === ""
        sourceComponent: EmptyState {
            iconSource: "qrc:/icons/devices.svg"
            title:      qsTr("Select a device on the left")
            message:    qsTr("Plug in an AJAZZ device or pick one from the sidebar to start editing.")
        }
    }

    // Tabs (only when a device is selected)
    Loader {
        active: root.codename !== ""
        sourceComponent: ColumnLayout {
            spacing: Theme.space.s3
            ProfileTabs { id: tabs; capabilities: deviceCapabilities }
            StackLayout { /* loader-backed, tabs.currentIndex */ }
        }
    }
}
```

______________________________________________________________________

### 2.4 `KeyDesigner.qml`

```qml
// src/app/qml/KeyDesigner.qml:11-36
Item {
    GridLayout {
        anchors.centerIn: parent          // FINDING: centers — won't scale
        columns: 5
        rowSpacing: 8; columnSpacing: 8   // OK
        Repeater {
            model: 15                      // FINDING: hard-coded; should come from device capabilities
            delegate: Rectangle {
                width: 96; height: 96      // FINDING: fixed pixel size
                radius: 8
                color: "#2a2a32"           // FINDING: literal
                border.color: "#3a3a44"    // FINDING: literal
                border.width: 1
                Text {
                    text: parent.index + 1 // FINDING: shows 1..15, no key label/icon/binding
                    color: "#aaa"
                    font.pixelSize: 18
                }
            }
        }
    }
}
```

**Findings**

- `model: 15` is hard-coded — AKP03 has 6 keys, AKP05 has 15, AKP153 has 15 + screen. Should be driven by `device.capabilities.keyCount`.
- No selected state, no hover state, no icon, no label, no binding → the cell is essentially decoration.
- The mockup shows a 3 × 2 layout for 6 keys with *highlighted* selected key, *icon*, *label*, *glow on hover*. Live code has none of that.
- No drag-and-drop. Stream Deck users expect to drag actions onto keys ([r/elgato Stream Deck UX feedback](https://www.reddit.com/r/elgato/comments/1c92tyt/feedback_on_the_stream_deck_66_software_first/)).
- No right-side inspector to actually configure the key after selection.
- Click events not wired (no `MouseArea`, no `TapHandler`).
- No keyboard navigation (cannot tab between cells).

**Fix sketch**

```qml
Item {
    id: root
    property var device
    property int selectedIndex: -1

    GridLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.s4
        columns: device ? device.gridColumns : 5
        rowSpacing: Theme.space.s2
        columnSpacing: Theme.space.s2
        Repeater {
            model: device ? device.keyCount : 0
            delegate: KeyCell {
                key: device.keys[index]
                selected: root.selectedIndex === index
                onClicked: root.selectedIndex = index
                Keys.onSpacePressed: clicked()
                Accessible.role: Accessible.Button
                Accessible.name: key.label || qsTr("Key %1").arg(index + 1)
            }
        }
    }
}
```

______________________________________________________________________

### 2.5 `RgbPicker.qml`

```qml
// src/app/qml/RgbPicker.qml:13-33
ColumnLayout {
    anchors.fill: parent
    spacing: 12                                     // FINDING: not on grid
    Label { text: qsTr("RGB effect"); color: "#ccc" }
    ComboBox {
        Layout.fillWidth: true
        model: [ "Static", "Breathing", "Wave",
                 "Reactive", "Cycle", "Custom" ]    // FINDING: not qsTr()
    }
    Label { text: qsTr("Brightness"); color: "#ccc" }
    Slider { from: 0; to: 100; value: 80; Layout.fillWidth: true }
    // FINDING: no value display next to slider
    Label { text: qsTr("Color"); color: "#ccc" }
    Rectangle { width: 64; height: 64; radius: 6
                color: "#ff5722" }                  // FINDING: hard-coded orange; not branding.accent
}
```

**Findings**

- The "color" swatch is a static `#ff5722` rectangle, not an actual color picker. There is no HSV wheel, no hex input, no eyedropper. Even OpenRGB — a famously minimalist UI — exposes both an RGB and a HSV picker ([OpenRGB site](https://openrgb.org/)).
- No per-key vs. global lighting toggle.
- No live preview on the device silhouette (Corsair iCUE's hallmark feature, [iCUE overview](https://www.corsair.com/us/en/s/icue)).
- All three labels use `#ccc` directly — should be `branding.fgMuted` (the BrandingService default `#888888` is too dim, but that's a theme problem, not a hard-code excuse).
- Effect names not localized.

**Fix sketch**

```qml
ColumnLayout {
    anchors.fill: parent
    anchors.margins: Theme.space.s4
    spacing: Theme.space.s3

    FormField {
        label: qsTr("RGB effect")
        ComboBox {
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "static",    label: qsTr("Static") },
                { id: "breathing", label: qsTr("Breathing") },
                { id: "wave",      label: qsTr("Wave") },
                { id: "reactive",  label: qsTr("Reactive") },
                { id: "cycle",     label: qsTr("Cycle") },
                { id: "custom",    label: qsTr("Custom") }
            ]
        }
    }
    FormField {
        label: qsTr("Brightness")
        RowLayout {
            Slider { id: brightness; from: 0; to: 100; value: 80
                     Layout.fillWidth: true }
            Label { text: Math.round(brightness.value) + "%"
                    color: branding.fgPrimary
                    Layout.preferredWidth: 48 }
        }
    }
    FormField {
        label: qsTr("Color")
        ColorPicker { /* extracted */ value: branding.accent }
    }
    Item { Layout.fillHeight: true }
}
```

______________________________________________________________________

### 2.6 `EncoderPanel.qml`

```qml
// src/app/qml/EncoderPanel.qml:18-31
Repeater {
    model: 4                                        // FINDING: hard-coded
    delegate: Rectangle {
        width: 120; height: 120; radius: 60         // OK (circle)
        color: "#2a2a32"                            // FINDING: literal
        border.color: "#ff5722"                     // FINDING: literal — orange ≠ branding.accent (green by default)
        border.width: 2
        Text {
            text: qsTr("Dial %1").arg(parent.index + 1)
            color: "#e0e0e0"                        // FINDING: literal
        }
    }
}
```

**Findings**

- The orange `#ff5722` outline is the only place in the app that *looks* accent-colored, but it is hard-coded and disagrees with the BrandingService default accent (`#41CD52` Qt green).
- Each encoder has only an ordinal label. Mockup shows an encoder card with **CW / CCW / Press** binding rows + "Volume Up/Down" preset → no UI for those bindings exists.
- No layout for variable encoder count (some devices have 1, others have 4).
- `parent.index` is brittle — should be `model.index` or `index`.

**Fix sketch**

```qml
GridLayout {
    anchors.fill: parent
    anchors.margins: Theme.space.s4
    columns: Math.max(1, Math.floor(width / 280))
    columnSpacing: Theme.space.s3
    rowSpacing: Theme.space.s3
    Repeater {
        model: device ? device.encoderCount : 0
        delegate: EncoderCard {
            encoderIndex: index
            cwAction:    profile.encoders[index].cw
            ccwAction:   profile.encoders[index].ccw
            pressAction: profile.encoders[index].press
        }
    }
}
```

______________________________________________________________________

### 2.7 `MousePanel.qml`

```qml
// src/app/qml/MousePanel.qml:12-33
ColumnLayout {
    anchors.fill: parent
    spacing: 12                                     // FINDING: not on grid
    Label { text: qsTr("DPI stages"); color: "#ccc" }
    Repeater {
        model: 6                                    // FINDING: 6 always; should follow device
        delegate: RowLayout {
            Label { text: qsTr("Stage %1").arg(parent.index + 1); color: "#aaa" }
            SpinBox {
                from: 100; to: 26000; stepSize: 100
                value: 800 + parent.index * 400      // FINDING: bogus magic seed
            }
        }
    }
    Label { text: qsTr("Polling rate (Hz)"); color: "#ccc" }
    ComboBox { model: [ 125, 250, 500, 1000, 2000, 4000, 8000 ] }
    Label { text: qsTr("Lift-off distance (mm)"); color: "#ccc" }
    Slider { from: 1.0; to: 2.0; stepSize: 0.1; value: 1.5 }
    // FINDING: no value display
    Item { Layout.fillHeight: true }
}
```

**Findings**

- Form-field grouping is implicit (Label-then-control). A reusable `FormField` would solve consistency *and* alignment in one go.
- No "Active stage" toggle — Razer Synapse and G HUB both let users set how many of the N stages are *active*.
- DPI stages should be color-coded so they correspond to the LED indicator on the mouse (G HUB convention — [G HUB Basics](https://www.logitechg.com/en-us/software/guides/g-hub-basics)).
- Numeric units (`Hz`, `mm`) appear in label text only, not next to the value control. International users with different formatters may be confused.
- No "Restore defaults" button anywhere.

______________________________________________________________________

## 3. Hard-coded colors / typography audit

### 3.1 Hard-coded color literals (12 unique values, 18 sites)

| File:line                 | Literal              | Should map to                         | Notes                                           |
| ------------------------- | -------------------- | ------------------------------------- | ----------------------------------------------- |
| `Main.qml:20`             | `#1a1d24` (fallback) | `branding.bgBase` (default `#14141a`) | Fallback drifts from real default.              |
| `DeviceList.qml:12`       | `#1e1e23`            | `branding.bgSidebar`                  | Currently matches default but ignores override. |
| `DeviceList.qml:34`       | `#2c2c34` (hover)    | `branding.bgRowHover`                 | Direct mapping exists.                          |
| `DeviceList.qml:34`       | `#24242a` (idle)     | *(missing role)* `bgRow`              | Add new role to BrandingService.                |
| `DeviceList.qml:42`       | `#f0f0f0`            | `branding.fgPrimary`                  |                                                 |
| `DeviceList.qml:48`       | `#888`               | `branding.fgMuted`                    |                                                 |
| `EncoderPanel.qml:23`     | `#2a2a32`            | *(missing role)* `bgCard`             | Add card surface role.                          |
| `EncoderPanel.qml:24`     | `#ff5722`            | `branding.accent`                     | Orange; default accent is green. Misleading.    |
| `EncoderPanel.qml:29`     | `#e0e0e0`            | `branding.fgPrimary`                  | Slightly different shade than `#f0f0f0`.        |
| `KeyDesigner.qml:25`      | `#2a2a32`            | `bgCard`                              | Same as encoder — deduplicate.                  |
| `KeyDesigner.qml:26`      | `#3a3a44`            | *(missing role)* `borderSubtle`       | Add.                                            |
| `KeyDesigner.qml:31`      | `#aaa`               | `branding.fgMuted`                    | Yet another grey.                               |
| `MousePanel.qml:16,29,32` | `#ccc`               | `branding.fgMuted`                    | Three sites.                                    |
| `MousePanel.qml:21`       | `#aaa`               | `branding.fgMuted`                    |                                                 |
| `ProfileEditor.qml:16`    | `#14141a`            | `branding.bgBase`                     |                                                 |
| `ProfileEditor.qml:27`    | `#e0e0e0`            | `branding.fgPrimary`                  |                                                 |
| `RgbPicker.qml:17,23,29`  | `#ccc`               | `branding.fgMuted`                    |                                                 |
| `RgbPicker.qml:32`        | `#ff5722`            | (state value)                         | Should be a bound color, not a literal.         |

**Distinct foreground greys in use:** `#f0f0f0`, `#e0e0e0`, `#ccc`, `#aaa`, `#888` — five shades of grey for what should be two roles (`fgPrimary`, `fgMuted`). The eye reads this as visual chaos.

**Missing branding roles to add:** `bgRow`, `bgCard`, `borderSubtle`, `accentMuted` (for unfocused selection), `success`, `warning`, `danger`. Without these, every component will keep inventing its own.

### 3.2 Hard-coded font sizes (4 unique values, 4 sites)

| File:line              |  px | Suggested token                               |
| ---------------------- | --: | --------------------------------------------- |
| `DeviceList.qml:43`    |  14 | `Theme.font.body` (14)                        |
| `DeviceList.qml:49`    |  11 | `Theme.font.caption` (12 — bump to pass WCAG) |
| `KeyDesigner.qml:32`   |  18 | `Theme.font.h3` (18)                          |
| `ProfileEditor.qml:28` |  20 | `Theme.font.h2` (20)                          |

The 11 px caption combined with `#888` foreground is the worst contrast offender (≈4 : 1; WCAG AA needs ≥ 4.5 : 1 for normal text).

### 3.3 Hard-coded spacing (8 sites)

| File:line              |  px | On 8-grid? | Suggested                         |
| ---------------------- | --: | :--------: | --------------------------------- |
| `DeviceList.qml:23`    |   8 |     ✓      | `Theme.space.s2`                  |
| `DeviceList.qml:24`    |   4 | ✓ (4-grid) | `Theme.space.s1`                  |
| `DeviceList.qml:38`    |  10 |     ✗      | `Theme.space.s2` (8) or `s3` (16) |
| `DeviceList.qml:39`    |   2 |     ✗      | `Theme.space.s1` (4)              |
| `ProfileEditor.qml:20` |  16 |     ✓      | `Theme.space.s3`                  |
| `ProfileEditor.qml:21` |  12 |     ✗      | `Theme.space.s3` (16)             |
| `MousePanel.qml:14`    |  12 |     ✗      | `Theme.space.s3` (16)             |
| `RgbPicker.qml:15`     |  12 |     ✗      | `Theme.space.s3` (16)             |

Per the [8-point grid convention used by Material/Carbon](https://uxplanet.org/everything-you-should-know-about-8-point-grid-system-in-ux-design-b69cb945b18d), 10 / 12 / 2 are off-grid and should be normalized to 4 / 8 / 16 / 24 / 32.

______________________________________________________________________

## 4. Recurring components needing extraction

The QML currently has **zero shared QML components**. Every visual idea is reinvented per file. Here is the proposed component library, ordered by ROI:

| File to add                                 | Replaces (current sites)                                                  | Why                                                                                                                                                                                 |
| ------------------------------------------- | ------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Theme.qml` (singleton, `pragma Singleton`) | All literal colors / font sizes / spacings                                | Single source of truth. Reads from `branding` and adds derived tokens (e.g. `bgCard`, `borderSubtle`, font scale, spacing scale, radii, durations). **Highest ROI; do this first.** |
| `Card.qml`                                  | `KeyDesigner.qml:20-34`, `EncoderPanel.qml:20-31`, `DeviceList.qml:25-58` | All three render a rounded rectangle with hover/selected states.                                                                                                                    |
| `IconButton.qml`                            | (currently absent)                                                        | For the icon + label keys in the mockup, and toolbar actions.                                                                                                                       |
| `PrimaryButton.qml` / `SecondaryButton.qml` | (currently absent)                                                        | Establishes the primary-vs-secondary action hierarchy.                                                                                                                              |
| `FormField.qml`                             | `RgbPicker` and `MousePanel` Label-then-Control pairs                     | Standardizes label position, gap, spacing, focus link.                                                                                                                              |
| `LabeledSlider.qml`                         | `RgbPicker.qml:24`, `MousePanel.qml:33`                                   | Slider + live numeric readout + unit suffix.                                                                                                                                        |
| `ColorPicker.qml`                           | `RgbPicker.qml:30-33`                                                     | Real picker (HSV wheel + hex input + eyedropper) instead of a static swatch.                                                                                                        |
| `KeyCell.qml`                               | `KeyDesigner.qml:20-34`                                                   | Square, tappable, icon + label, hover/focus/selected states.                                                                                                                        |
| `EncoderCard.qml`                           | `EncoderPanel.qml:20-31`                                                  | Round dial visual + CW/CCW/Press binding rows.                                                                                                                                      |
| `DeviceRow.qml`                             | `DeviceList.qml:25-58`                                                    | Sidebar row delegate; selected/hover/disabled/offline states.                                                                                                                       |
| `EmptyState.qml`                            | `ProfileEditor.qml:23-29`                                                 | Centered icon + headline + body + optional CTA.                                                                                                                                     |
| `PageHeader.qml`                            | `ProfileEditor.qml:23-29`                                                 | Title + subtitle + optional action button bar.                                                                                                                                      |
| `Toast.qml` / `Banner.qml`                  | (currently absent)                                                        | Required to surface success / error from `ProfileController` writes.                                                                                                                |
| `SectionHeader.qml`                         | (currently absent)                                                        | Sidebar group dividers ("Stream Decks" / "Keyboards" / "Mice").                                                                                                                     |
| `AppHeader.qml`                             | (currently absent)                                                        | Top app bar with product mark, profile selector, search, settings.                                                                                                                  |
| `Inspector.qml`                             | (currently absent — present only in mockup)                               | Right-rail editor for the selected key/encoder.                                                                                                                                     |

A minimal `Theme.qml` singleton (sketch):

```qml
// src/app/qml/Theme.qml
pragma Singleton
import QtQuick

QtObject {
    // colors — read from BrandingService, fall back to defaults
    readonly property color bgBase:      branding.bgBase
    readonly property color bgSidebar:   branding.bgSidebar
    readonly property color bgRow:       Qt.lighter(branding.bgSidebar, 1.10)
    readonly property color bgRowHover:  branding.bgRowHover
    readonly property color bgCard:      Qt.lighter(branding.bgBase, 1.30)
    readonly property color borderSubtle:Qt.rgba(1, 1, 1, 0.08)
    readonly property color fgPrimary:   branding.fgPrimary
    readonly property color fgMuted:     branding.fgMuted
    readonly property color accent:      branding.accent
    readonly property color accent2:     branding.accent2
    readonly property color success:     "#3CCB7F"
    readonly property color warning:     "#F0B429"
    readonly property color danger:      "#E5484D"

    // typography
    readonly property QtObject font: QtObject {
        readonly property int caption:  12
        readonly property int body:     14
        readonly property int bodyL:    16
        readonly property int h3:       18
        readonly property int h2:       20
        readonly property int h1:       28
    }

    // spacing — strict 4/8 scale
    readonly property QtObject space: QtObject {
        readonly property int s0: 0
        readonly property int s1: 4
        readonly property int s2: 8
        readonly property int s3: 16
        readonly property int s4: 24
        readonly property int s5: 32
        readonly property int s6: 48
    }

    // radii
    readonly property QtObject radius: QtObject {
        readonly property int sm: 4
        readonly property int md: 8
        readonly property int lg: 12
    }

    // motion
    readonly property int durationFast: 120
    readonly property int durationBase: 200

    // breakpoints
    readonly property int sidebarWidth:    320
    readonly property int sidebarMinWidth: 240
    readonly property int inspectorWidth:  320
}
```

______________________________________________________________________

## 5. Information architecture suggestions

1. **Global header (new)** — product mark on the left, profile picker in the center, search + settings + minimize-to-tray on the right. Stream Deck, [Synapse 4](https://www.windowscentral.com/software-apps/razer-synapse-4-launch) and iCUE all anchor navigation here.
1. **Sidebar grouping by family** — the sidebar should section by family (Stream Decks / Keyboards / Mice) using `ListView.section.delegate`. Categories collapse, devices within sort by connection state. With ten supported devices today and more planned, a flat list is already cramped.
1. **Connection-state visual** — a 6 × 6 dot prefix (green/grey) is more scannable than the trailing `· connected` string.
1. **Three-pane layout for editing screens** — sidebar (devices) | canvas (key grid / encoder grid / silhouette) | inspector (per-element binding). This matches Stream Deck's canvas + properties model and is what the mockup actually shows.
1. **Capability-driven tabs** — `KeyDesigner / RgbPicker / EncoderPanel / MousePanel` should not all be visible for every device. Read `device.capabilities` and hide irrelevant tabs (e.g. AKP03 has no mouse panel, AJ-MX has no RGB tab unless lit).
1. **Profiles as first-class** — currently no UI for switching profiles. Add a profile dropdown to the header (matches every competitor). Profile-per-app smart switching is Stream Deck's killer feature ([Elgato Stream Deck product page](https://www.elgato.com/ww/en/p/stream-deck)).
1. **Settings location** — there is no settings panel. Tray + theme + plugins + updates need a page; `Ctrl+,` should open it.

______________________________________________________________________

## 6. Empty states + onboarding

The current QML has **one** empty state ("Select a device on the left") and **zero** onboarding. The first launch experience for an alpha-stage open-source app is critical.

**Missing first-launch states:**

- `DeviceList`: when `model.count === 0` show "No AJAZZ devices detected." + small "USB" icon + "Plug in a device — the list updates automatically. Or, [view supported devices →]". Today the sidebar is just an empty rectangle.
- `ProfileEditor` (no codename): currently shows a tab bar with no content. Should show a friendly EmptyState with a hardware silhouette and a button "View getting-started guide" (deep-link to `docs/wiki/Quick-Start.md`).
- `KeyDesigner`: when no key is selected, the inspector should explain "Click a key to assign an action." Today there is no inspector.
- Loading: when ProfileController is reading the firmware, no spinner / skeleton.
- Error: when a write fails (USB unplugged mid-write), nothing is rendered. Need a non-modal toast and an inline banner on the affected control.
- Permission errors (Linux udev rules missing): need a dedicated state with "Run `scripts/install-udev.sh`" instructions, since this is the #1 first-run failure for HID apps.

**Recommended first-launch flow:**

1. **Splash** (already supported by branding `splash.png` via `BRANDING.md`).
1. **Welcome dialog** — three steps: identify devices → choose theme → import profiles (skippable).
1. **Tooltip tour on first device select** — 3 tooltips: "Tabs let you switch between Keys/RGB/Encoders/Mouse", "Click a key to bind it", "Use the profile picker (top) to switch contexts".
1. **Persistent dismissible banner** if udev rules / driver missing.

______________________________________________________________________

## 7. Comparison matrix vs. competitors

| Pattern                                             |                                                   Elgato Stream Deck                                                    |                   Corsair iCUE                    |                                               Razer Synapse 4                                               |                                      Logitech G HUB                                       |      OpenRGB       |                                                                         **Adopt for AJAZZ?**                                                                          |
| --------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------: | :---------------------------------------------------------------------------------------------------------: | :---------------------------------------------------------------------------------------: | :----------------: | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------: |
| Three-pane edit (sidebar / canvas / inspector)      |                                                           ✅                                                            |                        ✅                         |                                                     ✅                                                      |                                     partial (overlay)                                     |         ❌         |                                                            **Yes** — closest fit for stream-deck use case                                                             |
| Top header w/ global search & profile picker        |                                                           ✅                                                            |                        ✅                         |            ✅ ([Synapse 4](https://www.windowscentral.com/software-apps/razer-synapse-4-launch))            |                                            ✅                                             |         ❌         |                                                                                **Yes**                                                                                |
| Sidebar grouped by device family                    |                                                         partial                                                         |                        ✅                         |                                                     ✅                                                      | ✅ ([G HUB tile/list view](https://www.logitechg.com/en-us/software/guides/g-hub-basics)) |        flat        |                                                                                **Yes**                                                                                |
| Drag-and-drop actions onto keys                     |                                                    ✅ (core feature)                                                    |                        n/a                        |                                                     n/a                                                     |                                            n/a                                            |        n/a         |                                                                    **Yes** for stream-deck devices                                                                    |
| Per-app smart profiles                              |                                ✅ ([Elgato](https://www.elgato.com/ww/en/p/stream-deck))                                |                        ✅                         |                                                     ✅                                                      |                                            ✅                                             |         ❌         |                                                                         **Yes** (ship later)                                                                          |
| Live device silhouette w/ RGB preview               |                                                         partial                                                         | ✅ ([iCUE](https://www.corsair.com/us/en/s/icue)) |                                                     ✅                                                      |                                            ✅                                             |         ❌         |                                                                         **Yes** (iCUE-style)                                                                          |
| Plugin marketplace inside the app                   |                                                           ✅                                                            |                        ✅                         |                                              partial (Modules)                                              |                                          partial                                          |         ❌         |                                                          **Eventually** — already have Python plugin system                                                           |
| Heavy bright accent bar (old Synapse green)         |                                                           ❌                                                            |                        ❌                         | ❌ (removed in v4 — [Windows Central](https://www.windowscentral.com/software-apps/razer-synapse-4-launch)) |                                            ❌                                             |         ❌         |                                                                  **Reject** — keep accent restrained                                                                  |
| Modular separate apps (Synapse / Chroma / Axen)     |                                                           ❌                                                            |                        ❌                         |                                                     ✅                                                      |                                            ❌                                             |         ❌         |                                                               **Reject** — fragmentation hated by users                                                               |
| Bundled bloat (game library, news, vouchers)        |                                                           ❌                                                            |                        ❌                         |                                                     ❌                                                      |                                            ❌                                             |         ❌         | **Reject** — Armoury Crate is universally panned for this ([r/ASUS](https://www.reddit.com/r/ASUS/comments/1j4qjqj/installing_the_latest_armory_crate_is_confusing/)) |
| Mandatory cloud account                             |                                                           ❌                                                            |                     optional                      |                                                  optional                                                   |                                            ✅                                             |         ❌         |                                                            **Reject** — open-source positioning forbids it                                                            |
| Tray-only run-time mode                             |                                                           ✅                                                            |                        ✅                         |                                                     ✅                                                      |                                            ✅                                             |         ✅         |                                                 **Already supported** (`tray.startMinimized`) — surface in onboarding                                                 |
| Pure dark theme only                                |                                                    ❌ (light + dark)                                                    |                        ❌                         |                                               ✅ (dark only)                                                |                                            ❌                                             |      partial       |                                            **Add light variant** — `BrandingService` already supports it via `theme.json`.                                            |
| List-vs-tile device view toggle                     |                                                           ❌                                                            |                        ✅                         |                                                     ❌                                                      |     ✅ ([G HUB Basics](https://www.logitechg.com/en-us/software/guides/g-hub-basics))     |         ❌         |                                                                           **Nice-to-have**                                                                            |
| Lightweight footprint (no telemetry)                |                                                           ❌                                                            |                    ❌ (heavy)                     |                 better in v4 ([PCGamesN](https://www.pcgamesn.com/razer/synapse-4-update))                  |                                            ❌                                             | ✅ (selling point) |                                                            **Yes** — match OpenRGB's "lightweight" promise                                                            |
| Text-rendered icons inside keys (Stream Deck-style) | ✅ ([feedback thread](https://www.reddit.com/r/elgato/comments/1c92tyt/feedback_on_the_stream_deck_66_software_first/)) |                        n/a                        |                                                     n/a                                                     |                                            n/a                                            |        n/a         |                                                                        **Yes** for AKP devices                                                                        |

**Adopt:** three-pane edit, capability-driven tabs, header w/ profile picker, family-grouped sidebar, drag-and-drop key assignment, per-app smart profiles (later), iCUE-style live silhouette, restrained accent.

**Reject:** Razer-style modular split, Armoury Crate-style content bundling, mandatory accounts, telemetry, neon-accent bars.

______________________________________________________________________

## 8. Findings table

Severity scale: **Critical** = ship-blocker / accessibility-fail, **High** = major UX pain, **Medium** = polish, **Low** = nit. Effort: **S** ≤ 1 day, **M** ≤ 1 week, **L** > 1 week.

| id   | category       | severity | file:line                                                                                                                                                              | description                                                                                                                                     | suggested_fix                                                                                                                                              | effort |
| ---- | -------------- | :------: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- | :----: |
| F-01 | branding       | Critical | `DeviceList.qml:12,34,42,48` `EncoderPanel.qml:23,24,29` `KeyDesigner.qml:25,26,31` `MousePanel.qml:16,21,29,32` `ProfileEditor.qml:16,27` `RgbPicker.qml:17,23,29,32` | 18 hard-coded color literals; `BrandingService` is bypassed everywhere except `Main.qml`. Directly contradicts `docs/architecture/BRANDING.md`. | Introduce `Theme.qml` singleton, replace every literal with `Theme.bg…`/`Theme.fg…`/`Theme.accent`.                                                        |   M    |
| F-02 | components     | Critical | all files                                                                                                                                                              | No shared QML components — every screen redefines rectangles, text styles, spacing. Forks of branding will compound the problem.                | Build component library (Card, KeyCell, EncoderCard, DeviceRow, FormField, ColorPicker, EmptyState, PageHeader, Toast).                                    |   L    |
| F-03 | accessibility  | Critical | `DeviceList.qml:48-49`                                                                                                                                                 | 11 px text in `#888` on `#1e1e23` ≈ 4.0 : 1 contrast — fails WCAG 2.1 AA (≥ 4.5 : 1).                                                           | Use `Theme.font.caption` (12 px) and `Theme.fgMuted` (`#aaaaaa` minimum) to reach 4.6 : 1.                                                                 |   S    |
| F-04 | accessibility  |   High   | `DeviceList.qml:53` `KeyDesigner.qml:11` `EncoderPanel.qml:11`                                                                                                         | Interactive elements use `MouseArea`; no keyboard focus, no `Accessible.role`/`name`/`description`, no Tab navigation, no visible focus ring.   | Replace `MouseArea` with `ItemDelegate`/`Button`. Add `Accessible.*` properties. Render a 2 px focus ring with `Theme.accent` on `activeFocus`.            |   M    |
| F-05 | hierarchy      |   High   | `ProfileEditor.qml:31-38`                                                                                                                                              | TabBar is unstyled — selected tab is barely distinguishable from inactive.                                                                      | Add custom `TabButton` style (active: accent underline + `Theme.fgPrimary`; inactive: `Theme.fgMuted`).                                                    |   S    |
| F-06 | empty states   |   High   | `KeyDesigner.qml`, `RgbPicker.qml`, `EncoderPanel.qml`, `MousePanel.qml`                                                                                               | No empty/loading/error states inside any sub-tab. When backend is wired, missing or failed reads will render blank screens.                     | Build `EmptyState`, `Skeleton`, `Toast`/`Banner`. Wire to `ProfileController` signals.                                                                     |   M    |
| F-07 | hierarchy      |   High   | `ProfileEditor.qml:40-48`                                                                                                                                              | Right-side **Inspector panel** (Action / Icon / Label / Press behavior) shown in mockup is missing. The user has no way to *configure* a key.   | Add `Inspector.qml` as third pane in `ProfileEditor`; bind to selected element.                                                                            |   L    |
| F-08 | components     |   High   | `KeyDesigner.qml:19`                                                                                                                                                   | `model: 15` is hard-coded. AKP03 has 6 keys, AKP153 has 15 + screen.                                                                            | Drive `model` from `device.capabilities.keyCount`; lay out columns from `device.gridColumns`.                                                              |   S    |
| F-09 | components     |   High   | `EncoderPanel.qml:19`                                                                                                                                                  | `model: 4` hard-coded; encoder count is device-specific.                                                                                        | Same fix pattern as F-08.                                                                                                                                  |   S    |
| F-10 | components     |   High   | `MousePanel.qml:18`                                                                                                                                                    | `model: 6` DPI stages always rendered; AJ-MX has fewer.                                                                                         | Drive from device DPI-stage capability; show "Active stages" toggle.                                                                                       |   S    |
| F-11 | i18n           |  Medium  | `RgbPicker.qml:20` `MousePanel.qml:30`                                                                                                                                 | Combo-box items (`"Static"`, `"Breathing"`, …, `"125"`) are not wrapped in `qsTr()`.                                                            | Wrap user-visible strings in `qsTr`; numeric items keep raw values but display `qsTr` labels via `textRole`.                                               |   S    |
| F-12 | i18n           |  Medium  | `DeviceList.qml:46-47`                                                                                                                                                 | `"connected"` / `"offline"` literals not localized.                                                                                             | Wrap in `qsTr()`.                                                                                                                                          |   S    |
| F-13 | spacing        |  Medium  | `DeviceList.qml:38,39` `ProfileEditor.qml:21` `MousePanel.qml:14` `RgbPicker.qml:15`                                                                                   | Off-grid spacings: `10`, `2`, `12`.                                                                                                             | Snap to `4 / 8 / 16 / 24 / 32` via `Theme.space.*`.                                                                                                        |   S    |
| F-14 | branding       |  Medium  | `Main.qml:20`                                                                                                                                                          | Fallback color `#1a1d24` differs from `BrandingService` default `#14141a`.                                                                      | Replace fallback with the canonical default.                                                                                                               |   S    |
| F-15 | branding       |  Medium  | `EncoderPanel.qml:24` `RgbPicker.qml:32`                                                                                                                               | `#ff5722` is hard-coded as the visual accent and disagrees with the actual `branding.accent` (`#41CD52` default).                               | Use `Theme.accent`; the static-color swatch in RgbPicker should bind to a state value.                                                                     |   S    |
| F-16 | responsiveness |  Medium  | `KeyDesigner.qml:13`, `EncoderPanel.qml:13`                                                                                                                            | `anchors.centerIn: parent` plus fixed cell sizes — the grid does not reflow when window narrows.                                                | Use `anchors.fill` + grid `columns: Math.max(1, Math.floor(width/120))`; cells via `Layout.preferredWidth`.                                                |   M    |
| F-17 | responsiveness |  Medium  | `Main.qml:13-14`                                                                                                                                                       | No `minimumWidth`/`minimumHeight`; sidebar fixed at 320 px regardless of window size.                                                           | Set `minimumWidth: 900`, allow sidebar to collapse to icons-only below 700 px.                                                                             |   M    |
| F-18 | iconography    |  Medium  | (all panels)                                                                                                                                                           | No icons in any live panel; mockup's microphone / browser / paste glyphs do not exist in code.                                                  | Bundle a 24-px stroke icon set (e.g. Lucide / Phosphor); add `IconImage` to `KeyCell`, `DeviceRow`, header buttons. Stroke weight 1.5 px, 24 × 24 nominal. |   M    |
| F-19 | hierarchy      |  Medium  | (all panels)                                                                                                                                                           | No primary/secondary button styling; no save / revert / restore-defaults actions.                                                               | Add `PrimaryButton`/`SecondaryButton`, place "Apply" + "Revert" in a sticky footer of `ProfileEditor`.                                                     |   M    |
| F-20 | architecture   |  Medium  | `Main.qml`                                                                                                                                                             | No header / global navigation. No profile picker, no search, no settings.                                                                       | Add `AppHeader.qml` with product mark, profile picker, search, settings, minimize-to-tray.                                                                 |   M    |
| F-21 | architecture   |  Medium  | `DeviceList.qml`                                                                                                                                                       | Devices rendered as a flat list; no grouping by family.                                                                                         | Use `ListView.section` to group by `family`; add `SectionHeader.qml`.                                                                                      |   S    |
| F-22 | tab structure  |  Medium  | `ProfileEditor.qml:31-38`                                                                                                                                              | All four tabs (Keys/RGB/Encoders/Mouse) shown for every device.                                                                                 | Compute visible tabs from `device.capabilities`.                                                                                                           |   S    |
| F-23 | empty states   |  Medium  | `DeviceList.qml`                                                                                                                                                       | When `model.count === 0`, sidebar is blank — no first-launch guidance.                                                                          | Add empty-state with USB icon + helper text + link to *Supported Devices* docs.                                                                            |   S    |
| F-24 | dark/light     |  Medium  | `BrandingService`                                                                                                                                                      | Theme JSON supports any palette but no light theme is shipped, and code paths assume a dark base.                                               | Add `theme-light.json`, ensure all derived colors via `Theme.qml` work in both themes; gate hover lightening on luminance.                                 |   M    |
| F-25 | UX feature     |  Medium  | `RgbPicker.qml:30-33`                                                                                                                                                  | "Color" is a static rectangle — not a real picker.                                                                                              | Implement `ColorPicker.qml` with HSV wheel + hex input + recent-colors strip.                                                                              |   M    |
| F-26 | UX feature     |  Medium  | `EncoderPanel.qml`, `KeyDesigner.qml`                                                                                                                                  | No selection state; clicking does nothing visible.                                                                                              | Add `selected` boolean, render outline (`Theme.accent`, 2 px) + slight bg tint.                                                                            |   S    |
| F-27 | code health    |  Medium  | `DeviceList.qml:41,46-47` `EncoderPanel.qml:28` `KeyDesigner.qml:30`                                                                                                   | `parent.parent.model` ancestor traversal is brittle.                                                                                            | Use `model.<role>` directly (already declared `required property`s).                                                                                       |   S    |
| F-28 | performance    |   Low    | `ProfileEditor.qml:40-48`                                                                                                                                              | StackLayout instantiates all four sub-views always.                                                                                             | Use `Loader { active: tabs.currentIndex === N }`.                                                                                                          |   S    |
| F-29 | UX feature     |   Low    | (all panels)                                                                                                                                                           | No "Restore defaults" / "Revert" buttons.                                                                                                       | Add to `Inspector` and to each panel footer.                                                                                                               |   S    |
| F-30 | UX feature     |   Low    | (all)                                                                                                                                                                  | No undo/redo.                                                                                                                                   | Bind `Ctrl+Z`/`Ctrl+Shift+Z` to `ProfileController` history.                                                                                               |   M    |
| F-31 | onboarding     |   Low    | n/a                                                                                                                                                                    | No first-launch tour or welcome screen.                                                                                                         | Three-step welcome dialog (devices → theme → profiles).                                                                                                    |   M    |
| F-32 | feedback       |   Low    | n/a                                                                                                                                                                    | No confirmation when a profile write succeeds; no error when it fails.                                                                          | Toast component + connect to `ProfileController` success/error signals.                                                                                    |   S    |
| F-33 | tray UX        |   Low    | `Main.qml:23-30`                                                                                                                                                       | Tray "show window" works, but no "Quit" / "Pause" / "Switch profile" submenu items in tray.                                                     | Extend `TrayController` and add shortcuts.                                                                                                                 |   S    |
| F-34 | accessibility  |   Low    | (all)                                                                                                                                                                  | No tooltips on icon buttons (when added per F-18).                                                                                              | Wrap each icon button in `ToolTip` and provide `Accessible.description`.                                                                                   |   S    |
| F-35 | docs           |   Low    | `docs/architecture/BRANDING.md`                                                                                                                                        | Doc claims "no hard-coded hex anywhere in the QML files" — this is currently false.                                                             | Either (a) make it true via F-01, or (b) update the doc to reflect reality + roadmap. Prefer (a).                                                          |   S    |

**Severity counts**

| Severity  |  Count |
| --------- | -----: |
| Critical  |  **4** |
| High      |  **6** |
| Medium    | **17** |
| Low       |  **8** |
| **Total** | **35** |

______________________________________________________________________

## 9. Suggested redesign mockup brief

Hand this to a designer (or the media-skill image generator) for a v2 mockup.

> **Brief — "AJAZZ Control Center v2 mockup"**
>
> **Format:** 1440 × 900, dark theme, PNG, taken on a neutral wallpaper. Single window, no shadows from outside the app.
>
> **Layout:** three vertical panes — `[ 280 sidebar | flex canvas | 320 inspector ]`, plus a 56 px global header.
>
> - **Header (56 px):** product mark `AJAZZ` (left, accent green `#41CD52`), centered profile picker `Default ▾`, right cluster: search field, settings cog, tray-minimize. Bottom 1 px hairline `rgba(255,255,255,0.06)`.
> - **Sidebar (280 px, `#1e1e23`):** sections `STREAM DECKS`, `KEYBOARDS`, `MICE` (12 px caption, `#888`, letter-spaced). Within each, 56 px rows: 32 px device thumbnail, name 14 px `#f0f0f0`, codename 12 px `#aaa`, 8 px green/grey dot. Selected row: `#2c2c34` background, 3 px accent bar on the left.
> - **Canvas (`#14141a`):** breadcrumb-style title "AKP03 · Default profile". Below it, a tab bar with **active tab underlined in accent** and the rest in `#aaa`. Active tab "Keys" shows a 3 × 2 grid of 128 × 128 key cells, 16 px gaps, centered. Each cell: 12 px radius, `#2a2a32` fill, 1 px `#3a3a44` border, accent glow on the *selected* cell, icon + label centered. One cell shows a microphone icon + "OBS Mute"; selected.
> - **Inspector (320 px, `#1a1a1f` slightly lighter than canvas):** Header "Key 1". Sections: **Action** (combo box), **Icon** (icon preview + change button), **Label** (text field), **Press behavior** (segmented control: Press and Release / Hold), **Linked encoder** (small dial preview + label). Sticky footer with `Apply` (primary, accent fill) and `Revert` (secondary, ghost border). 24 px outer padding, 16 px between sections.
> - **Spacing:** strict 8-grid. Section padding 24 px, intra-section 16 px, label-to-control 8 px.
> - **Typography:** Inter (or fallback system UI). 12 / 14 / 16 / 18 / 20 / 28 px scale.
> - **Color rules:** background `#14141a`, sidebar `#1e1e23`, card `#2a2a32`, border `rgba(255,255,255,0.08)`, text `#f0f0f0` / `#aaa`. Accent `#41CD52` only for: selected state, primary buttons, focus ring, the product mark, and progress. **No accent on dial outlines or static swatches.**
> - **States to show in the mockup:** one selected key, one hover key (subtle outline), the empty-state for the second device family ("No keyboard connected — plug one in to start"). Show a non-modal success toast in the bottom-right: "Profile saved".
> - **Iconography:** Lucide / Phosphor at 1.5 px stroke, 24 × 24 nominal.
> - **Reject:** glass/translucency, gradients on backgrounds, neon outer-glows on encoders, decorative wallpaper bleed-through.
>
> **One-line:** "A three-pane Stream Deck-style canvas with a family-grouped sidebar, a sticky inspector for per-key bindings, an 8-px grid, restrained accent green, and capability-aware tabs — no Razer-style modular split, no Armoury Crate-style bloat."

______________________________________________________________________

## Sources

- [BRANDING.md (this repo)](../architecture/BRANDING.md) — defines the `BrandingService` color contract.
- [Razer Synapse 4 launch — Windows Central](https://www.windowscentral.com/software-apps/razer-synapse-4-launch)
- [Razer Synapse 4 update review — PCGamesN](https://www.pcgamesn.com/razer/synapse-4-update)
- [CORSAIR iCUE: Everything you need to know](https://www.corsair.com/us/en/explorer/gamer/mice/corsair-icue-everything-you-need-to-know/)
- [iCUE Software / Drivers — Corsair](https://www.corsair.com/us/en/s/icue)
- [Stream Deck — Elgato](https://www.elgato.com/ww/en/p/stream-deck)
- [Stream Deck 6.6 software feedback — r/elgato](https://www.reddit.com/r/elgato/comments/1c92tyt/feedback_on_the_stream_deck_66_software_first/)
- [G HUB Basics — Logitech G](https://www.logitechg.com/en-us/software/guides/g-hub-basics)
- [Armoury Crate vs MyASUS vs GPU Tweak III — ROG](https://rog.asus.com/articles/guides/armoury-crate-vs-myasus-vs-gpu-tweak-iii-whats-the-difference-between-asus-software/)
- [Armoury Crate UX critique — r/ASUS](https://www.reddit.com/r/ASUS/comments/1j4qjqj/installing_the_latest_armory_crate_is_confusing/)
- [OpenRGB project site](https://openrgb.org/)
- [OpenRGB Windows setup wiki](https://openrgb-wiki.readthedocs.io/en/latest/OpenRGB-Windows-Setup-and-Usage/)
- [8-point grid system — UX Planet](https://uxplanet.org/everything-you-should-know-about-8-point-grid-system-in-ux-design-b69cb945b18d)
- [8 px grid spacing system — The Hangline](https://www.thehangline.com/8px-grid-spacing-system-explained-for-web-designers/)
- [Accessible QML type — Qt 6 docs](https://doc.qt.io/qt-6/qml-qtquick-accessible.html)
