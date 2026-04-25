// SPDX-License-Identifier: GPL-3.0-or-later
//
// RGB lighting configuration tab.
// Allows selecting an RGB effect preset, adjusting brightness via a Slider,
// and previewing the static colour swatch.
// Exposes no custom signals or properties; communicates with the backend
// through the DeviceController context property (not yet wired).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        Label { text: qsTr("RGB effect"); color: Theme.fgFaint }
        ComboBox {
            Layout.fillWidth: true
            model: [ "Static", "Breathing", "Wave", "Reactive", "Cycle", "Custom" ]
        }

        Label { text: qsTr("Brightness"); color: Theme.fgFaint }
        Slider {
            from: 0; to: 100; value: 80
            Layout.fillWidth: true
        }

        Label { text: qsTr("Color"); color: Theme.fgFaint }
        Rectangle {
            width: 64; height: 64; radius: Theme.radiusMd
            color: Theme.accent
        }

        Item { Layout.fillHeight: true }
    }
}
