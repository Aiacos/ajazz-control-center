// SPDX-License-Identifier: GPL-3.0-or-later
//
// Profile editor panel shown in the main content area after a device is selected.
// Hosts a TabBar (Keys / RGB / Encoders / Mouse) and switches between
// KeyDesigner, RgbPicker, EncoderPanel, and MousePanel via a StackLayout.
// Exposes `codename` so the parent can drive which device is being edited.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    /// Codename of the device currently being edited (e.g. "ak820pro").
    /// An empty string shows a placeholder prompt.
    property string codename: ""
    color: "#14141a"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: root.codename === ""
                ? qsTr("Select a device on the left")
                : qsTr("Editing: %1").arg(root.codename)
            color: "#e0e0e0"
            font.pixelSize: 20
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Keys") }
            TabButton { text: qsTr("RGB") }
            TabButton { text: qsTr("Encoders") }
            TabButton { text: qsTr("Mouse") }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            KeyDesigner  { }
            RgbPicker    { }
            EncoderPanel { }
            MousePanel   { }
        }
    }
}
