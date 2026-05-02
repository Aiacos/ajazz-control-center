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

    /// Emitted when the user clicks the "Plugin Store" button. Main.qml
    /// listens for this and opens the Plugin Store drawer / page.
    signal pluginStoreRequested()

    /// Emitted when the user clicks the "Loaded" button — opens the
    /// runtime loaded-plugins drawer (SEC-003 #51 trust UI).
    signal loadedPluginsRequested()

    /// Emitted when the user clicks the "Settings" button. Main.qml listens
    /// for this and opens the Settings drawer.
    signal settingsRequested()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingLg

        // Product mark — wordmark image (banner) followed by the product name.
        // The wordmark is shipped at qrc:/qt/qml/AjazzControlCenter/branding/ajazz-logo.png;
        // hidden gracefully when the override branding directory ships no logo.
        Image {
            id: brandLogo
            source: "qrc:/qt/qml/AjazzControlCenter/branding/ajazz-logo.png"
            // Constrain to header height while preserving the 3:1 wordmark aspect.
            Layout.preferredHeight: 32
            Layout.preferredWidth: status === Image.Ready ? (32 * sourceSize.width / Math.max(1, sourceSize.height)) : 0
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: status === Image.Ready
            asynchronous: true
            cache: true
            Accessible.role: Accessible.Graphic
            Accessible.name: Branding.vendorName + " " + Branding.productName
        }

        Text {
            text: Branding.productName
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontLg
            font.bold: true
            // Hide the redundant product-name text when the wordmark image is rendered,
            // since the banner already contains the product name. Falls back to text
            // for branded builds that ship a non-wordmark logo.
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

        // Plugin Store button — opens the catalogue browser. Placed to the
        // left of the minimize button so it sits next to the search field
        // and is the most visible non-search header action.
        ToolButton {
            id: pluginsBtn
            text: qsTr("Plugins")
            font.pixelSize: Theme.fontMd
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Open the plugin store")
            onClicked: root.pluginStoreRequested()
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Plugin Store")
            Accessible.description: qsTr("Browse and install plugins")
            background: Rectangle {
                radius: Theme.radiusMd
                color: pluginsBtn.hovered ? Theme.bgRowHover : "transparent"
                border.width: pluginsBtn.activeFocus ? Theme.focusRingWidth : 0
                border.color: Theme.accent
            }
            contentItem: Text {
                text: pluginsBtn.text
                color: Theme.fgPrimary
                font: pluginsBtn.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSm
                rightPadding: Theme.spacingSm
            }
        }

        // Loaded plugins button — opens the runtime loaded-plugins
        // drawer with the trust-chip UI from SEC-003 #51. Sits between
        // the Plugin Store and Settings so the "Plugins (catalogue)" →
        // "Loaded (runtime)" → "Settings" reading order matches the
        // user's mental model: browse → manage → configure.
        ToolButton {
            id: loadedBtn
            text: qsTr("Loaded")
            font.pixelSize: Theme.fontMd
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Open the loaded-plugins panel")
            onClicked: root.loadedPluginsRequested()
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Loaded plugins")
            Accessible.description: qsTr("Review currently-loaded plugins and their signature status")
            background: Rectangle {
                radius: Theme.radiusMd
                color: loadedBtn.hovered ? Theme.bgRowHover : "transparent"
                border.width: loadedBtn.activeFocus ? Theme.focusRingWidth : 0
                border.color: Theme.accent
            }
            contentItem: Text {
                text: loadedBtn.text
                color: Theme.fgPrimary
                font: loadedBtn.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSm
                rightPadding: Theme.spacingSm
            }
        }

        // Settings button — opens the Settings drawer with theme / autostart
        // / tray-startup toggles. Sits between the Plugin Store and the
        // minimize button so it shares the same right-side action cluster.
        ToolButton {
            id: settingsBtn
            text: qsTr("Settings")
            font.pixelSize: Theme.fontMd
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Open application settings")
            onClicked: root.settingsRequested()
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Settings")
            Accessible.description: qsTr("Open the settings panel")
            background: Rectangle {
                radius: Theme.radiusMd
                color: settingsBtn.hovered ? Theme.bgRowHover : "transparent"
                border.width: settingsBtn.activeFocus ? Theme.focusRingWidth : 0
                border.color: Theme.accent
            }
            contentItem: Text {
                text: settingsBtn.text
                color: Theme.fgPrimary
                font: settingsBtn.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSm
                rightPadding: Theme.spacingSm
            }
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
