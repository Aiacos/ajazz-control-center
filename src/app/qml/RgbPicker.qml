// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label { text: qsTr("RGB effect"); color: "#ccc" }
        ComboBox {
            Layout.fillWidth: true
            model: [ "Static", "Breathing", "Wave", "Reactive", "Cycle", "Custom" ]
        }

        Label { text: qsTr("Brightness"); color: "#ccc" }
        Slider {
            from: 0; to: 100; value: 80
            Layout.fillWidth: true
        }

        Label { text: qsTr("Color"); color: "#ccc" }
        Rectangle {
            width: 64; height: 64; radius: 6
            color: "#ff5722"
        }

        Item { Layout.fillHeight: true }
    }
}
