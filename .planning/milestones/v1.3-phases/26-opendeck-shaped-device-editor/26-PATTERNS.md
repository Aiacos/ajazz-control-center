# Phase 26: OpenDeck-shaped Device Editor — Pattern Map

**Mapped:** 2026-05-28
**Files analyzed:** 19 new/modified files
**Analogs found:** 17 / 19

## File Classification

| New/Modified File                                                  | Role                            | Data Flow        | Closest Analog                                                                            | Match Quality       |
| ------------------------------------------------------------------ | ------------------------------- | ---------------- | ----------------------------------------------------------------------------------------- | ------------------- |
| `src/app/qml/DeviceView.qml`                                       | component (top-level editor)    | request-response | `src/app/qml/KeyDesigner.qml`                                                             | exact (replacement) |
| `src/app/qml/components/EncoderDial.qml`                           | component (cell delegate)       | request-response | `src/app/qml/components/EncoderCard.qml`                                                  | exact (extend)      |
| `src/app/qml/components/TouchStripLane.qml`                        | component (row delegate)        | request-response | `src/app/qml/components/KeyCell.qml`                                                      | role-match          |
| `src/app/qml/ActionLibraryPane.qml`                                | component (drag source panel)   | event-driven     | `src/app/qml/Inspector.qml` (pane shape) + `src/app/qml/EncoderPanel.qml` (Repeater list) | role-match          |
| `src/app/src/device_layout_loader.{hpp,cpp}` (optional)            | utility (Q_INVOKABLE helper)    | request-response | `src/app/src/profile_controller.{hpp,cpp}`                                                | role-match          |
| `tests/unit/test_streamdeck_register_geometry.cpp`                 | test (Catch2 registry iterator) | CRUD             | `tests/unit/test_device_registry.cpp`                                                     | exact               |
| `src/core/include/ajazz/core/profile.hpp`                          | model (schema extension)        | CRUD             | existing `EncoderBinding` struct in same file                                             | exact               |
| `src/core/src/profile.cpp`                                         | model (serialiser extension)    | CRUD             | existing `writeEncoderBinding` + `profileToJson` in same file                             | exact               |
| `src/core/include/ajazz/core/device.hpp`                           | model (descriptor extension)    | CRUD             | existing `DeviceDescriptor` fields in same file                                           | exact               |
| `src/devices/streamdeck/src/register.cpp`                          | config (descriptor rows)        | CRUD             | existing `akp03_descriptor` / `akp05` rows in same file                                   | exact               |
| `src/app/src/profile_controller.{hpp,cpp}`                         | service (new Q_INVOKABLE)       | request-response | existing `commitKeyBinding` in same files                                                 | exact               |
| `src/app/qml/Main.qml` (one-line fix)                              | component (wire-up)             | request-response | existing `onDeviceSelected` handler lines 127-129                                         | exact               |
| `src/app/qml/ProfileEditor.qml` (Loader swap)                      | component (capability router)   | request-response | existing `keyDesignerComp` Component at line 253-323                                      | exact               |
| `resources/device-photos/<codename>.png`                           | asset (photo background)        | file-I/O         | `resources/devices/products/product-akp05e.png` (same directory convention)               | exact               |
| `resources/device-layouts/<codename>.json`                         | config (layout JSON)            | file-I/O         | `resources/streamdeck-fallback.json` (JSON qrc resource pattern)                          | role-match          |
| `resources/device-photos/README.md`                                | docs (attribution)              | —                | `resources/icons/README.md`                                                               | role-match          |
| `src/app/qml/KeyDesigner.qml` (DELETE)                             | —                               | —                | —                                                                                         | —                   |
| `src/app/qml/components/KeyCell.qml` (extend: add DropArea + Drag) | component (cell delegate)       | event-driven     | existing file (self-analog)                                                               | exact               |
| `src/app/qml/Inspector.qml` (REUSE, no changes)                    | component (right pane)          | request-response | existing file                                                                             | —                   |

______________________________________________________________________

## Pattern Assignments

### `src/app/qml/DeviceView.qml` (component, request-response)

**Analog:** `src/app/qml/KeyDesigner.qml`
**Read first:** `src/app/qml/KeyDesigner.qml` (full file, 177 lines), `src/app/qml/ProfileEditor.qml` (lines 253-323 for context), `src/app/qml/Inspector.qml` (lines 52-59 for property contract)

**What NOT to copy from KeyDesigner** (it is deleted): the single generic `GridLayout` that ignores encoder rows and touch-strip rows. The new component replaces this with three stacked rows.

