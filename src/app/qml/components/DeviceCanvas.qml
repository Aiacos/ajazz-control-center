// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceCanvas.qml -- geometry-driven, device-shaped schematic of a Stream Dock.
//
// Renders a faithful (OpenDeck-style) schematic of the active device from its
// descriptor geometry alone -- no per-SKU photo or layout JSON required:
//
//   ┌─ device frame ──────────────────────────────┐
//   │  <device name>                               │
//   │  [ keyRows × keyColumns LCD-key grid ]       │
//   │  [ touch strip: touchZoneCount zones ]       │  (only if touchZoneCount>0)
//   │  ( encoderCount dials, aligned under zones ) │  (only if encoderCount>0)
//   └──────────────────────────────────────────────┘
//
// The three lanes share a common content width so the strip spans the key grid
// and the dials sit under the strip zones -- matching the physical Stream Dock
// Plus stack (keys on top, horizontal touch strip, rotary knobs at the bottom).
//
// This component owns LAYOUT only. It takes the per-key `bindings` ListModel and
// the current selection, and emits intent signals (clicks, swaps, drag state)
// that the parent (DeviceView) wires to ProfileController + the Inspector. That
// keeps the canvas reusable across device families (AKP03/153/815/AKP05E): each
// renders correctly from its own keyRows/keyColumns/encoderCount/touchZoneCount.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter

Item {
    id: canvas

    // ---- Geometry (from DeviceDescriptor via DeviceView) -------------------
    property int    keyRows:        0
    property int    keyColumns:     0
    property int    keyCount:       0
    property int    encoderCount:   0
    property int    touchZoneCount: 0
    property string deviceName:     ""

    // ---- Data + selection (owned by DeviceView) ----------------------------
    property var bindings: null            // ListModel of {iconSource,label,actionKind,actionParams}
    property int selectedKeyIndex:     -1
    property int selectedEncoderIndex: -1
    property int selectedZoneIndex:    -1

    // ---- Intent signals (DeviceView wires these to ProfileController) ------
    signal keyClicked(int index)
    signal keySwapRequested(int src, int dst)
    signal encoderClicked(int index)
    signal encoderSwapRequested(int src, int dst)
    signal zoneClicked(int index)
    signal zoneSwapRequested(int src, int dst)
    signal cellDragActiveChanged(bool active)

    // ---- Sizing tokens -----------------------------------------------------
    // A single key cell drives the whole schematic; the strip + dial lanes are
    // sized off the resulting key-grid content width so everything aligns.
    readonly property int _keyCell: 76
    readonly property int _gap: Theme.spacingSm
    readonly property int _contentWidth:
        keyColumns > 0
            ? keyColumns * _keyCell + (keyColumns - 1) * _gap
            : _keyCell

    // Device frame: sized to its content, centered in the available chassis area.
    Rectangle {
        id: frame
        anchors.centerIn: parent
        width:  body.implicitWidth + 2 * Theme.spacingXl
        height: body.implicitHeight + 2 * Theme.spacingXl
        radius: Theme.radiusLg
        color: Theme.tile
        border.color: Theme.borderSubtle
        border.width: 1

        ColumnLayout {
            id: body
            anchors.centerIn: parent
            spacing: Theme.spacingLg

            // ---- Device name -------------------------------------------------
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: canvas.deviceName
                visible: canvas.deviceName !== ""
                color: Theme.fgSecondary
                font: Theme.typeLabelMedium
            }

            // ---- Lane 1: LCD-key grid ---------------------------------------
            Grid {
                Layout.alignment: Qt.AlignHCenter
                columns: canvas.keyColumns
                rows: canvas.keyRows
                rowSpacing: canvas._gap
                columnSpacing: canvas._gap
                visible: canvas.keyCount > 0

                Repeater {
                    // Integer model (like the strip/dials, which work). KeyCell's
                    // required iconSource/label are set explicitly from the bindings
                    // ListModel by index, so an empty/absent model still renders the
                    // grid as empty tiles instead of failing delegate creation.
                    model: canvas.keyCount
                    delegate: KeyCell {
                        // `index` is the type-level required property of KeyCell;
                        // the Repeater auto-assigns it. Do NOT redeclare it here
                        // (duplicate-property error → delegate fails to instantiate).
                        width: canvas._keyCell
                        height: canvas._keyCell
                        iconSource: (canvas.bindings && index < canvas.bindings.count)
                            ? canvas.bindings.get(index).iconSource : ""
                        label: (canvas.bindings && index < canvas.bindings.count)
                            ? canvas.bindings.get(index).label : ""
                        selected: canvas.selectedKeyIndex === index
                        onDragActiveChanged: (active) => canvas.cellDragActiveChanged(active)
                        onClicked: canvas.keyClicked(index)
                        onCellSwapRequested: (src, dst) => canvas.keySwapRequested(src, dst)
                    }
                }
            }

            // ---- Lane 2: touch strip (above the dials, Stream Dock Plus order) -
            // One horizontal LCD divided into `touchZoneCount` zones. Reuses the
            // standalone TouchStripLane; centered under the key grid.
            TouchStripLane {
                Layout.alignment: Qt.AlignHCenter
                visible: canvas.touchZoneCount > 0
                touchZoneCount: canvas.touchZoneCount
                zoneIconSources: []
                zoneLabels: []
                onZoneTapped: (idx) => canvas.zoneClicked(idx)
                onZoneSwapRequested: (src, dst) => canvas.zoneSwapRequested(src, dst)
                onZoneDragActiveChanged: (active) => canvas.cellDragActiveChanged(active)
            }

            // ---- Lane 3: rotary dials (one slot per zone, aligned) ----------
            Row {
                id: dialLane
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: canvas._contentWidth
                spacing: canvas._gap
                visible: canvas.encoderCount > 0

                Repeater {
                    model: canvas.encoderCount
                    delegate: Item {
                        required property int index
                        // Each dial occupies an equal slot of the content width
                        // so dial N sits under strip zone N (Stream Dock Plus).
                        width: canvas.encoderCount > 0
                            ? (canvas._contentWidth - (canvas.encoderCount - 1) * canvas._gap)
                              / canvas.encoderCount
                            : canvas._contentWidth
                        height: 76

                        EncoderDial {
                            anchors.centerIn: parent
                            width: 64
                            height: 64
                            index: parent.index
                            iconSource: ""
                            selected: canvas.selectedEncoderIndex === parent.index
                            onDragActiveChanged: (active) => canvas.cellDragActiveChanged(active)
                            onClicked: canvas.encoderClicked(parent.index)
                            onEncoderSwapRequested: (src, dst) => canvas.encoderSwapRequested(src, dst)
                        }
                    }
                }
            }
        }
    }
}
