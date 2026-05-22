// SPDX-License-Identifier: GPL-3.0-or-later
//
// FirmwareUpdateDialog.qml — the "Update firmware…" modal.
//
// Implements the user-facing surface of the DECIDED firmware posture
// (docs/architecture/FIRMWARE-UPDATES.md): we do NOT distribute firmware and
// we do NOT flash. The dialog explains that, then offers two delegate-only
// actions wired to the `FirmwareUpdate` C++ singleton:
//
//   * [Launch vendor app]            — only when the vendor firmware tool is
//                                      actually installed for this family.
//   * [Open firmware download page]  — always available; opens the pinned
//                                      per-family vendor URL in the browser.
//   * [Close]                        — standard button.
//
// Open it via openFor(familyValue, deviceName), where familyValue is a
// FirmwareUpdate.Family (resolve it with FirmwareUpdate.familyForDevice(...)).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter

Dialog {
    id: dlg

    // FirmwareUpdate.Family of the target device; set by openFor().
    property int family: FirmwareUpdate.Unknown
    // Human-readable model name for the disclaimer body; may be empty.
    property string deviceName: ""

    function openFor(familyValue, name) {
        dlg.family = familyValue;
        dlg.deviceName = name;
        dlg.open();
    }

    title: qsTr("Update firmware")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.spacingXl * 2 : 480, 480)
    standardButtons: Dialog.Close

    // Per CLAUDE.md Qt gotcha: Material attached properties don't cross the
    // Popup scope, so set the theme inside the contentItem, not on the root.
    contentItem: ColumnLayout {
        spacing: Theme.spacingMd
        Material.theme: ThemeService.effectiveMode === "dark" ? Material.Dark : Material.Light

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontSm
            text: dlg.deviceName !== ""
                ? qsTr("%1 is a clean-room reimplementation — it does not distribute firmware and does not flash your device. Use the official AJAZZ vendor application to update %2.")
                    .arg(Branding.productName).arg(dlg.deviceName)
                : qsTr("%1 is a clean-room reimplementation — it does not distribute firmware and does not flash your device. Use the official AJAZZ vendor application to update your device.")
                    .arg(Branding.productName)
        }

        // Only offered when the vendor tool is detected on disk; otherwise the
        // download page is the only honest action.
        PrimaryButton {
            Layout.fillWidth: true
            visible: FirmwareUpdate.isVendorToolInstalled(dlg.family)
            text: qsTr("Launch vendor app")
            accessibleDescription: qsTr("Closes this app's connection to the device, then starts the installed vendor firmware tool")
            onClicked: {
                FirmwareUpdate.launchVendorTool(dlg.family);
                dlg.close();
            }
        }

        SecondaryButton {
            Layout.fillWidth: true
            text: qsTr("Open firmware download page")
            accessibleDescription: qsTr("Opens the official AJAZZ firmware download page in your browser")
            onClicked: {
                FirmwareUpdate.openFirmwareDownloadPage(dlg.family);
                dlg.close();
            }
        }
    }
}