**Imports pattern** (KeyDesigner.qml lines 24-28):

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter
import "components"
```

**Property contract pattern** (KeyDesigner.qml lines 30-38):

```qml
Item {
    id: root

    property int keyCount: 0
    property int gridColumns: 5
    property int selectedIndex: -1

    signal keyActivated(int index)
    signal keySelected(int index)
```

DeviceView extends this contract with: `property int keyRows`, `property int encoderCount`, `property int touchZoneCount`, `property string codename`.

**Empty state pattern** (KeyDesigner.qml lines 113-118):

```qml
EmptyState {
    anchors.centerIn: parent
    visible: root.keyCount === 0
    title: qsTr("No keys")
    body: qsTr("This device does not expose programmable LCD keys.")
}
```

**RowLayout + Inspector integration pattern** (KeyDesigner.qml lines 123-175):

```qml
RowLayout {
    anchors.fill: parent
    visible: root.keyCount > 0
    spacing: Theme.spacingLg

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        GridLayout {
            anchors.centerIn: parent
            columns: Math.max(1, root.gridColumns)
            rowSpacing: Theme.spacingSm
            columnSpacing: Theme.spacingSm

            Repeater {
                model: bindings
                delegate: KeyCell {
                    selected: root.selectedIndex === index
                    onClicked: {
                        root.selectedIndex = index;
                        root.keySelected(index);
                        root.keyActivated(index);
                    }
                }
            }
        }
    }

    Inspector {
        Layout.preferredWidth: 320
        Layout.fillHeight: true
        selectionLabel: root.selectedIndex >= 0
            ? qsTr("Key %1").arg(root.selectedIndex + 1)
            : ""
        binding: root.selectedBinding
        onBindingFieldChanged: function(field, value) {
            root.updateSelectedBinding(field, value);
        }
    }
}
```

DeviceView adds `ActionLibraryPane` on the left (240px fixed), replaces the single GridLayout with three stacked rows (key grid / encoder row / touch-strip row), and keeps the Inspector on the right (280px fixed per UI-SPEC).

**updateSelectedBinding pattern** (KeyDesigner.qml lines 91-107):

```qml
function updateSelectedBinding(field, value) {
    if (root.selectedIndex < 0 || root.selectedIndex >= bindings.count) {
        return;
    }
    bindings.setProperty(root.selectedIndex, field, value);

    var row = bindings.get(root.selectedIndex);
    ProfileController.commitKeyBinding(
        root.selectedIndex,
        row.iconSource,
        row.label,
        row.actionKind,
        row.actionParams
    );
}
```

**Brightness / live-device controls pattern** (ProfileEditor.qml lines 268-322): the brightness Slider + "Clear all" button live in `ProfileEditor`'s `keyDesignerComp` Component, NOT inside KeyDesigner/DeviceView. DeviceView itself has no live-device controls — those stay in the ProfileEditor wrapper.

______________________________________________________________________

### `src/app/qml/components/EncoderDial.qml` (component, event-driven)

**Analog:** `src/app/qml/components/EncoderCard.qml`
**Read first:** `src/app/qml/components/EncoderCard.qml` (full file, 42 lines)

**Full file for reference** (EncoderCard.qml lines 1-42):

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

ItemDelegate {
    id: root

    property int index: 0
    property string label: qsTr("Dial %1").arg(index + 1)

    width: 120
    height: 120

    background: Rectangle {
        radius: 60           // <-- full circle (radius = width/2)
        color: root.hovered ? Theme.tileHover : Theme.tile
        border.width: root.activeFocus ? Theme.focusRingWidth : 2
        border.color: root.activeFocus ? Theme.accent : Theme.accent
    }

    contentItem: Text {
        text: root.label
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontMd
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: qsTr("Configures the rotary encoder %1").arg(index + 1)
}
```

**Additions for EncoderDial.qml** (not in EncoderCard): `required property url iconSource`, `property bool selected`, `DropArea` child, `Drag.active` + `Drag.mimeData`, `transform: Scale` for drag-over animation (see UI-SPEC Component States table), cursor shape `Qt.OpenHandCursor` when occupied.

**Accessible pattern** (to extend):

```qml
Accessible.role: Accessible.Button
Accessible.name: root.iconSource.toString() !== ""
    ? qsTr("Encoder %1: %2").arg(root.index + 1).arg(root.label)
    : qsTr("Encoder %1 -- empty").arg(root.index + 1)
Accessible.description: qsTr("Drop an encoder action here")
```

Note: use `--` not `—` for ASCII compliance (CLAUDE.md test names + accessible strings).

______________________________________________________________________

### `src/app/qml/components/TouchStripLane.qml` (component, event-driven)

**Analog:** `src/app/qml/components/KeyCell.qml`
**Read first:** `src/app/qml/components/KeyCell.qml` (full file, 94 lines)

**Base structure** (KeyCell.qml lines 25-94 — full pattern for a drop-target ItemDelegate):

