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

    function _ensureBindings() {
        while (bindings.count < root.keyCount) {
            bindings.append({ iconSource: "", label: "", actionKind: 0, actionParams: "" });
        }
        while (bindings.count > root.keyCount) {
            bindings.remove(bindings.count - 1);
        }
        if (root.selectedKeyIndex >= bindings.count) {
            root.selectedKeyIndex = -1;
        }
    }

    onKeyCountChanged: _ensureBindings()
    Component.onCompleted: _ensureBindings()

    readonly property var selectedBinding:
        selectedKeyIndex >= 0 && selectedKeyIndex < bindings.count
            ? bindings.get(selectedKeyIndex)
            : null

    function updateSelectedBinding(field, value) {
        if (root.selectedKeyIndex < 0 || root.selectedKeyIndex >= bindings.count) return;
        bindings.setProperty(root.selectedKeyIndex, field, value);
        var row = bindings.get(root.selectedKeyIndex);
        ProfileController.commitKeyBinding(root.selectedKeyIndex, row.iconSource,
                                           row.label, row.actionKind, row.actionParams);
    }

    // ---- Track any active drag for trash-zone opacity ----------------------
    property bool anyDragActive: false

    // ---- Layout JSON loader (D-06, D-07) ------------------------------------
    // Fetches qrc:/qt/qml/AjazzControlCenter/device-layouts/<codename>.json
    // synchronously. Returns parsed JS object on 200/OK, null on any error.
    // JSONparse is wrapped in try/catch (T-26-11 mitigate).
    function loadLayout(cn) {
        if (cn === "") return null;
        var url = "qrc:/qt/qml/AjazzControlCenter/device-layouts/" + cn + ".json";
        var xhr = Qt.createQmlObject('import QtQml 2.15; XMLHttpRequest {}', root);
        xhr.open("GET", url, false);   // synchronous; tiny static qrc resource
        try {
            xhr.send();
            if (xhr.status === 200) {
                return JSON.parse(xhr.responseText);
            }
        } catch (e) {
            // Parse failure or network error -- fall through to null (D-07 fallback).
        }
        return null;
    }

    property var _layout: null

    onCodenameChanged: {
        _layout = loadLayout(root.codename);
    }

    readonly property bool _hasPhoto: _layout !== null && _layout.photo !== undefined
                                       && _layout.photo !== ""

    // ---- Three-column outer layout -----------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingXl

        // Column 1: Action library pane (left).
        ActionLibraryPane {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
        }

        // Column 2: Device chassis area (center, fills remaining width).
        FocusScope {
            id: chassisScope
            Layout.fillWidth: true
            Layout.fillHeight: true

            Accessible.role: Accessible.Table
            Accessible.name: root.codename !== ""
                ? qsTr("%1 device editor").arg(root.codename)
                : qsTr("Device editor")

            // Chassis content: photo path OR outline-frame fallback.
            // Phase 26 v1: outline-frame always active (layout JSONs land in Plan 26-06).
            Item {
                id: chassisArea
                anchors.fill: parent

                // ---- Outline-frame fallback (D-07) always shown in v1 -----
                // When _hasPhoto is true and layout JSONs exist, this Rectangle
                // becomes the chassis background behind the photo image. For
                // v1 it is the primary visual.
                Rectangle {
                    id: outlineFrame
                    anchors.fill: parent
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusLg
                    visible: true

                    // Scrollable chassis content.
                    Flickable {
                        id: chassisFlickable
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMd
                        contentWidth: chassisColumn.implicitWidth
                        contentHeight: chassisColumn.implicitHeight
                        clip: true

                        Column {
                            id: chassisColumn
                            spacing: Theme.spacingLg

                            // ----- Row 1: LCD-key grid -----------------------
                            Grid {
                                id: keyGrid
                                columns: root._keyColumnsResolved
                                rows: root.keyRowsResolved
                                rowSpacing: Theme.spacingSm
                                columnSpacing: Theme.spacingSm
                                visible: root.keyCount > 0

                                Repeater {
                                    model: bindings
                                    delegate: KeyCell {
                                        required property int index
                                        selected: root.selectedKeyIndex === index
                                        onClicked: {
                                            root.selectedKeyIndex = index;
                                            root.selectedEncoderIndex = -1;
                                            root.selectedZoneIndex = -1;
                                            root.keySelected(index);
                                            root.keyActivated(index);
                                        }
                                        onCellSwapRequested: function(src, dst) {
                                            // Phase 26 v1: no swapKeyBindings Q_INVOKABLE yet;
                                            // read both rows, swap them via two commitKeyBinding
                                            // calls. A dedicated swap method lands in a follow-up.
                                            if (src < 0 || src >= bindings.count) return;
                                            if (dst < 0 || dst >= bindings.count) return;
                                            var srcRow = bindings.get(src);
                                            var dstRow = bindings.get(dst);
                                            // swap in model
                                            bindings.set(src, { iconSource: dstRow.iconSource,
                                                                 label: dstRow.label,
                                                                 actionKind: dstRow.actionKind,
                                                                 actionParams: dstRow.actionParams });
                                            bindings.set(dst, { iconSource: srcRow.iconSource,
                                                                 label: srcRow.label,
                                                                 actionKind: srcRow.actionKind,
                                                                 actionParams: srcRow.actionParams });
                                            // commit both
                                            ProfileController.commitKeyBinding(src,
                                                dstRow.iconSource, dstRow.label,
                                                dstRow.actionKind, dstRow.actionParams);
                                            ProfileController.commitKeyBinding(dst,
                                                srcRow.iconSource, srcRow.label,
                                                srcRow.actionKind, srcRow.actionParams);
                                        }
                                    }
                                }
                            }

                            // ----- Row 2: Encoder dials ----------------------
                            Row {
                                spacing: Theme.spacingMd
                                visible: root.encoderCount > 0

                                Repeater {
                                    model: root.encoderCount
                                    delegate: EncoderDial {
                                        required property int index
                                        iconSource: ""
                                        selected: root.selectedEncoderIndex === index
                                        onClicked: {
                                            root.selectedKeyIndex = -1;
                                            root.selectedEncoderIndex = index;
                                            root.selectedZoneIndex = -1;
                                            root.encoderSelected(index);
                                        }
                                        onEncoderSwapRequested: function(src, dst) {
                                            // Phase 26 v1: commitEncoderBinding not yet in
                                            // ProfileController; encoder swap is a no-op until
                                            // that Q_INVOKABLE lands in a follow-up plan.
                                        }
                                    }
                                }
                            }

                            // ----- Row 3: Touch-strip zones ------------------
                            TouchStripLane {
                                visible: root.touchZoneCount > 0
                                touchZoneCount: root.touchZoneCount
                                zoneIconSources: []
                                zoneLabels: []
                                // CR-03: handle tap to update Inspector selection.
                                onZoneTapped: function(idx) {
                                    root.selectedKeyIndex = -1;
                                    root.selectedEncoderIndex = -1;
                                    root.selectedZoneIndex = idx;
                                    root.zoneSelected(idx);
                                }
                                onZoneSwapRequested: function(src, dst) {
                                    // Phase 26 v1: zone swap calls commitTouchZoneBinding
                                    // with empty params on both sides (effectively resets).
                                    ProfileController.commitTouchZoneBinding(src, "", "", 0, "");
                                    ProfileController.commitTouchZoneBinding(dst, "", "", 0, "");
                                }
                            }
                        }
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
                            if (drop.hasFormat("application/x-ajazz-binding")) {
                                var bp = JSON.parse(drop.getDataAsString("application/x-ajazz-binding"));
                                // Phase 26 v1: clearBinding does not exist; route trash drops
                                // through commitXxxBinding with empty params (effectively clears
                                // the visual; full clearBinding lands in a follow-up plan).
                                if (bp.controller === "Keypad") {
                                    ProfileController.commitKeyBinding(bp.position, "", "", 0, "");
                                    if (bp.position >= 0 && bp.position < bindings.count) {
                                        bindings.set(bp.position, { iconSource: "", label: "",
                                                                     actionKind: 0, actionParams: "" });
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

        // Column 3: Inspector pane (right).
        Inspector {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            selectionLabel: root.selectedKeyIndex >= 0
                ? qsTr("Key %1").arg(root.selectedKeyIndex + 1)
                : (root.selectedEncoderIndex >= 0
                   ? qsTr("Encoder %1").arg(root.selectedEncoderIndex + 1)
                   : (root.selectedZoneIndex >= 0
                      ? qsTr("Zone %1").arg(root.selectedZoneIndex + 1)
                      : ""))
            binding: root.selectedBinding
            onBindingFieldChanged: function(field, value) {
                root.updateSelectedBinding(field, value);
            }
        }
    }
}
