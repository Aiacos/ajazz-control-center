// SPDX-License-Identifier: GPL-3.0-or-later
//
// ProfileEditor.qml — middle pane of the main window.
//
// Hosts a TabBar (Keys / RGB / Mouse / Settings / Firmware) and switches
// between the matching panels. Each panel is wrapped in a Loader (F-28) so
// that only the currently visible tab is instantiated.
//
// Encoders have no dedicated tab: they are edited in-place as the rotary dials
// on the device canvas inside the Keys tab (DeviceCanvas Lane 3).
//
// Tabs are conditionally present based on the device's runtime capabilities
// (F-22): a keyboard hides "Keys" (it is not an LCD-key device), a stream
// deck hides "Mouse", a non-RGB device hides "RGB", etc.
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
pragma ComponentBehavior: Bound
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
    readonly property int  _keyRows:       capabilities && capabilities.keyRows       ? capabilities.keyRows       : 0
    readonly property int  _touchZoneCount: capabilities && capabilities.touchZoneCount ? capabilities.touchZoneCount : 0
    readonly property int  _dpiStageCount: capabilities && capabilities.dpiStageCount ? capabilities.dpiStageCount : 0
    readonly property bool _hasRgb:        capabilities && capabilities.hasRgb        ? capabilities.hasRgb        : false
    readonly property bool _hasSettings:   capabilities && capabilities.hasSettings   ? capabilities.hasSettings   : false
    readonly property bool _hasClock:      capabilities && capabilities.hasClock      ? capabilities.hasClock      : false
    // Maturity tier string (5-tier vocabulary: scaffolded / probed / partial /
    // functional / verified). Shown in the Settings tab; defaults to scaffolded
    // for an unknown codename so the view always has a valid tier.
    readonly property string _maturity:    capabilities && capabilities.maturity     ? capabilities.maturity      : "scaffolded"

    // Coarse core DeviceFamily int (from DeviceModel.capabilitiesFor) — fed to
    // the Firmware tab so it can resolve the FirmwareUpdate.Family.
    readonly property int  _family:        capabilities && capabilities.family !== undefined ? capabilities.family : 0

    readonly property bool _showKeys:      _keyCount > 0
    readonly property bool _showRgb:       _hasRgb
    // Encoders no longer have a dedicated tab: they are edited in-place as the
    // rotary dials on the device canvas inside the Keys tab (DeviceCanvas Lane 3).
    readonly property bool _showMouse:     _dpiStageCount > 0
    // The Settings tab hosts per-device Time-sync, the AK-series batch, AND the
    // device maturity tier. Maturity applies to every catalogued device, so the
    // tab is always present once a device is selected.
    readonly property bool _showSettings:  true
    // Every device has firmware, so the Firmware tab is always present.
    readonly property bool _showFirmware:  true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // Header — restructured so the human model name sits on line 1 next to
        // the device's product image, with the machine codename on line 2.
        //
        // Empty state: when nothing is selected we fall back to the plain
        // PageHeader prompt (no image, no codename line).
        PageHeader {
            Layout.fillWidth: true
            visible: root.codename === ""
            title: qsTr("Select a device on the left")
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.codename !== ""
            spacing: Theme.spacingMd

            // Product photo (remote, per-codename) with per-family SVG fallback.
            DeviceImage {
                Layout.alignment: Qt.AlignVCenter
                codename: root.codename
                family: root._family
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                // Line 1 — the human product NAME, large. Falls back to the
                // codename when the capability map carries no model string.
                Text {
                    Layout.fillWidth: true
                    text: root.capabilities && root.capabilities.model
                              ? root.capabilities.model
                              : root.codename
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontXl
                    font.bold: true
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }

                // Line 2 — "Editing: <machine codename>".
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Editing: %1").arg(root.codename)
                    color: Theme.fgMuted
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }
            }
        }

        // Profile switcher bar (Workstream D) --------------------------------
        // Profiles are device-scoped. Selecting a device activates that
        // device's profile (creating a "Default" the first time); this bar
        // lets the user switch between them and create/rename/duplicate/delete.
        ProfileBar {
            Layout.fillWidth: true
            visible: root.codename !== ""
            deviceCodename: root.codename
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
                text: qsTr("Mouse")
                visible: root._showMouse
                width: visible ? implicitWidth : 0
            }
            TabButton {
                text: qsTr("Settings")
                visible: root._showSettings
                width: visible ? implicitWidth : 0
            }
            TabButton {
                text: qsTr("Firmware")
                visible: root._showFirmware
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
                sourceComponent: deviceViewComp
            }
            Loader {
                active: stack.currentIndex === 1 && root._showRgb
                sourceComponent: rgbPickerComp
            }
            Loader {
                active: stack.currentIndex === 2 && root._showMouse
                sourceComponent: mousePanelComp
            }
            Loader {
                active: stack.currentIndex === 3 && root._showSettings
                sourceComponent: settingsRowComp
            }
            Loader {
                active: stack.currentIndex === 4 && root._showFirmware
                sourceComponent: firmwarePanelComp
            }
        }

        // Sticky footer with Apply / Revert / Restore defaults --------------
        Rectangle {
            Layout.fillWidth: true
            visible: root.codename !== ""
            Layout.preferredHeight: 56
            color: Theme.bgSidebar
            radius: Theme.radiusMd

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                spacing: Theme.spacingSm

                SecondaryButton {
                    objectName: "restoreDefaultsButton"
                    text: qsTr("Restore defaults")
                    onClicked: root.restoreDefaultsRequested()
                    accessibleDescription: qsTr("Reset every value on this tab to its factory default")
                }
                Item { Layout.fillWidth: true }
                SecondaryButton {
                    objectName: "revertButton"
                    text: qsTr("Revert")
                    onClicked: root.revertRequested()
                    accessibleDescription: qsTr("Discard unsaved changes and reload the last saved profile")
                }
                PrimaryButton {
                    objectName: "applyButton"
                    text: qsTr("Apply")
                    onClicked: root.applyRequested()
                    accessibleDescription: qsTr("Persist the current changes and push them to the device")
                }
            }
        }
    }

    // ---- Component definitions for the Loaders ----------------------------

    // Keys tab: DeviceView (geometry-driven three-row editor, REQ-26-B, Phase 26) +
    // live-device controls (DISPLAY-09, Phase 16).
    // The brightness Slider is debounced via a single-shot Timer (~80 ms) so
    // dragging does not flood LIG writes (T-16a-01). One final setBrightness is
    // issued on pointer release (onPressedChanged when !pressed). Both controls
    // are gated to LCD-key devices (_showKeys / codename != "").
    Component {
        id: deviceViewComp

        // Keys tab: DeviceView only. Its live-device controls (brightness +
        // clear-all) moved INTO the canvas header inside DeviceView (Stream
        // Deck layout: controls next to the canvas, not a separate bottom row).
        ColumnLayout {
            spacing: Theme.spacingSm

            DeviceView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                keyCount: root._keyCount
                gridColumns: root._gridColumns
                keyRows: root._keyRows
                encoderCount: root._encoderCount
                touchZoneCount: root._touchZoneCount
                codename: root.codename
                // Canvas-header device selector → re-point the editor at the
                // chosen device (same path as the sidebar selection in Main.qml).
                onDeviceChangeRequested: function(codename) {
                    if (codename === "" || codename === root.codename)
                        return;
                    StreamDockControlService.setActiveDevice(codename);
                    root.codename = codename;
                    root.capabilities = DeviceModel.capabilitiesFor(codename);
                }
            }
        }
    }

    Component { id: rgbPickerComp;   RgbPicker    { deviceCodename: root.codename } }
    Component { id: mousePanelComp;  MousePanel   { dpiStageCount: root._dpiStageCount } }
    Component { id: settingsRowComp; SettingsRow  { deviceCodename: root.codename; hasSettings: root._hasSettings; hasClock: root._hasClock; deviceMaturity: root._maturity } }
    Component { id: firmwarePanelComp; FirmwarePanel { deviceCodename: root.codename; deviceFamily: root._family } }
}