```qml
ItemDelegate {
    id: root

    required property int index
    required property url iconSource
    required property string label
    property bool selected: false

    width: 96
    height: 96

    background: Rectangle {
        radius: Theme.radiusLg       // <-- change to Theme.radiusSm for strip zones
        color: root.hovered ? Theme.tileHover : Theme.tile
        border.width: root.activeFocus || root.selected ? Theme.focusRingWidth : 1
        border.color: root.activeFocus || root.selected ? Theme.accent : Theme.borderSubtle
    }

    contentItem: Item {
        Item {
            anchors.fill: parent
            anchors.margins: 6

            Image {
                anchors.fill: parent
                source: root.iconSource
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
                visible: root.iconSource.toString() !== ""
            }

            Text {
                id: overlayText
                anchors.fill: parent
                text: root.label !== ""
                    ? root.label
                    : (root.iconSource.toString() === "" ? (root.index + 1).toString() : "")
                color: Theme.fgPrimary
                font.pixelSize: root.label !== "" ? Theme.fontSm : Theme.fontLg
                ...
                style: root.iconSource.toString() !== "" ? Text.Outline : Text.Normal
                styleColor: "#000000"
            }
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label !== ""
        ? qsTr("Key %1: %2").arg(root.index + 1).arg(root.label)
        : qsTr("Key %1").arg(root.index + 1)
    Accessible.description: qsTr("Configures action bound to key %1").arg(root.index + 1)
}
```

**TouchStripLane differences from KeyCell:**

- `width` from layout JSON viewBox; aspect ratio ~3:1 (wide bar)
- `background.radius: Theme.radiusSm` (4px, not radiusLg)
- Top border thickened to 4px `Theme.accent` (LED-strip hint per UI-SPEC)
- Default icon: `"view_column"` Material Symbol (centered when empty)
- DropArea key: `"application/x-ajazz-action"` + `"application/x-ajazz-binding"` with `controller: "TouchZone"` check
- Accessible name: `qsTr("Touch zone %1 -- empty").arg(index + 1)` (ASCII dashes)

**Lane container pattern** (from EncoderPanel.qml lines 32-50 — Row of delegates):

```qml
// TouchStripLane wraps the zone cells in a Row:
Row {
    spacing: Theme.spacingXs

    Repeater {
        model: root.touchZoneCount
        delegate: TouchZoneCell {
            required property int index
            index: index
        }
    }
}
```

______________________________________________________________________

### `src/app/qml/ActionLibraryPane.qml` (component, event-driven)

**Analog:** `src/app/qml/Inspector.qml` (pane shape/structure), `src/app/qml/EncoderPanel.qml` (simple list with Repeater)
**Read first:** `src/app/qml/Inspector.qml` lines 44-59 (imports + property contract), lines 207-230 (ActionKind ComboBox — the 5 built-in entries to replicate as tiles)

**Imports and pane structure pattern** (Inspector.qml lines 44-59):

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Rectangle {
    id: root

    property string selectionLabel: ""
    property bool   hasSelection: selectionLabel.length > 0
    property var    binding: null

    signal bindingFieldChanged(string field, var value)
```

ActionLibraryPane is a similar fixed-width `Rectangle` pane. Replace `binding`/`bindingFieldChanged` with a drag-source model.

**ActionKind tile data** (Inspector.qml lines 215-223 — the 5 entries to replicate as drag tiles):

```qml
model: [
    qsTr("Plugin action"),     // ActionKind 0
    qsTr("Key macro"),         // ActionKind 2
    qsTr("Launch command"),    // ActionKind 3
    qsTr("Open URL"),          // ActionKind 4
    qsTr("Open folder")        // ActionKind 5
]
readonly property var _kindByIndex: [0, 2, 3, 4, 5]
```

These become the 5 draggable tiles. Icon mapping per UI-SPEC: `extension`, `keyboard`, `terminal`, `link`, `folder_open`.

**ListView + ItemDelegate pattern** (from DeviceList.qml — list of items, each draggable):
Each tile is an `ItemDelegate` with:

```qml
ItemDelegate {
    id: tile
    width: parent.width
    height: 48

    Drag.active: dragHandler.active
    Drag.dragType: Drag.Automatic
    Drag.mimeData: {
        "application/x-ajazz-action": JSON.stringify({
            actionKind: model.kind,
            label: model.label,
            iconName: model.iconName
        })
    }

    DragHandler {
        id: dragHandler
        target: null
        acceptedButtons: Qt.LeftButton
        dragThreshold: 8   // avoids conflict with ListView scroll
    }

    contentItem: RowLayout {
        Text { /* Material icon */ }
        Text { text: model.label; color: Theme.fgPrimary; ... }
    }

    background: Rectangle {
        color: tile.hovered ? Theme.bgRowHover : Theme.bgBase
    }
}
```

**PITFALL (CLAUDE.md + RESEARCH.md §7.2):** `DragHandler` inside a `ListView` delegate MUST set `acceptedButtons: Qt.LeftButton` and `dragThreshold: 8` to prevent accidental drags during list scroll.

______________________________________________________________________

### `src/app/src/device_layout_loader.{hpp,cpp}` (utility, request-response) — optional

**Analog:** `src/app/src/profile_controller.{hpp,cpp}`
**Read first:** `src/app/src/profile_controller.hpp` (full file, 252 lines) for QML singleton pattern

**QML_SINGLETON pattern** (profile_controller.hpp lines 40-54):

```cpp
class ProfileController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ProfileController)
    QML_SINGLETON
