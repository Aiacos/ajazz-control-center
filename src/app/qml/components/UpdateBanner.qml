// SPDX-License-Identifier: GPL-3.0-or-later
//
// UpdateBanner.qml -- top-of-window Material banner shown when
// AppUpdate.status === AppUpdate.UpdateAvailable.
//
// Compact horizontal row:
//   * Left: "Update available: <tag>" plus a short hint that the user
//     can click for release notes.
//   * Right: two buttons -- "Open release page" (primary,
//     Qt.openUrlExternally) and "Later" (AppUpdate.dismissCurrentUpdate()).
//
// On Linux .deb/.rpm distributions the "Open release page" affordance is
// replaced with the suggested system updater command line surfaced as a
// selectable TextEdit. The detection key is AppUpdate.platformLabel which
// the C++ service derives from FLATPAK_ID / APPIMAGE env vars + the
// compile-time Q_OS_* macros.
//
// All user-visible strings are ASCII-only per the project's TEST_CASE /
// banner-string discipline.

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Rectangle {
    id: root

    // Convenience: true when this build is a system-package Linux distro
    // (i.e. apt/dnf own the update flow). AppImage / Flatpak / source
    // builds keep the "Open release page" affordance.
    readonly property bool isSystemUpdaterDistro:
        AppUpdate.platformLabel === "Linux .deb/.rpm"

    // Suggested command lines surfaced for .deb/.rpm. Two lines so the
    // user can copy either; the hint label above tells them to pick the
    // one for their distro.
    readonly property string systemUpdaterCommand:
        "sudo apt upgrade ajazz-control-center\nsudo dnf upgrade ajazz-control-center"

    Layout.fillWidth: true
    Layout.preferredHeight: bannerRow.implicitHeight + Theme.spacingMd * 2
    color: Theme.accent2
    border.color: Theme.borderSubtle
    border.width: 1

    RowLayout {
        id: bannerRow
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        anchors.topMargin: Theme.spacingMd
        anchors.bottomMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            Label {
                Layout.fillWidth: true
                text: "Update available: " + AppUpdate.latestVersion
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeBodyMedium.pixelSize
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Label {
                Layout.fillWidth: true
                visible: !root.isSystemUpdaterDistro
                text: "Click 'Open release page' to view notes and download."
                color: Theme.fgMuted
                font.pixelSize: Theme.typeBodySmall.pixelSize
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                visible: root.isSystemUpdaterDistro
                text: "Run your system updater to upgrade. Suggested command:"
                color: Theme.fgMuted
                font.pixelSize: Theme.typeBodySmall.pixelSize
                wrapMode: Text.WordWrap
            }

            TextEdit {
                Layout.fillWidth: true
                visible: root.isSystemUpdaterDistro
                readOnly: true
                selectByMouse: true
                color: Theme.fgPrimary
                font.family: "monospace"
                font.pixelSize: Theme.typeBodySmall.pixelSize
                text: root.systemUpdaterCommand
                wrapMode: TextEdit.NoWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        Button {
            text: root.isSystemUpdaterDistro ? "View release notes"
                                             : "Open release page"
            onClicked: Qt.openUrlExternally(AppUpdate.latestReleaseUrl)
            Accessible.role: Accessible.Button
            Accessible.name: text
        }

        Button {
            text: "Later"
            flat: true
            onClicked: AppUpdate.dismissCurrentUpdate()
            Accessible.role: Accessible.Button
            Accessible.name: text
        }
    }
}
