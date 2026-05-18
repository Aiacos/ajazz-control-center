// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsRow.qml — per-device row for the AK-series settings batch
// (opcode 0x07 sub 0x10 — issue #57).
//
// Three fields land in one 33-byte short report and are persisted to the
// firmware EEPROM:
//
//   * Fn-layer behaviour   — 0 = hold, 1 = toggle (Switch)
//   * Sleep timer minutes  — vendor values 0/1/3/5/10/30 (ComboBox)
//   * Key-response time    — level 1..5; higher = snappier (Slider)
//
// An "Apply" button commits the batch through SettingsService.setSettings,
// which fans out the 4-packet envelope (START / SETTINGS-DATA / SAVE /
// FINISH) on the I/O thread. Toast feedback bubbles up via the parent
// (the SettingsService signals are wired in Main.qml).
//
// Initial state seeds from SettingsService.currentSettings(codename) on
// Component.onCompleted so the controls reflect the last persisted batch
// the moment the tab opens — no HID round-trip required.
//
// Bind `deviceCodename` from the page that hosts this component (the
// ProfileEditor's per-device tab strip); the row refreshes its seed
// state whenever the binding changes.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Item {
    id: root

    /// Codename of the currently-edited device. Bound by the host page.
    property string deviceCodename: ""

    /// Cached availability + values map returned by SettingsService.
    /// `available` is true when the device is connected AND advertises
    /// ISettingsCapable; the numeric fields fall back to the vendor
    /// defaults (fn=0, sleep=0, response=3) otherwise.
    readonly property var snapshot:
        deviceCodename === "" ? ({available: false, fnSwitch: 0, sleepMinutes: 0, responseLevel: 3})
                              : SettingsService.currentSettings(deviceCodename)

    // Sleep timer vendor values exposed in the UI (matches AK980 PRO vendor
    // app dropdown). Indexed by ComboBox.currentIndex; map back through
    // _sleepValues[currentIndex] when reading.
    readonly property var _sleepValues: [0, 1, 3, 5, 10, 30]

    function _sleepIndexFor(minutes) {
        for (var i = 0; i < _sleepValues.length; ++i) {
            if (_sleepValues[i] === minutes) {
                return i;
            }
        }
        return 0;
    }

    // Seed the controls from the snapshot whenever the binding refreshes.
    // Component.onCompleted handles the initial mount; the explicit
    // `onSnapshotChanged` handler refreshes when the host swaps codenames.
    Component.onCompleted: {
        fnSwitch.checked = (root.snapshot.fnSwitch === 1);
        sleepBox.currentIndex = root._sleepIndexFor(root.snapshot.sleepMinutes);
        responseSlider.value = root.snapshot.responseLevel;
    }

    onSnapshotChanged: {
        fnSwitch.checked = (snapshot.fnSwitch === 1);
        sleepBox.currentIndex = _sleepIndexFor(snapshot.sleepMinutes);
        responseSlider.value = snapshot.responseLevel;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        Label {
            text: qsTr("Fn-layer behaviour")
            color: Theme.fgFaint
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Switch {
                id: fnSwitch
                text: checked ? qsTr("Toggle") : qsTr("Hold (default)")
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Fn-layer behaviour")
                Accessible.description:
                    qsTr("Off: Fn layer activates only while the key is held. "
                       + "On: tapping Fn toggles the layer on or off.")
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: qsTr("Sleep timer")
            color: Theme.fgFaint
        }
        ComboBox {
            id: sleepBox
            Layout.fillWidth: true
            model: [
                qsTr("Never"),
                qsTr("1 min"),
                qsTr("3 min"),
                qsTr("5 min"),
                qsTr("10 min"),
                qsTr("30 min")
            ]
            Accessible.role: Accessible.ComboBox
            Accessible.name: qsTr("Sleep timer")
            Accessible.description:
                qsTr("Minutes of idle before the firmware halts the backlight controller. "
                   + "Never disables the timer entirely.")
        }

        Label {
            text: qsTr("Key response time")
            color: Theme.fgFaint
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Slider {
                id: responseSlider
                from: 1
                to: 5
                stepSize: 1
                snapMode: Slider.SnapAlways
                Layout.fillWidth: true
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("Key response time")
                Accessible.description:
                    qsTr("Higher levels make the keyboard scan faster (snappier feel) "
                       + "but draw more battery on wireless connections.")
            }
            // Numeric chip mirrors the slider's current value so users can
            // confirm the exact level they're committing.
            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 28
                radius: Theme.radiusSm
                color: Theme.tile
                border.color: Theme.borderSubtle
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: Math.round(responseSlider.value).toString()
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.typeBodyMedium.pixelSize
                    font.weight: Font.Medium
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            Item { Layout.fillWidth: true }
            Button {
                id: applyButton
                text: qsTr("Apply")
                enabled: root.deviceCodename !== "" && root.snapshot.available
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Apply settings batch")
                Accessible.description:
                    qsTr("Send the new Fn / sleep / response values to the device "
                       + "in one firmware-persisted batch.")
                onClicked: {
                    SettingsService.setSettings(
                        root.deviceCodename,
                        fnSwitch.checked ? 1 : 0,
                        root._sleepValues[sleepBox.currentIndex],
                        Math.round(responseSlider.value));
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