public:
    static ProfileController* create(QQmlEngine* qml, QJSEngine* js);
    static void registerInstance(ProfileController* instance) noexcept;
    explicit ProfileController(QObject* parent);   // no default ctor
    ...
};

// MANDATORY anti-hallucination guard (CLAUDE.md "Qt 6 / QML gotchas"):
static_assert(!std::is_default_constructible_v<ProfileController>,
              "ProfileController must not be default-constructible — see BrandingService.");
```

**Q_INVOKABLE returning a JS-friendly type** (pattern from commitKeyBinding):

```cpp
Q_INVOKABLE QVariantMap layoutFor(QString const& codename);
// Returns JS object: { codename, photo, viewBox:{w,h}, keys:[...], encoders:[...], touchZones:[...] }
// Reads qrc:/qt/qml/AjazzControlCenter/device-layouts/<codename>.json via QFile
```

**Alternative (zero-C++): QML XMLHttpRequest pattern** — if DeviceLayoutLoader is skipped, DeviceView reads the layout JSON directly:

```qml
function loadLayout(codename) {
    var xhr = new XMLHttpRequest();
    xhr.open("GET",
        "qrc:/qt/qml/AjazzControlCenter/device-layouts/" + codename + ".json",
        false);   // synchronous — layout is tiny, called once on mount
    xhr.send();
    if (xhr.status === 200)
        return JSON.parse(xhr.responseText);
    return null;  // triggers outline-frame fallback (D-07)
}
```

Planner chooses: XMLHttpRequest is zero-dependency; Q_INVOKABLE gives type-checked error reporting. Both are valid. The XMLHttpRequest path is simpler for Wave 4.

______________________________________________________________________

### `tests/unit/test_streamdeck_register_geometry.cpp` (test, CRUD)

**Analog:** `tests/unit/test_device_registry.cpp`
**Read first:** `tests/unit/test_device_registry.cpp` (full file, 62 lines)

**Test structure pattern** (test_device_registry.cpp lines 1-62):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/device_registry.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("device registry enumerates all three families", "[registry]") {
    ajazz::core::DeviceRegistry registry;
    ajazz::streamdeck::registerAll(registry);
    // ... construct registry, call registerAll, iterate descriptors
    auto const descriptors = registry.enumerate();
    REQUIRE(descriptors.size() >= 7);
    for (auto const& d : descriptors) {
        switch (d.family) { ... }
    }
    REQUIRE(deckCount >= 3);
}
```

**Geometry-check test structure** (new pattern — copy skeleton from above):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/device_registry.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <array>
#include <string_view>
#include <catch2/catch_test_macros.hpp>

// REQ-26-D: allow-list for SKUs intentionally deferred (keyRows = 0 sentinel).
// Removing a codename from this list requires populating the descriptor row.
static constexpr std::array kDeferredLcdSkus = {
    std::string_view{"akp815"},  // Phase 26 SPEC out-of-scope: 800x480 wide strip
};

TEST_CASE("streamdeck LCD-key descriptors have geometry fields", "[registry][geometry]") {
    ajazz::core::DeviceRegistry registry;
    ajazz::streamdeck::registerAll(registry);

    for (auto const& d : registry.enumerate()) {
        if (d.family != ajazz::core::DeviceFamily::StreamDeck) continue;
        if (d.keyCount == 0) continue;  // not an LCD-key SKU

        // Skip deferred SKUs
        bool deferred = false;
        for (auto const sv : kDeferredLcdSkus) {
            if (d.codename == sv) { deferred = true; break; }
        }
        if (deferred) continue;

        // REQ-26-D assertions
        REQUIRE(d.keyRows > 0);
        if (d.touchZoneCount > 0) {
            REQUIRE(d.mainScreenWidthPx > 0);
            REQUIRE(d.mainScreenHeightPx > 0);
        }
    }
}
```

**ASCII test name required** (CLAUDE.md): use `"streamdeck LCD-key descriptors have geometry fields"`, not `"streamdeck LCD-key descriptors → geometry fields"`.

______________________________________________________________________

### `src/core/include/ajazz/core/profile.hpp` (model, CRUD)

**Analog:** Existing `EncoderBinding` struct + `Profile::encoders` field in same file (lines 113-155)
**Read first:** `src/core/include/ajazz/core/profile.hpp` (full file, 200 lines)

**EncoderBinding pattern** (profile.hpp lines 113-118 — copy this exact shape for TouchZoneBinding):

```cpp
struct EncoderBinding {
    std::vector<Action> onCw;    ///< Chain fired on a clockwise rotation tick.
    std::vector<Action> onCcw;   ///< Chain fired on a counter-clockwise tick.
    std::vector<Action> onPress; ///< Chain fired on a knob press.
    KeyState state;              ///< Optional LCD label for AKP05's encoder strip.
};
```

**Profile map fields pattern** (profile.hpp lines 154-155 — copy pattern for touchZones):

```cpp
std::unordered_map<std::uint16_t, Binding> keys; ///< Key index -> binding (root page).
std::unordered_map<std::uint16_t, EncoderBinding> encoders; ///< Encoder index -> binding.
```

**New TouchZoneBinding** (D-11 decision):

```cpp
struct TouchZoneBinding {
    KeyState state;
    std::vector<Action> onTap;
};

