// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsRow.qml — the device "Settings" tab. Hosts, per capability:
//
//   * Support maturity (always) — the device's maturity tier as a compact
//     colour-coded chip + one-line description. Moved here from the sidebar
//     row hover tooltip (it was verbose and easy to miss); the Settings tab
//     is the single home for per-device info.
//
//   * Time synchronization (hasClock) — auto-sync toggle + manual "Sync now".
//     Moved here from the sidebar so the device's Settings tab is the single
//     home for per-device configuration. Auto-sync runs automatically on
//     connect and at app start (default on); this is the on/off + manual push.
//
//   * AK-series settings batch (hasSettings) — opcode 0x07 sub 0x10 (issue #57):
//       - Fn-layer behaviour   — 0 = hold, 1 = toggle (Switch)
//       - Sleep timer minutes  — vendor values 0/1/3/5/10/30 (ComboBox)
//       - Key-response time    — level 1..5; higher = snappier (Slider)
//     "Apply" commits the batch via SettingsService.setSettings (4-packet
//     envelope on the I/O thread); toast feedback bubbles up via Main.qml.
//
// Bind `deviceCodename`, `hasClock`, `hasSettings` from the host page
// (ProfileEditor's per-device tab strip).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Item {
    id: root

    /// Codename of the currently-edited device. Bound by the host page.
    property string deviceCodename: ""

    /// Capability gates from the host (ProfileEditor). Each section shows only
    /// when the device supports it.
    property bool hasSettings: false
    property bool hasClock: false

    /// Maturity tier of this device (5-tier vocabulary: scaffolded / probed /
    /// partial / functional / verified). Bound by the host; the maturity
    /// section is always shown.
    property string deviceMaturity: "scaffolded"

    // Tier → display metadata. Colours stay inside the existing Theme palette:
    // verified = success green, functional = brand accent, partial = warning
    // amber, probed/scaffolded = muted (work-in-progress, not alarmist).
    function _maturityColor(t) {
        if (t === "verified")   return Theme.successAccent;
        if (t === "functional") return Theme.accent;
        if (t === "partial")    return Theme.warningAccent;
        return Theme.fgMuted; // probed / scaffolded / unknown
    }
    function _maturityLabel(t) {
        if (t === "verified")   return qsTr("Verified");
        if (t === "functional") return qsTr("Functional");
        if (t === "partial")    return qsTr("Partial");
        if (t === "probed")     return qsTr("Probed");
        return qsTr("Scaffolded");
    }
    function _maturityDesc(t) {
        if (t === "verified")   return qsTr("Tested on real hardware.");
        if (t === "functional") return qsTr("All advertised features work.");
        if (t === "partial")    return qsTr("Some features work; others in progress.");
        if (t === "probed")     return qsTr("Protocol probed; wiring in progress.");
        return qsTr("Support not implemented yet.");
    }

    // Inline result of the last time-sync for THIS device ("" = none yet,
    // "ok", or an error message). Updated for both the manual "Sync now" click
    // and any automatic sync; reset when the host swaps to another device.
    property string _syncStatus: ""
    onDeviceCodenameChanged: _syncStatus = ""

    Connections {
        target: TimeSyncService
        function onSyncSucceeded(codename) {
            if (codename === root.deviceCodename) {
                root._syncStatus = "ok";
            }
        }
        function onSyncFailed(codename, message) {
            if (codename === root.deviceCodename) {
                root._syncStatus = message;
            }
        }
    }

    /// Cached availability + values map returned by SettingsService.
    /// `available` is true when the device is connected AND advertises
    /// ISettingsCapable; the numeric fields fall back to the vendor
    /// defaults (fn=0, sleep=0, response=3) otherwise.
    readonly property var snapshot:
        deviceCodename === "" ? ({available: false, fnSwitch: 0, sleepMinutes: 0, responseLevel: 3})
                              : SettingsService.currentSettings(deviceCodename)

    // Sleep timer vendor values exposed in the UI (matches AK980 PRO vendor
    // app dropdown). Indexed by ComboBox.currentIndex.
    readonly property var _sleepValues: [0, 1, 3, 5, 10, 30]

    function _sleepIndexFor(minutes) {
        for (var i = 0; i < _sleepValues.length; ++i) {
            if (_sleepValues[i] === minutes) {
                return i;
            }
        }
        return 0;
    }

    // Seed the settings controls from the snapshot whenever the binding refreshes.
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
        spacing: Theme.spacingLg

        // ---- Support maturity (always) -------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Label {
                text: qsTr("Support maturity")
                color: Theme.fgFaint
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                // Colour-coded tier chip.
                Rectangle {
                    id: maturityChip
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: chipLabel.implicitWidth + Theme.spacingMd * 2
                    implicitHeight: chipLabel.implicitHeight + Theme.spacingXs * 2
                    radius: Theme.radiusSm
                    readonly property color tierColor: root._maturityColor(root.deviceMaturity)
                    color: Qt.rgba(tierColor.r, tierColor.g, tierColor.b, 0.14)
                    border.width: 1
                    border.color: Qt.rgba(tierColor.r, tierColor.g, tierColor.b, 0.45)

                    Label {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: root._maturityLabel(root.deviceMaturity)
                        color: maturityChip.tierColor
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.Medium
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: root._maturityDesc(root.deviceMaturity)
                    color: Theme.fgMuted
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---- Time synchronization (hasClock) -------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.hasClock
            spacing: Theme.spacingSm

            Label {
                text: qsTr("Time synchronization")
                color: Theme.fgFaint
            }
            Switch {
                id: autoSyncSwitch
                text: qsTr("Auto-sync time on connect")
                checked: TimeSyncService.autoSync
                onToggled: TimeSyncService.autoSync = checked
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Auto-sync time on connect")
                Accessible.description:
                    qsTr("When on, the system time is pushed to this device automatically "
                       + "as soon as it connects and when the app starts.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Button {
                    text: qsTr("Sync time now")
                    enabled: root.deviceCodename !== ""
                    onClicked: TimeSyncService.setSystemTimeOn(root.deviceCodename)
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Sync time to this device now")
                    Accessible.description: qsTr("Push the current system time to this device's clock surface")
                }
                Label {
                    visible: root._syncStatus !== ""
                    text: root._syncStatus === "ok" ? qsTr("✓ Synced")
                                                     : qsTr("✗ %1").arg(root._syncStatus)
                    color: root._syncStatus === "ok" ? Theme.accent : Theme.fgMuted
                    font.pixelSize: Theme.fontSm
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }
            }
        }

        // ---- AK-series settings batch (hasSettings) ------------------------
        ColumnLayout {
            id: settingsSection
            Layout.fillWidth: true
            visible: root.hasSettings
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
        }

        Item { Layout.fillHeight: true }
    }
}
