// SPDX-License-Identifier: GPL-3.0-or-later
//
// AppHeader.qml — top application bar (F-20).
//
// Hosts the product mark, an optional profile picker, a search field, and a
// "minimize to tray" button. Designed to span the full width of the window;
// the parent ApplicationWindow places it as the first child of a ColumnLayout
// so it stays sticky at the top.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Rectangle {
    id: root
    color: Theme.bgSidebar
    border.width: 0
    height: 56

    /// Emitted when the user clicks the "minimize to tray" button.
    signal minimizeRequested()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingLg

        // Product mark — square app icon (the same artwork used in the README and the
        // OS tray/installer) followed by either the wordmark banner or the product name.
        // Keeping the app icon as the leading element ensures the in-app system bar shows
        // the same brand mark users see at install time and on the desktop / dock.
        Image {
            id: appMark
            source: branding ? branding.appIconUrl
                             : "qrc:/qt/qml/AjazzControlCenter/branding/app.svg"
            Layout.preferredHeight: 32
            Layout.preferredWidth: 32
            sourceSize.width: 64    // Render at 2x for crisp scaling on HiDPI displays.
            sourceSize.height: 64
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            asynchronous: true
            cache: true
            Accessible.role: Accessible.Graphic
            Accessible.name: branding ? branding.productName + qsTr(" application icon")
                                      : qsTr("AJAZZ Control Center application icon")
        }

        // Optional wordmark banner shown next to the app icon when the integrator ships
        // one. Hidden gracefully when the override branding directory ships no logo.
        Image {
            id: brandLogo
            source: "qrc:/qt/qml/AjazzControlCenter/branding/ajazz-logo.png"
            // Constrain to header height while preserving the wordmark's native aspect.
            Layout.preferredHeight: 28
            Layout.preferredWidth: status === Image.Ready ? (28 * sourceSize.width / Math.max(1, sourceSize.height)) : 0
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: status === Image.Ready
            asynchronous: true
            cache: true
            Accessible.role: Accessible.Graphic
            Accessible.name: branding ? branding.vendorName + " " + branding.productName : qsTr("AJAZZ Control Center")
        }

        Text {
            text: branding ? branding.productName : qsTr("AJAZZ Control Center")
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontLg
            font.bold: true
            // Hide the redundant product-name text when the wordmark image is rendered,
            // since the banner already contains the product name. Falls back to text
            // for branded builds that ship a non-wordmark logo (the square app icon
            // alone is not self-explanatory).
            visible: !brandLogo.visible
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Item { Layout.fillWidth: true }

        // Search box (no functional binding yet; surfaces a placeholder).
        TextField {
            id: search
            Layout.preferredWidth: 240
            placeholderText: qsTr("Search devices, actions…")
            color: Theme.fgPrimary
            placeholderTextColor: Theme.fgMuted
            background: Rectangle {
                radius: Theme.radiusMd
                color: Theme.tile
                border.width: search.activeFocus ? Theme.focusRingWidth : 1
                border.color: search.activeFocus ? Theme.accent : Theme.borderSubtle
            }
            Accessible.role: Accessible.EditableText
            Accessible.name: qsTr("Search")
            Accessible.description: qsTr("Filter the device list by name")
        }

        // Minimize-to-tray button.
        ToolButton {
            id: minBtn
            text: "—"
            font.pixelSize: Theme.fontLg
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Minimize to tray")
            onClicked: root.minimizeRequested()
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Minimize to tray")
            Accessible.description: qsTr("Hide the window; the application keeps running in the system tray")
            background: Rectangle {
                radius: Theme.radiusMd
                color: minBtn.hovered ? Theme.bgRowHover : "transparent"
                border.width: minBtn.activeFocus ? Theme.focusRingWidth : 0
                border.color: Theme.accent
            }
            contentItem: Text {
                text: minBtn.text
                color: Theme.fgPrimary
                font: minBtn.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Bottom hairline separator.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.borderSubtle
    }
}