// In Profile:
std::unordered_map<std::uint8_t, TouchZoneBinding> touchZones;  // NEW
```

______________________________________________________________________

### `src/core/src/profile.cpp` (model, CRUD)

**Analog:** Existing `writeEncoderBinding` + `profileToJson` / `profileFromJson` in same file
**Read first:** `src/core/src/profile.cpp` lines 203-260 (writeEncoderBinding + profileToJson top)

**WARNING — COD-031 boundary:** This file uses a hand-rolled JSON writer (`std::ostringstream`), NOT `nlohmann::json`. The comment on line 1-16 explains why. Do NOT introduce `nlohmann::json` in `ajazz_core` — it is PRIVATE-linked to `ajazz_plugins` only (CLAUDE.md "No `nlohmann::json` in `ajazz_core`"). The hand-rolled pattern MUST be followed.

**writeEncoderBinding pattern** (profile.cpp lines 203-216 — copy for writeTouchZoneBinding):

```cpp
void writeEncoderBinding(std::ostringstream& out, EncoderBinding const& b) {
    out << "{";
    writeChain(out, "onCw", b.onCw);
    out << ",";
    writeChain(out, "onCcw", b.onCcw);
    out << ",";
    writeChain(out, "onPress", b.onPress);
    if (!keyStateIsDefault(b.state)) {
        out << ",\"state\":";
        writeKeyState(out, b.state);
    }
    out << "}";
}
```

**profileToJson encoders section** (profile.cpp lines 245-256 — copy for touchZones):

```cpp
out << ",\"encoders\":{";
first = true;
for (auto const& [idx, eb] : profile.encoders) {
    if (!first) { out << ","; }
    out << "\"" << idx << "\":";
    writeEncoderBinding(out, eb);
    first = false;
}
out << "}";
```

**Schema version bump** (D-12): add `"_schemaVersion": 2` to the writer output (e.g. as the first key after `"id"`). The reader must detect absence of `_schemaVersion` (v1 profile) and treat `touchZones` as empty — forward/backward compat.

**Migration test analog** (test_profile_serialization.cpp lines 36-100 — copy round-trip structure):

```cpp
TEST_CASE("v1 profile migrates touchZones to empty on load", "[profile][migration]") {
    // Construct a JSON string with no _schemaVersion / no touchZones key
    // Load via profileFromJson, assert touchZones.empty()
    // Re-serialize, assert "_schemaVersion":2 present
}
```

______________________________________________________________________

### `src/core/include/ajazz/core/device.hpp` (model, CRUD)

**Analog:** Existing `DeviceDescriptor` fields at device.hpp lines 72-103
**Read first:** `src/core/include/ajazz/core/device.hpp` (full file, 237 lines)

**Existing field pattern** (device.hpp lines 72-85 — additive zero-default style):

```cpp
std::uint16_t keyCount{0};      ///< Number of LCD/macro keys (0 if N/A).
std::uint16_t gridColumns{0};   ///< Preferred grid column count for the keys (0 if N/A).
std::uint16_t encoderCount{0};  ///< Number of rotary encoders (0 if N/A).
std::uint16_t dpiStageCount{0}; ///< Number of DPI stages (mice; 0 if N/A).
bool hasRgb{false};             ///< True if the device exposes RGB lighting.
bool hasTouchStrip{false};      ///< True if the device exposes a touch strip.
bool hasClock{false};           ///< ...
```

**New fields to add** (after line 85, preserving zero defaults):

```cpp
// ---- OpenDeck-pattern geometry (Phase 26) --------------------------------
/// Number of LCD-key rows. When 0, falls back to keyCount/gridColumns.
std::uint8_t keyRows{0};

/// Number of touch-strip zones aligned to encoders.
/// 0 = no touch strip; >0 = N discrete touchpoints.
/// AKP05/N4 = 4; AKP815 = 0 (its strip uses mainScreenWidthPx instead).
std::uint8_t touchZoneCount{0};

/// Width in pixels of the main LCD strip (AKP815's 800x480). 0 if absent.
std::uint16_t mainScreenWidthPx{0};

