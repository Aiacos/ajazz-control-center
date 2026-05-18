// SPDX-License-Identifier: GPL-3.0-or-later
//
// RGB lighting configuration tab.
//
// For devices that expose firmware-built-in effects via
// `IFirmwareLightingCapable` (currently AK980 PRO with its 20-mode 0x13
// catalogue), this picker dynamically populates with the device's actual
// mode list and wires changes through `LightingService.setMode()`. For
// other devices it falls back to the generic six-effect ComboBox so the
// page still renders something useful.
//
// Bind `deviceCodename` from the page that hosts this component (e.g.
// the per-device settings drawer); the tab refreshes its mode list when
// the binding changes.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Item {
    id: root

    /// Codename of the currently-edited device. Bound by the host page.
    property string deviceCodename: ""

    /// Cached list of {id, name} maps returned by LightingService.
    /// Empty means the device has no firmware-built-in modes; the
    /// generic ComboBox below covers that path.
    readonly property var firmwareModes:
        deviceCodename === "" ? [] : LightingService.modesFor(deviceCodename)

    readonly property int firmwareBrightnessMax:
        deviceCodename === "" ? 5 : LightingService.brightnessMaxFor(deviceCodename)

    readonly property int firmwareSpeedMax:
        deviceCodename === "" ? 5 : LightingService.speedMaxFor(deviceCodename)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // ---- Firmware-built-in modes (AK980 PRO etc.) ---------------------
        Loader {
            Layout.fillWidth: true
            active: root.firmwareModes.length > 0
            sourceComponent: ColumnLayout {
                spacing: Theme.spacingSm

                Label {
                    text: qsTr("Firmware lighting mode")
                    color: Theme.fgFaint
                }

                ComboBox {
                    id: firmwareModeBox
                    Layout.fillWidth: true
                    textRole: "name"
                    valueRole: "id"
                    model: root.firmwareModes
                    Accessible.role: Accessible.ComboBox
                    Accessible.name: qsTr("Firmware lighting mode")
                    Accessible.description:
                        qsTr("Select one of the device's built-in lighting effects.")
                    onActivated: {
                        var modeId = currentValue
                        if (modeId === undefined)
                            return
                        LightingService.setMode(
                            root.deviceCodename, modeId,
                            firmwareBrightnessSlider.value,
                            firmwareSpeedSlider.value)
                    }
                }

                Label {
                    text: qsTr("Brightness")
                    color: Theme.fgFaint
                }
                Slider {
                    id: firmwareBrightnessSlider
                    from: 0
                    to: root.firmwareBrightnessMax
                    stepSize: 1
                    value: Math.min(3, root.firmwareBrightnessMax)
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Brightness")
                    onValueChanged: {
                        if (firmwareModeBox.currentValue === undefined)
                            return
                        LightingService.setMode(
                            root.deviceCodename, firmwareModeBox.currentValue,
                            value, firmwareSpeedSlider.value)
                    }
                }

                Label {
                    text: qsTr("Speed")
                    color: Theme.fgFaint
                }
                Slider {
                    id: firmwareSpeedSlider
                    from: 0
                    to: root.firmwareSpeedMax
                    stepSize: 1
                    value: Math.min(3, root.firmwareSpeedMax)
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Animation speed")
                    onValueChanged: {
                        if (firmwareModeBox.currentValue === undefined)
                            return
                        LightingService.setMode(
                            root.deviceCodename, firmwareModeBox.currentValue,
                            firmwareBrightnessSlider.value, value)
                    }
                }
            }
        }

        // ---- Generic fallback (devices without firmware mode catalogue) ---
        Loader {
            Layout.fillWidth: true
            active: root.firmwareModes.length === 0
            sourceComponent: ColumnLayout {
                spacing: Theme.spacingSm

                Label { text: qsTr("RGB effect"); color: Theme.fgFaint }
                ComboBox {
                    Layout.fillWidth: true
                    model: [ qsTr("Static"), qsTr("Breathing"), qsTr("Wave"),
                             qsTr("Reactive"), qsTr("Cycle"), qsTr("Custom") ]
                    Accessible.role: Accessible.ComboBox
                    Accessible.name: qsTr("RGB effect")
                }

                Label { text: qsTr("Brightness"); color: Theme.fgFaint }
                Slider { from: 0; to: 100; value: 80; Layout.fillWidth: true }

                Label { text: qsTr("Color"); color: Theme.fgFaint }
                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    radius: Theme.radiusMd
                    color: Theme.accent
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
