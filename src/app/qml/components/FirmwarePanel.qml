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
            wrapMode: Text.WordWrap
            color: Theme.fgMuted
            font.pixelSize: Theme.fontSm
            text: qsTr("%1 is a clean-room reimplementation — it does not flash your device itself. "
                       + "The firmware update is performed by the official AJAZZ vendor tool.")
                  .arg(Branding.productName)
        }

        // Primary action: launch the installed vendor firmware tool. Shown only
        // when the tool is actually present on this machine.
        PrimaryButton {
            Layout.topMargin: Theme.spacingSm
            visible: root.toolInstalled
            text: qsTr("Update with vendor tool")
            accessibleDescription: qsTr("Closes this app's connection to the device, then starts the installed vendor firmware tool")
            onClicked: FirmwareUpdate.launchVendorTool(root.fwFamily)
        }

        // Fallback when the vendor tool is not installed: take the user to the
        // official firmware download page. (Auto-download replaces this next.)
        SecondaryButton {
            visible: !root.toolInstalled
            text: qsTr("Open firmware download page")
            accessibleDescription: qsTr("Opens the official AJAZZ firmware download page in your browser")
            onClicked: FirmwareUpdate.openFirmwareDownloadPage(root.fwFamily)
        }

        Item { Layout.fillHeight: true } // push content to the top
    }
}