/// Height in pixels of the main LCD strip. 0 if absent.
std::uint16_t mainScreenHeightPx{0};
```

______________________________________________________________________

### `src/devices/streamdeck/src/register.cpp` (config, CRUD)

**Analog:** Existing `akp03_descriptor` helper + `akp05` inline rows in same file
**Read first:** `src/devices/streamdeck/src/register.cpp` lines 85-100 (akp03_descriptor helper) and lines 260-311 (AKP05 descriptor rows)

**Descriptor helper pattern** (register.cpp lines 86-100):

```cpp
constexpr auto
akp03_descriptor(std::uint16_t vid, std::uint16_t pid, char const* model, char const* codename) {
    return core::DeviceDescriptor{
        .vendorId = vid,
        .productId = pid,
        .family = core::DeviceFamily::StreamDeck,
        .model = model,
        .codename = codename,
        .keyCount = akp03::DisplayKeyCount,
        .gridColumns = 3,
        .encoderCount = akp03::EncoderCount,
    };
}
```

**Inline row pattern** (register.cpp lines 298-311 — AKP05E with hasTouchStrip):

```cpp
reg.registerDevice(
    core::DeviceDescriptor{
        .vendorId = akp05::VendorId,
        .productId = Akp05ePid,
        .family = core::DeviceFamily::StreamDeck,
        .model = "AJAZZ AKP05E (Stream Dock Plus)",
        .codename = "akp05e",
        .keyCount = akp05::KeyCount,
        .gridColumns = akp05::KeyCols,
        .encoderCount = akp05::EncoderCount,
        .hasTouchStrip = true,
        .hasClock = false,
    },
    &makeAkp05);
```

**New fields to add to each LCD-key row** (all rows except AKP815):

```cpp
// Add to akp153_descriptor helper return:
.keyRows = 3,  // 3×5 grid

// Add to akp03_descriptor helper return:
.keyRows = 2,  // 2×3 grid

// Add to AKP05/N4/AKP05E inline rows:
.keyRows = 2,
.touchZoneCount = 4,
.mainScreenWidthPx = 0,    // touch strip is 4 discrete zones, not one wide rect
.mainScreenHeightPx = 0,

// AKP815 row stays at keyRows = 0 (deferred sentinel per D-13)
```

______________________________________________________________________

### `src/app/src/profile_controller.{hpp,cpp}` (service, request-response)

**Analog:** Existing `commitKeyBinding` in same files
**Read first:** `src/app/src/profile_controller.hpp` lines 125-151 (commitKeyBinding declaration), `src/app/src/profile_controller.cpp` lines 150-188 (commitKeyBinding implementation)

**Declaration pattern** (profile_controller.hpp lines 147-151):

```cpp
Q_INVOKABLE void commitKeyBinding(int keyIndex,
                                  QString const& iconPath,
                                  QString const& label,
                                  int actionKind,
                                  QString const& settingsJson);
```

**Implementation pattern** (profile_controller.cpp lines 150-188):

```cpp
void ProfileController::commitKeyBinding(int keyIndex,
                                         QString const& iconPath,
                                         QString const& label,
                                         int actionKind,
                                         QString const& settingsJson) {
    // Validate index range
    if (keyIndex < 0 ||
        keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitKeyBinding: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return;
    }
    // Validate actionKind
    constexpr int kMaxActionKind = static_cast<int>(ajazz::core::ActionKind::BackToParent);
    if (actionKind < 0 || actionKind > kMaxActionKind) {
        AJAZZ_LOG_WARN(...); return;
    }

    auto const idx = static_cast<std::uint16_t>(keyIndex);
    auto& binding = m_profile.keys[idx];

    binding.state.imagePath =
        iconPath.isEmpty() ? std::nullopt : std::optional<std::string>{iconPath.toStdString()};
    binding.state.text =
        label.isEmpty() ? std::nullopt : std::optional<std::string>{label.toStdString()};

    ajazz::core::Action act{};
    act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
    act.settingsJson = settingsJson.toStdString();
    binding.onPress = {std::move(act)};

    emit profileChanged();
}
```

**New `commitTouchZoneBinding` follows the same shape** with `m_profile.touchZones[idx]` instead of `m_profile.keys[idx]`, and `TouchZoneBinding::state` + `TouchZoneBinding::onTap`. Use `quint8` (not `int` → `uint16_t`) for the zone index since `touchZoneCount` is `uint8_t`.

______________________________________________________________________

### `src/app/qml/Main.qml` (one-line fix, lines 127-129)

**Current state** (Main.qml lines 127-130):

```qml
onDeviceSelected: codename => {
    editor.codename = codename;
    editor.capabilities = DeviceModel.capabilitiesFor(codename);
}
```

**Target state** (add one line before the assignments):

```qml
onDeviceSelected: codename => {
    StreamDockControlService.setActiveDevice(codename);  // closes GAP-25A
    editor.codename = codename;
    editor.capabilities = DeviceModel.capabilitiesFor(codename);
}
```

`StreamDockControlService` is already a QML singleton accessible by name (registered in the app). No import needed.

______________________________________________________________________

### `src/app/qml/ProfileEditor.qml` (Loader swap)

**Current Loader** (ProfileEditor.qml lines 186-189):

```qml
Loader {
    active: stack.currentIndex === 0 && root._showKeys
    sourceComponent: keyDesignerComp
}
```

**Current Component definition** (ProfileEditor.qml lines 253-323):

```qml
Component {
    id: keyDesignerComp

    ColumnLayout {
        spacing: Theme.spacingSm

        KeyDesigner {
            Layout.fillWidth: true
            Layout.fillHeight: true
            keyCount: root._keyCount
            gridColumns: root._gridColumns
        }
        // brightness slider + clear-all row...
    }
}
```

**Target** (replace `KeyDesigner` with `DeviceView`; note the Component stays, just the inner component type changes):

```qml
Component {
    id: keyDesignerComp  // rename to deviceViewComp in same commit

    ColumnLayout {
        spacing: Theme.spacingSm

        DeviceView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            keyCount: root._keyCount
            gridColumns: root._gridColumns
            keyRows: root._keyRows         // NEW — from capabilities map
            encoderCount: root._encoderCount
            touchZoneCount: root._touchZoneCount
            codename: root.codename
        }
        // brightness slider + clear-all row unchanged
    }
}
```

**Capabilities map extension**: `ProfileEditor.qml` reads `_keyRows` + `_touchZoneCount` from `capabilities` the same way it reads `_keyCount` (lines 44-50). These require `DeviceModel.capabilitiesFor()` to expose the new descriptor fields — check `DeviceModel` implementation.

______________________________________________________________________

### `resources/device-photos/<codename>.png` (asset, file-I/O)

**Analog:** `resources/devices/products/product-akp05e.png` convention
**Read first:** `src/app/qml/components/DeviceImage.qml` (full file, 138 lines) for the URL resolution pattern; `src/app/CMakeLists.txt` lines 281-297 for the glob-based qrc registration

**Existing photo path convention** (DeviceImage.qml lines 40-56):

```qml
readonly property string _base: "qrc:/qt/qml/AjazzControlCenter/icons/devices/products/"
function _img(name) { return root._base + name; }

