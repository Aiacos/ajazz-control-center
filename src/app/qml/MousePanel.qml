// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mouse configuration tab: exposes DPI stage configuration (up to six stages
// with individual DPI values), USB polling rate selection, and lift-off
// distance adjustment.
// Exposes no custom signals or properties; backend wiring is pending.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        Label { text: qsTr("DPI stages"); color: Theme.fgFaint }
        Repeater {
            model: 6
            delegate: RowLayout {
                required property int index
                Label { text: qsTr("Stage %1").arg(parent.index + 1); color: Theme.fgFaint }
                SpinBox {
                    from: 100; to: 26000; stepSize: 100
                    value: 800 + parent.index * 400
                }
            }
        }

        Label { text: qsTr("Polling rate (Hz)"); color: Theme.fgFaint }
        ComboBox { model: [ 125, 250, 500, 1000, 2000, 4000, 8000 ] }

        Label { text: qsTr("Lift-off distance (mm)"); color: Theme.fgFaint }
        Slider { from: 1.0; to: 2.0; stepSize: 0.1; value: 1.5 }

        Item { Layout.fillHeight: true }
    }
}
