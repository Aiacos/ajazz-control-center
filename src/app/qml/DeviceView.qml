// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceView.qml -- geometry-driven three-stacked-rows device editor.
//
// Replaces KeyDesigner.qml for all LCD-key SKUs (REQ-26-B, Phase 26).
//
// Layout: three-column RowLayout:
//   1. ActionLibraryPane (240px fixed, left)
//   2. Device chassis area (fills remaining width, center)
//   3. Inspector.qml (280px fixed, right)
//
// Chassis area renders three stacked rows:
//   Row 1..keyRows:  keyRows x keyColumns LCD-key cells (KeyCell.qml)
//   Row keyRows+1:   (when encoderCount > 0) Repeater over encoderCount -> EncoderDial.qml
//   Row keyRows+2:   (when touchZoneCount > 0) TouchStripLane.qml
//
// Photo background strategy (D-04, D-07):
//   loadLayout(codename) fetches qrc:/qt/qml/AjazzControlCenter/device-layouts/<codename>.json
//   via XMLHttpRequest. On success: render per-SKU photo + viewBox-positioned cells.
//   On failure / missing JSON: render outline-frame fallback (D-07) from DeviceDescriptor
//   geometry (keyRows, keyColumns, encoderCount, touchZoneCount).
//
// NOTE: Phase 26 v1 ships with the outline-frame fallback wired unconditionally
//       because per-SKU layout JSONs land in Plan 26-06. The photo path is
//       fully implemented; it engages once 26-06 populates device-layouts/.
//
// Trash zone (D-09, UI-SPEC):
//   RoundButton top-right of chassis; 30% opacity idle; 100% during any cell drag.
//   On drop: calls ProfileController.commitKeyBinding/commitTouchZoneBinding with
//   empty params to clear the binding.
//   // Phase 26 v1: ProfileController.clearBinding() does not exist yet; trash
//   // drops route through commitXxxBinding with empty params (effectively clears
//   // the visual; a dedicated clearBinding Q_INVOKABLE lands in a follow-up plan).
//
// Keyboard navigation:
//   v1: basic Tab/Shift-Tab chain through ActionLibraryPane -> chassis -> Inspector.
//   Full grid arrow-key navigation is a follow-up (arrow keys within chassis deferred).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root

    // ---- Property contract (extends KeyDesigner.qml contract) ---------------
    property int    keyCount:    0
    property int    gridColumns: 0   // falls back from keyCount math when 0
    property int    keyRows:     0   // from DeviceDescriptor; 0 => infer from keyCount/gridColumns
    property int    encoderCount:    0
    property int    touchZoneCount:  0
    property string codename:    ""

    property int    selectedKeyIndex:     -1
    property int    selectedEncoderIndex: -1
    property int    selectedZoneIndex:    -1

    signal keySelected(int idx)
    signal keyActivated(int idx)
    signal encoderSelected(int idx)
    signal zoneSelected(int idx)

    // ---- Resolved grid dimensions ------------------------------------------
    readonly property int _keyColumnsResolved: gridColumns > 0
        ? gridColumns
        : Math.max(1, Math.ceil(keyCount / Math.max(1, keyRowsResolved)))
    readonly property int keyRowsResolved: keyRows > 0
        ? keyRows
        : Math.max(1, Math.ceil(keyCount / Math.max(1, gridColumns > 0 ? gridColumns : 5)))

    // ---- Observability accessors for tests (Phase 26 Plan 26-05, REQ-26-B) -
    // Pure read-only mirrors of the Repeater counts exposed for offscreen QML
    // tests (test_device_view_geometry.qml).  No behaviour change at runtime.
    readonly property int keyCellsRendered:   keyCount          // equals bindings.count after _ensureBindings
    readonly property int encoderDialsRendered: encoderCount    // drives encoder Repeater model directly
    readonly property int touchZonesRendered: touchZoneCount    // drives TouchStripLane.touchZoneCount

    // ---- Per-key binding model (mirrors KeyDesigner.qml) -------------------
    ListModel { id: bindings }

    // Phase 29 (OpenDeck parity / update_state): when the device renders a key
    // (a plugin's setImage/setTitle, a built-in icon, or a profile repaint), the
    // control service emits keyImageAssigned so the on-screen cell mirrors the
    // SAME live frame the device shows. Point the cell's iconSource at the
    // "livekey" image provider; the ?r=<revision> query busts QML's image cache.
    Connections {
        target: StreamDockControlService
        function onKeyImageAssigned(keyIndex, revision) {
            if (keyIndex >= 0 && keyIndex < bindings.count) {
                bindings.setProperty(keyIndex, "iconSource",
                                     "image://livekey/" + keyIndex + "?r=" + revision);
            }
        }
    }

    function _ensureBindings() {
        while (bindings.count < root.keyCount) {
            bindings.append({ iconSource: "", label: "", actionKind: 0, actionParams: "", actionId: "" });
        }
        while (bindings.count > root.keyCount) {
            bindings.remove(bindings.count - 1);
        }
        if (root.selectedKeyIndex >= bindings.count) {
            root.selectedKeyIndex = -1;
        }
    }

    onKeyCountChanged: { _ensureBindings(); _syncFromProfile(); }
    Component.onCompleted: { _ensureBindings(); _syncFromProfile(); }

    // Rebuild the preview model from the active profile's key bindings. Called
    // on profileChanged so switching profiles (or any commit) refreshes the
    // tiles — the QML model is the editor's source of truth for the preview,
    // and ProfileController.activeKeyBindings() reflects the committed state.
    function _syncFromProfile() {
        for (var i = 0; i < bindings.count; ++i) {
            bindings.set(i, { iconSource: "", label: "", actionKind: 0, actionParams: "", actionId: "" });
        }
        var kb = ProfileController.activeKeyBindings();
        for (var j = 0; j < kb.length; ++j) {
            var b = kb[j];
            if (b.index >= 0 && b.index < bindings.count) {
                bindings.set(b.index, { iconSource: b.iconSource ? b.iconSource : "",
                                        label: b.label ? b.label : "",
                                        actionKind: b.actionKind,
                                        actionParams: "",
                                        actionId: b.actionId ? b.actionId : "" });
            }
        }
    }

    Connections {
        target: ProfileController
        function onProfileChanged() {
            root._syncFromProfile();
            root._refreshSelectedKeyActionList();
            root._refreshSelectedEncoderBinding();
        }
    }

    readonly property var selectedBinding:
        selectedKeyIndex >= 0 && selectedKeyIndex < bindings.count
            ? bindings.get(selectedKeyIndex)
            : null

    // PLUGIN-23: full onPress action list for the selected key.
    // Derived from activeKeyBindings() "actionList" field. Updated on profileChanged.
    property var _selectedKeyActionList: []

    function _refreshSelectedKeyActionList() {
        if (root.selectedKeyIndex < 0) {
            root._selectedKeyActionList = [];
            return;
        }
        var kb = ProfileController.activeKeyBindings();
        for (var i = 0; i < kb.length; ++i) {
            var b = kb[i];
            if (b.index === root.selectedKeyIndex) {
                root._selectedKeyActionList = b.actionList ? b.actionList : [];
                return;
            }
        }
        root._selectedKeyActionList = [];
    }

    onSelectedKeyIndexChanged: { root._refreshSelectedKeyActionList(); }

    // Selected ENCODER (dial) binding, resolved from activeEncoderBindings() so
    // the Property Inspector can configure a plugin action bound to a dial. Kept
    // reactive by re-resolving when the encoder selection changes AND on
    // profileChanged (a drag-bind/move mutates the profile, not this property).
    property var _selectedEncoderBinding: null
    function _refreshSelectedEncoderBinding() {
        if (root.selectedEncoderIndex < 0) {
            root._selectedEncoderBinding = null;
            return;
        }
        var eb = ProfileController.activeEncoderBindings();
        for (var i = 0; i < eb.length; ++i) {
            if (eb[i].index === root.selectedEncoderIndex) {
                root._selectedEncoderBinding = eb[i];
                return;
            }
        }
        root._selectedEncoderBinding = null;
    }
    onSelectedEncoderIndexChanged: { root._refreshSelectedEncoderBinding(); }

    // The control whose action the Inspector edits: the selected key, else the
    // selected dial. (Touch-zone PI is a follow-up; zones already bind via tap.)
    readonly property var selectedInspectorBinding:
        selectedKeyIndex >= 0
            ? selectedBinding
            : (selectedEncoderIndex >= 0 ? _selectedEncoderBinding : null)

    function updateSelectedBinding(field, value) {
        if (root.selectedKeyIndex < 0 || root.selectedKeyIndex >= bindings.count) return;
        bindings.setProperty(root.selectedKeyIndex, field, value);
        var row = bindings.get(root.selectedKeyIndex);
        ProfileController.commitKeyBinding(root.selectedKeyIndex, row.iconSource,
                                           row.label, row.actionKind, row.actionParams,
                                           row.actionId !== undefined ? row.actionId : "");
    }

    // ---- Track any active drag for trash-zone opacity (WR-02) ---------------
    // Counter tracks how many cell drags are currently in flight. Drives the
    // anyDragActive bool which controls trash-zone opacity (D-10 / D-09).
    // Using a counter (not a simple bool) handles the case where two drags
    // start before the first ends (rare, but avoids a stuck-high bug).
    property int  _activeDragCount: 0
    // anyDragActive: true during any cell-to-cell drag OR any KeyBindingList row drag.
    // DragRelay.active covers both (all drag sources call DragRelay.begin/finish).
    readonly property bool anyDragActive: _activeDragCount > 0 || DragRelay.active

    // ---- Layout JSON loader (D-06, D-07) ------------------------------------
    // Fetches qrc:/qt/qml/AjazzControlCenter/device-layouts/<codename>.json
    // asynchronously (WR-05: sync XHR deprecated in Qt 6.8+; async avoids
    // blocking the QML main thread). Sets root._layout as a side effect;
    // bindings on _layout update when the async load completes.
    // JSON parse is wrapped in try/catch (T-26-11 mitigate).
    function loadLayout(cn) {
        if (cn === "") {
            root._layout = null;
            return;
        }
        var url = "qrc:/qt/qml/AjazzControlCenter/device-layouts/" + cn + ".json";
        var xhr = new XMLHttpRequest();   // WR-05/IN-04: standard constructor, not Qt.createQmlObject
        xhr.open("GET", url, true);       // async=true; avoids deprecated sync-XHR warning
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    try {
                        root._layout = JSON.parse(xhr.responseText);
                    } catch (e) {
                        // Parse failure -- fall through to null (D-07 outline-frame fallback).
                        root._layout = null;
                    }
                } else {
                    root._layout = null;
                }
            }
        };
        xhr.send();
        // _layout starts null; binding updates when the async request completes.
    }

    property var _layout: null

    onCodenameChanged: {
        root._layout = null;   // reset immediately so stale layout does not flicker
        loadLayout(root.codename);
    }

    // TODO(Phase 26 Plan 26-06): _hasPhoto gates per-SKU photo rendering. Currently
    // the outline-frame fallback is always active (v1 note, lines 22-25). When device-
    // layouts/ JSONs land with a "photo" key, this binding activates the photo layer.
    // Until then, _hasPhoto is computed but not yet consumed by any visual element.
    readonly property bool _hasPhoto: _layout !== null && _layout.photo !== undefined
                                       && _layout.photo !== ""

    // ---- Three-column outer layout -----------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // Left/center column (OpenDeck single-view): device canvas on top,
        // Property Inspector docked beneath it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            // Device chassis area (center, fills remaining height).
            FocusScope {
                id: chassisScope
                Layout.fillWidth: true
                Layout.fillHeight: true

            Accessible.role: Accessible.Table
            Accessible.name: root.codename !== ""
                ? qsTr("%1 device editor").arg(root.codename)
                : qsTr("Device editor")

            // Chassis content: photo path OR outline-frame fallback.
            // UI-REVIEW.md fix: D-04 photo background now actually renders.
            // When _hasPhoto is true (layout JSON declares a photo, Plan 26-06
            // shipped 17 layout JSONs), the Image{} below provides the Elgato-
            // pattern background and the outlineFrame Rectangle yields the
            // visual to the photo. When _hasPhoto is false (layout missing,
            // photo field absent, or image fails to load), D-07 outline-frame
            // remains the always-renders fallback.
            Item {
                id: chassisArea
                anchors.fill: parent

                // ---- D-04 photo background (Elgato-pattern primary visual) ----
                // Renders behind the cell-grid Flickable when a layout JSON declares
                // a photo and the asset resolves. fillMode = PreserveAspectFit so
                // the photo respects its native aspect ratio inside the chassis area.
                Image {
                    id: chassisPhoto
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    visible: root._hasPhoto && status === Image.Ready
                    // Photos live under the qrc tree at icons/devices/products/
                    // per components/DeviceImage.qml convention (CMake auto-globs
                    // resources/devices/products/product-<codename>.png there).
                    source: root._hasPhoto
                        ? "qrc:/qt/qml/AjazzControlCenter/icons/devices/products/" + root._layout.photo
                        : ""
                    // Accessibility: photo is decorative; cell-grid below carries
                    // the actionable Accessible descriptions.
                    Accessible.ignored: true
                }

                // ---- Device-shaped schematic (D-07 always-renders fallback) ----
                // Geometry-driven OpenDeck-style canvas: a centered device frame
                // with the physical Stream Dock stack — key grid on top, the
                // horizontal touch strip beneath it, rotary dials at the bottom
                // (one under each strip zone). Active whenever no per-SKU photo is
                // available; the editor is NEVER broken (renders from descriptor
                // geometry alone). All cell intent is forwarded to the same
                // ProfileController + selection wiring as before.
                DeviceCanvas {
                    id: deviceCanvas
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    visible: !chassisPhoto.visible

                    keyRows:        root.keyRowsResolved
                    keyColumns:     root._keyColumnsResolved
                    keyCount:       root.keyCount
                    encoderCount:   root.encoderCount
                    touchZoneCount: root.touchZoneCount
                    deviceName:     root.codename
                    bindings:       bindings
                    selectedKeyIndex:     root.selectedKeyIndex
                    selectedEncoderIndex: root.selectedEncoderIndex
                    selectedZoneIndex:    root.selectedZoneIndex

                    // WR-02: drive the trash-zone opacity counter. Math.max guards
                    // against underflow if a cell is destroyed mid-drag.
                    onCellDragActiveChanged: function(active) {
                        root._activeDragCount = active
                            ? root._activeDragCount + 1
                            : Math.max(0, root._activeDragCount - 1);
                    }

                    onKeyClicked: function(index) {
                        root.selectedKeyIndex = index;
                        root.selectedEncoderIndex = -1;
                        root.selectedZoneIndex = -1;
                        root.keySelected(index);
                        root.keyActivated(index);
                    }
                    onKeySwapRequested: function(src, dst) {
                        // Move/swap a bound action between two keys. The atomic
                        // whole-Binding swap lives in C++ (ProfileController.
                        // swapKeyBindings) so a multi-action onPress chain survives
                        // the move intact — the old two-commitKeyBinding workaround
                        // collapsed it to a single action and dropped onRelease/
                        // onLongPress. The visual `bindings` model + the live key
                        // render are refreshed by the profileChanged() handler that
                        // rebuilds the model, and the plugin lifecycle (willDisappear
                        // on the vacated key, willAppear on the new one) is driven by
                        // the bridge's context reconcile on the same signal.
                        if (src < 0 || dst < 0 || src === dst) return;
                        ProfileController.swapKeyBindings(src, dst);
                    }

                    // Library -> key drop (Workstream B): update the live preview
                    // model AND commit. For a plugin action, payload.actionId is the
                    // dotted UUID the plugin host routes to; iconUrl is a file:// URL
                    // resolved from the plugin's manifest icon.
                    onKeyActionDropped: function(index, payload) {
                        if (index < 0 || index >= bindings.count) return;
                        var icon = payload.iconUrl ? payload.iconUrl : "";
                        var lbl = payload.label ? payload.label : "";
                        var aid = payload.actionId ? payload.actionId : "";
                        bindings.set(index, { iconSource: icon, label: lbl,
                                              actionKind: payload.actionKind, actionParams: "",
                                              actionId: aid });
                        ProfileController.commitKeyBinding(index, icon, lbl,
                                                           payload.actionKind, "", aid);
                        root.selectedKeyIndex = index;
                        root.selectedEncoderIndex = -1;
                        root.selectedZoneIndex = -1;
                        root.keySelected(index);
                    }

                    onEncoderClicked: function(index) {
                        root.selectedKeyIndex = -1;
                        root.selectedEncoderIndex = index;
                        root.selectedZoneIndex = -1;
                        root.encoderSelected(index);
                    }
                    onEncoderSwapRequested: function(src, dst) {
                        ProfileController.swapEncoderBindings(src, dst);
                    }

                    onZoneClicked: function(index) {
                        root.selectedKeyIndex = -1;
                        root.selectedEncoderIndex = -1;
                        root.selectedZoneIndex = index;
                        root.zoneSelected(index);
                    }
                    onZoneSwapRequested: function(src, dst) {
                        ProfileController.swapTouchZoneBindings(src, dst);
                    }
                }

                // ---- Trash zone (top-right of chassis, D-09) ---------------
                RoundButton {
                    id: trashBtn
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    width:  Theme.minTouchTarget
                    height: Theme.minTouchTarget
                    opacity: root.anyDragActive ? 1.0 : 0.3

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationShort; easing.type: Theme.easingStandard }
                    }

                    contentItem: Text {
                        anchors.centerIn: parent
                        font.family: "Material Symbols Outlined"
                        font.pixelSize: 22
                        text: "delete"
                        color: Theme.errorAccent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: trashBtn.width / 2
                        color: trashDropArea.containsDrag
                            ? Theme.chipBgError
                            : "transparent"
                        border.width: trashBtn.activeFocus ? Theme.focusRingWidth : 0
                        border.color: Theme.accent
                    }

                    DropArea {
                        id: trashDropArea
                        anchors.fill: parent
                        keys: ["application/x-ajazz-binding"]

                        onDropped: function(drop) {
                            if (DragRelay.mimeKey === "application/x-ajazz-binding") {
                                var bp = JSON.parse(DragRelay.payload);
                                if (bp.controller === "Keypad") {
                                    if (bp.actionPos !== undefined) {
                                        // PLUGIN-23: drag from KeyBindingList row — remove just
                                        // that action from the onPress chain (not the whole key binding).
                                        ProfileController.removeKeyActionAt(bp.position, bp.actionPos);
                                    } else {
                                        // Phase 26 v1: key-cell drag — clear the entire binding.
                                        ProfileController.commitKeyBinding(bp.position, "", "", 0, "");
                                        if (bp.position >= 0 && bp.position < bindings.count) {
                                            bindings.set(bp.position, {
                                                iconSource: "", label: "",
                                                actionKind: 0, actionParams: ""
                                            });
                                        }
                                    }
                                } else if (bp.controller === "TouchZone") {
                                    ProfileController.commitTouchZoneBinding(bp.position, "", "", 0, "");
                                }
                                drop.acceptProposedAction();
                            } else {
                                drop.accepted = false;
                            }
                        }
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Clear binding")
                    Accessible.description: qsTr("Drop a configured cell here to remove its binding")
                }

                // ---- Empty state (no keys configured) ----------------------
                EmptyState {
                    anchors.centerIn: parent
                    visible: root.keyCount === 0 && root.encoderCount === 0
                             && root.touchZoneCount === 0
                    title: qsTr("No layout data for this device")
                    body: qsTr("You can still configure keys using the grid below.")
                }
            }
        }

            // ---- Per-key multi-action binding list (PLUGIN-23, Phase 29-03) -----
            // Shown only when a key is selected AND it has >= 1 action in its
            // onPress chain. Surfaces the full action list for reorder/remove/append.
            // Hosted here in DeviceView, NOT inside Inspector.qml (merge-safety).
            KeyBindingList {
                objectName: "keyBindingList"
                Layout.fillWidth: true
                Layout.maximumHeight: 260
                visible: root.selectedKeyIndex >= 0
                         && root._selectedKeyActionList.length > 0
                keyIndex: root.selectedKeyIndex
                actionList: root._selectedKeyActionList
            }

            // Property Inspector docked at the bottom (OpenDeck layout).
            Inspector {
                Layout.fillWidth: true
                // Tall enough for the native form (icon + label + action type +
                // params) so it rarely needs to scroll; the Inspector clips +
                // scrolls internally so it never overflows onto the controls
                // below regardless of this value or the window size.
                Layout.preferredHeight: 320
                Layout.minimumHeight: 200
                selectionLabel: root.selectedKeyIndex >= 0
                    ? qsTr("Key %1").arg(root.selectedKeyIndex + 1)
                    : (root.selectedEncoderIndex >= 0
                       ? qsTr("Encoder %1").arg(root.selectedEncoderIndex + 1)
                       : (root.selectedZoneIndex >= 0
                          ? qsTr("Zone %1").arg(root.selectedZoneIndex + 1)
                          : ""))
                // Key OR dial binding, so the PI configures whichever control is
                // selected (goal: configure plugin settings for dials too).
                binding: root.selectedInspectorBinding
                // PLUGIN-22: feed the wire context inputs so the Inspector can
                // assemble the wire context id. Controller-aware: keyIndex for a
                // Keypad context (device#root#Keypad#row#col), encoderIndex for an
                // Encoder context (device#root#Encoder#0#col).
                deviceCodename: root.codename
                keyIndex: root.selectedKeyIndex
                encoderIndex: root.selectedEncoderIndex
                onBindingFieldChanged: function(field, value) {
                    root.updateSelectedBinding(field, value);
                }
            }
        }

        // Right: Action library sidebar (OpenDeck-style, fixed width).
        ActionLibraryPane {
            Layout.preferredWidth: 288
            Layout.fillHeight: true
        }
    }
}