readonly property var _productByCodename: ({
    "akp05e":     _img("product-akp05e.png"),
    "akp05":      _img("product-akp05.png"),
    ...
})
```

**Discovery:** The UI-SPEC references `resources/devices/products/product-<codename>.png` as the existing photo location (see UI-SPEC §"Photo Background Strategy"). Phase 26 device-layout JSON `photo` field should reference `product-akp05e.png` (the name already used by DeviceImage.qml), NOT introduce a parallel `resources/device-photos/` directory. This avoids duplicating assets.

**CMake registration** (CMakeLists.txt lines 288-296):

```cmake
file(GLOB ACC_DEVICE_PRODUCTS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/resources/devices/products/*.png
     ${CMAKE_SOURCE_DIR}/resources/devices/products/*.jpg
)
foreach(product_path IN LISTS ACC_DEVICE_PRODUCTS)
    get_filename_component(product_name "${product_path}" NAME)
    set_source_files_properties(
        "${product_path}" PROPERTIES QT_RESOURCE_ALIAS icons/devices/products/${product_name}
    )
endforeach()
```

Adding a new `product-<codename>.png` requires **only dropping the file** — no CMakeLists edit needed (CONFIGURE_DEPENDS re-globs).

______________________________________________________________________

### `resources/device-layouts/<codename>.json` (config, file-I/O)

**Analog:** `resources/streamdeck-fallback.json` (JSON resource with explicit QT_RESOURCE_ALIAS)
**Read first:** `src/app/CMakeLists.txt` lines 237-248

**JSON resource registration pattern** (CMakeLists.txt lines 237-248):

```cmake
set(ACC_STREAMDOCK_FALLBACK ${CMAKE_SOURCE_DIR}/resources/streamdeck-fallback.json)
set_source_files_properties(
    ${ACC_STREAMDOCK_FALLBACK} PROPERTIES QT_RESOURCE_ALIAS streamdock-fallback.json
)
```

For layout files, use a GLOB to auto-register, mirroring the products photo pattern:

```cmake
file(GLOB ACC_DEVICE_LAYOUTS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/resources/device-layouts/*.json
)
foreach(layout_path IN LISTS ACC_DEVICE_LAYOUTS)
    get_filename_component(layout_name "${layout_path}" NAME)
    set_source_files_properties(
        "${layout_path}" PROPERTIES QT_RESOURCE_ALIAS device-layouts/${layout_name}
    )
endforeach()
```

Then accessible in QML as: `"qrc:/qt/qml/AjazzControlCenter/device-layouts/akp05e.json"`

______________________________________________________________________

### `resources/device-photos/README.md` (docs, attribution)

**Analog:** `resources/icons/README.md`
**Read first:** `resources/icons/README.md` (full file — establishes the tone and licensing convention)

The README.md follows the same header structure: purpose, license posture, attribution table, conventions. Key difference: this README documents fair-use vendor imagery, not original artwork. Follow the format (not the content) of `resources/icons/README.md`.

______________________________________________________________________

## Shared Patterns

### QML Singleton Pattern

**Source:** `src/app/src/profile_controller.hpp` lines 40-54 + static_assert at line 249-250
**Apply to:** `DeviceLayoutLoader` (if implemented as Q_INVOKABLE C++ class)

```cpp
class DeviceLayoutLoader : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DeviceLayoutLoader)
    QML_SINGLETON
public:
    static DeviceLayoutLoader* create(QQmlEngine*, QJSEngine*);
    static void registerInstance(DeviceLayoutLoader*) noexcept;
    explicit DeviceLayoutLoader(QObject* parent);  // no default ctor
    ...
};
static_assert(!std::is_default_constructible_v<DeviceLayoutLoader>,
              "DeviceLayoutLoader must not be default-constructible — see BrandingService.");
```

### Theme Token Usage

**Source:** `src/app/qml/Theme.qml` (full file — all tokens catalogued)
**Apply to:** All new QML files
Key tokens for Phase 26:

- Cell backgrounds: `Theme.tile` (idle), `Theme.tileHover` (hover)
- Cell borders: `Theme.borderSubtle` (idle), `Theme.accent` (selected/focused/drag-over)
- Drop-accept highlight: `Theme.surfaceContainerLow` (accent-tinted background, level 2)
- Trash zone: `Theme.errorAccent`, `Theme.chipBgError`
- Spacing: `Theme.spacingXs/Sm/Md/Lg/Xl` = 4/8/12/16/24
- Radius: `Theme.radiusSm` (4px, touch-strip), `Theme.radiusLg` (8px, key cells)
- Motion: `Theme.durationShort` (150ms), `Easing.OutQuad` for drag-scale animation

### Drag-Drop Scale Animation

**Source:** `26-UI-SPEC.md` Component States table (no existing codebase analog — new pattern)
**Apply to:** KeyCell (extended), EncoderDial, TouchStripLane zone cells

```qml
transform: Scale {
    id: cellScale
    origin.x: width / 2
    origin.y: height / 2
    xScale: 1.0
    yScale: 1.0
    Behavior on xScale { NumberAnimation { duration: 100; easing.type: Easing.OutQuad } }
    Behavior on yScale { NumberAnimation { duration: 100; easing.type: Easing.OutQuad } }
}

DropArea {
    anchors.fill: parent
    keys: ["application/x-ajazz-action", "application/x-ajazz-binding"]
    onEntered: function(drop) {
        drop.accepted = true;
        cellScale.xScale = 1.05;
        cellScale.yScale = 1.05;
    }
    onExited: { cellScale.xScale = 1.0; cellScale.yScale = 1.0; }
    onDropped: function(drop) {
        cellScale.xScale = 1.0; cellScale.yScale = 1.0;
        // dispatch to ProfileController
    }
}
```

### Hand-Rolled JSON Pattern (core library)

**Source:** `src/core/src/profile.cpp` lines 36-200
**Apply to:** `profile.cpp` extension for `touchZones` serialisation

```cpp
// NEVER use nlohmann::json in ajazz_core (COD-031 boundary).
// Use std::ostringstream + escape() + writeChain() helpers already in the file.
void writeTouchZoneBinding(std::ostringstream& out, TouchZoneBinding const& b) {
    out << "{";
    writeChain(out, "onTap", b.onTap);
    if (!keyStateIsDefault(b.state)) {
        out << ",\"state\":";
        writeKeyState(out, b.state);
    }
    out << "}";
}
```

### Catch2 Registry Iterator Pattern

**Source:** `tests/unit/test_device_registry.cpp` lines 1-62
**Apply to:** `tests/unit/test_streamdeck_register_geometry.cpp`

- Include `ajazz/core/device_registry.hpp` + `ajazz/streamdeck/streamdeck.hpp`
- Construct a local `DeviceRegistry` (audit A1: no global state sharing)
- Call `ajazz::streamdeck::registerAll(registry)`
- Iterate `registry.enumerate()` and REQUIRE per-descriptor assertions
- ASCII-only test names (no em-dash, no arrows)

______________________________________________________________________

## No Analog Found

| File                                                                  | Role                    | Data Flow    | Reason                                                                                                                                                                |
| --------------------------------------------------------------------- | ----------------------- | ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/app/qml/components/TouchStripLane.qml` (the outer Row container) | component (row)         | event-driven | No existing horizontal row-of-cells component; closest is EncoderPanel.qml's GridLayout but it does not have DropArea cells. Combine KeyCell + EncoderPanel patterns. |
| `src/app/qml/ActionLibraryPane.qml` (DragHandler inside ListView)     | component (drag source) | event-driven | No existing draggable list in the codebase. Use RESEARCH.md §7.2 QML drag-drop excerpts as reference; pitfall documented above.                                       |

______________________________________________________________________

## Metadata

**Analog search scope:** `src/app/qml/`, `src/app/qml/components/`, `src/app/src/`, `src/core/include/`, `src/core/src/`, `src/devices/streamdeck/src/`, `tests/unit/`, `resources/`
**Files read:** 20
**Pattern extraction date:** 2026-05-28
