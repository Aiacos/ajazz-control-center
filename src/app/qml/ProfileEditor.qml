// SPDX-License-Identifier: GPL-3.0-or-later
//
// ProfileEditor.qml — middle pane of the main window.
//
// Hosts a TabBar (Keys / RGB / Encoders / Mouse) and switches between the
// matching panels. Each panel is wrapped in a Loader (F-28) so that only the
// currently visible tab is instantiated.
//
// Tabs are conditionally present based on the device's runtime capabilities
// (F-22): a keyboard hides "Keys" (it is not an LCD-key device), a stream
// deck hides "Mouse", an encoder-less stream deck hides "Encoders", etc.
//
// A sticky Apply / Revert footer (F-19, F-29) lives at the bottom and emits
// signals that ProfileController can wire to.
//
// Properties (driven by Main.qml):
//   * `codename`     — codename of the device currently being edited.
//   * `capabilities` — QVariantMap from DeviceModel.capabilitiesFor(codename).
//
// Emits:
//   * `applyRequested()`   — user clicked Apply.
//   * `revertRequested()`  — user clicked Revert.
//   * `restoreDefaultsRequested()` — user clicked Restore defaults.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Rectangle {
    id: root

    property string codename: ""
    property var    capabilities: ({})

    signal applyRequested()
    signal revertRequested()
    signal restoreDefaultsRequested()

    color: Theme.bgBase

    // ---- Capability shortcuts ----------------------------------------------
    readonly property int  _keyCount:      capabilities && capabilities.keyCount      ? capabilities.keyCount      : 0
    readonly property int  _gridColumns:   capabilities && capabilities.gridColumns   ? capabilities.gridColumns   : 5
    readonly property int  _encoderCount:  capabilities && capabilities.encoderCount  ? capabilities.encoderCount  : 0
    readonly property int  _dpiStageCount: capabilities && capabilities.dpiStageCount ? capabilities.dpiStageCount : 0
    readonly property bool _hasRgb:        capabilities && capabilities.hasRgb        ? capabilities.hasRgb        : false

    readonly property bool _showKeys:      _keyCount > 0
    readonly property bool _showRgb:       _hasRgb
    readonly property bool _showEncoders:  _encoderCount > 0
    readonly property bool _showMouse:     _dpiStageCount > 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        PageHeader {
            Layout.fillWidth: true
            title: root.codename === ""
                ? qsTr("Select a device on the left")
                : qsTr("Editing: %1").arg(root.codename)
            subtitle: capabilities && capabilities.model ? capabilities.model : ""
        }

        // Empty state when nothing is selected -------------------------------
        EmptyState {
            visible: root.codename === ""
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("No device selected")
            body: qsTr("Pick a device from the sidebar to see its keys, encoders, RGB, and pointer settings.")
        }

        // Tab strip + content ----------------------------------------------
        TabBar {
            id: tabs
            Layout.fillWidth: true
            visible: root.codename !== ""
            TabButton {
                text: qsTr("Keys")
                visible: root._showKeys
                width: visible ? implicitWidth : 0
            }
            TabButton {
                text: qsTr("RGB")
                visible: root._showRgb
                width: visible ? implicitWidth : 0
            }
            TabButton {
                text: qsTr("Encoders")
                visible: root._showEncoders
                width: visible ? implicitWidth : 0
            }
            TabButton {
                text: qsTr("Mouse")
                visible: root._showMouse
                width: visible ? implicitWidth : 0
            }
        }

        // Each Loader's `active` property is bound so only the visible panel
        // is instantiated at any time (F-28).
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.codename !== ""
            currentIndex: tabs.currentIndex

            Loader {
                active: stack.currentIndex === 0 && root._showKeys
                sourceComponent: keyDesignerComp
            }
            Loader {
                active: stack.currentIndex === 1 && root._showRgb
                sourceComponent: rgbPickerComp
            }
            Loader {
                active: stack.currentIndex === 2 && root._showEncoders
                sourceComponent: encoderPanelComp
            }
            Loader {
                active: stack.currentIndex === 3 && root._showMouse
                sourceComponent: mousePanelComp
            }
        }

        // Sticky footer with Apply / Revert / Restore defaults --------------
        Rectangle {
            Layout.fillWidth: true
            visible: root.codename !== ""
            height: 56
            color: Theme.bgSidebar
            radius: Theme.radiusMd

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                spacing: Theme.spacingSm

                SecondaryButton {
                    text: qsTr("Restore defaults")
                    onClicked: root.restoreDefaultsRequested()
                    accessibleDescription: qsTr("Reset every value on this tab to its factory default")
                }
                Item { Layout.fillWidth: true }
                SecondaryButton {
                    text: qsTr("Revert")
                    onClicked: root.revertRequested()
                    accessibleDescription: qsTr("Discard unsaved changes and reload the last saved profile")
                }
                PrimaryButton {
                    text: qsTr("Apply")
                    onClicked: root.applyRequested()
                    accessibleDescription: qsTr("Persist the current changes and push them to the device")
                }
            }
        }
    }

    // ---- Component definitions for the Loaders ----------------------------
    Component { id: keyDesignerComp; KeyDesigner  { keyCount: root._keyCount; gridColumns: root._gridColumns } }
    Component { id: rgbPickerComp;   RgbPicker    { } }
    Component { id: encoderPanelComp; EncoderPanel { encoderCount: root._encoderCount } }
    Component { id: mousePanelComp;  MousePanel   { dpiStageCount: root._dpiStageCount } }
}
