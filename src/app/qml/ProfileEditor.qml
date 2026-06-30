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
    readonly property int  _encoderCount:  capabilities && capabilities.encoderCount  ? capabilities.encoderCount  : 0
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

    readonly property bool _showRgb:       _hasRgb
    readonly property bool _showMouse:     _dpiStageCount > 0
    // The Settings tab hosts per-device Time-sync, the AK-series batch, AND the
    // device maturity tier. Maturity applies to every catalogued device, so the
    // tab is always present once a device is selected.
    readonly property bool _showSettings:  true
    // Every device has firmware, so the Firmware tab is always present.
    readonly property bool _showFirmware:  true

    // Stream controllers (Stream Dock keys / encoders / touch strip) are edited
    // in the embedded OpenDeck SPA (OpenDeckPane) instead of the native tabs;
    // mice and keyboards keep the native tabs below. A device is a stream
    // controller when it exposes any renderable key / encoder / touch surface.
    readonly property bool _isStreamController: _keyCount > 0 || _encoderCount > 0 || _touchZoneCount > 0

    // Native editor (mouse + keyboard tabs; also the "select a device" empty
    // state). Hidden for stream controllers — the OpenDeck pane below takes over.
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd
        visible: !root._isStreamController

        // Header — restructured so the human model name sits on line 1 next to
        // the device's product image, with the machine codename on line 2.
        //
        // Empty state: when nothing is selected we fall back to the plain
        // PageHeader prompt (no image, no codename line).
        PageHeader {
            Layout.fillWidth: true
            visible: root.codename === ""
            title: qsTr("Select a device")
        }

        // Profile switcher bar (Workstream D) — device-scoped. Carries the
        // profile selector ("Default" dropdown) + New/Rename/Duplicate/Delete/
        // Export/Import actions. The profile dropdown belongs here (with the
        // Stream Dock it configures), NOT in the global top bar.
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
            body: qsTr("Pick a device from the top bar to see its keys, encoders, RGB, and pointer settings.")
        }

        // Tab strip + content ----------------------------------------------
        TabBar {
            id: tabs
            objectName: "deviceEditorTabs" // debug-channel addressable (qml.set currentIndex)
            Layout.fillWidth: true
            visible: root.codename !== ""
            // No "Keys" tab: stream controllers are edited in the embedded
            // OpenDeck pane above, never in these native tabs (mouse/keyboard).
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
                active: stack.currentIndex === 0 && root._showRgb
                sourceComponent: rgbPickerComp
            }
            Loader {
                active: stack.currentIndex === 1 && root._showMouse
                sourceComponent: mousePanelComp
            }
            Loader {
                active: stack.currentIndex === 2 && root._showSettings
                sourceComponent: settingsRowComp
            }
            Loader {
                active: stack.currentIndex === 3 && root._showFirmware
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

    // Embedded OpenDeck SPA — the streamdeck editor. Fills the whole pane and
    // covers the native ColumnLayout (which is hidden for stream controllers).
    // Loaded by string `source` so builds without Qt WebEngine (no OpenDeckPane
    // in the module) don't fault on a missing type — they just log and show the
    // native editor's empty area. The Loader stays alive across device switches
    // so the SPA isn't reloaded every time; the OpenDeck top bar drives which
    // Stream Dock is active when more than one is connected.
    Loader {
        id: openDeckLoader
        anchors.fill: parent
        active: root._isStreamController && root.codename !== ""
        visible: active
        source: active ? "OpenDeckPane.qml" : ""
        onStatusChanged: {
            if (status === Loader.Error)
                console.error("ProfileEditor: failed to load OpenDeckPane.qml —",
                              "is the app built with -DAJAZZ_BUILD_WEBUI=ON?")
        }
    }

    // ---- Component definitions for the Loaders (mouse + keyboard tabs) -----

    Component { id: rgbPickerComp;   RgbPicker    { deviceCodename: root.codename } }
    Component { id: mousePanelComp;  MousePanel   { dpiStageCount: root._dpiStageCount } }
    Component { id: settingsRowComp; SettingsRow  { deviceCodename: root.codename; hasSettings: root._hasSettings; hasClock: root._hasClock; deviceMaturity: root._maturity } }
    Component { id: firmwarePanelComp; FirmwarePanel { deviceCodename: root.codename; deviceFamily: root._family } }
}
