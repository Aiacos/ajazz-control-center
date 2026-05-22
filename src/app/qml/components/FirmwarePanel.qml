// SPDX-License-Identifier: GPL-3.0-or-later
//
// FirmwarePanel.qml — the "Firmware" tab content in the device pane.
//
// Replaces the old sidebar button + modal dialog: firmware lives inside the
// device editor, next to Keys / RGB / Encoders / Mouse / Settings.
//
// Current behaviour is delegate-only (detect + launch the installed vendor
// tool, or open the vendor download page). The auto-download flow (fetch the
// firmware from the vendor endpoint, no browser) lands in a follow-up commit
// and will plug into the primary action here.
//
// Properties (set by ProfileEditor):
//   * deviceCodename — codename of the selected device.
//   * deviceFamily   — coarse core DeviceFamily int (resolved to a
//                      FirmwareUpdate.Family below).
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter

Item {
    id: root

    property string deviceCodename: ""
    property int deviceFamily: 0

    // Granular firmware family (Stream Dock / Keyboard / AJ159 / AJ199) used by
    // the FirmwareUpdate singleton's URL + vendor-tool lookups.
    readonly property int fwFamily: FirmwareUpdate.familyForDevice(deviceFamily, deviceCodename)
    readonly property bool toolInstalled: FirmwareUpdate.isVendorToolInstalled(fwFamily)

    // Running firmware version, read once when the tab opens (firmwareVersion()
    // may do a HID round-trip, so we don't put it in a binding). Empty/"unknown"
    // when the device is offline or doesn't answer.
    property string installedVersion: ""
    Component.onCompleted: installedVersion = FirmwareUpdate.runningFirmwareVersion(deviceCodename)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        Text {
            text: qsTr("Firmware")
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontLg
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontMd
            text: {
                var v = root.installedVersion;
                var shown = (v === "" || v === "unknown") ? qsTr("unknown") : v;
                return qsTr("Installed firmware: %1").arg(shown);
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.fgMuted
            font.pixelSize: Theme.fontSm
            text: root.toolInstalled
                ? qsTr("%1 doesn't flash the device itself. Updating launches the official "
                       + "AJAZZ app, which checks, downloads and flashes the firmware "
                       + "automatically — we just hand off the device to it.")
                      .arg(Branding.productName)
                : qsTr("%1 doesn't flash the device itself — the official AJAZZ app does. "
                       + "Install it to update this device's firmware.")
                      .arg(Branding.productName)
        }

        // Primary action: hand off to the vendor's MAIN app, which performs the
        // whole update automatically. Shown only when that app is installed.
        PrimaryButton {
            Layout.topMargin: Theme.spacingSm
            visible: root.toolInstalled
            text: qsTr("Update firmware")
            accessibleDescription: qsTr("Closes this app's connection to the device, then launches the official AJAZZ app, which updates the firmware automatically")
            onClicked: FirmwareUpdate.launchVendorTool(root.fwFamily)
        }

        // Fallback when the vendor app is not installed: send the user to the
        // official page to install it.
        SecondaryButton {
            visible: !root.toolInstalled
            text: qsTr("Get the AJAZZ app")
            accessibleDescription: qsTr("Opens the official AJAZZ download page so you can install the app that updates firmware")
            onClicked: FirmwareUpdate.openFirmwareDownloadPage(root.fwFamily)
        }

        Item { Layout.fillHeight: true } // push content to the top
    }
}
