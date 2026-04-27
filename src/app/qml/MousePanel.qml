// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mouse configuration tab: exposes DPI stage configuration (one stage per
// `dpiStageCount`), USB polling-rate selection, and lift-off distance.
//
// Properties:
//   * `dpiStageCount` — number of DPI stages to expose (default 0).
//
// Backend wiring is pending; the values are local placeholders.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root

    property int dpiStageCount: 0

    EmptyState {
        anchors.centerIn: parent
        visible: root.dpiStageCount === 0
        title: qsTr("No mouse settings")
        body: qsTr("This device does not expose DPI stages or pointer settings.")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        visible: root.dpiStageCount > 0
        spacing: Theme.spacingMd

        Label { text: qsTr("DPI stages"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
        Repeater {
            model: root.dpiStageCount
            delegate: RowLayout {
                id: stageRow
                required property int index
                Label {
                    text: qsTr("Stage %1").arg(stageRow.index + 1)
                    color: Theme.fgMuted
                    font.pixelSize: Theme.fontSm
                }
                SpinBox {
                    from: 100
                    to: 26000
                    stepSize: 100
                    value: 800 + stageRow.index * 400
                    Accessible.role: Accessible.SpinBox
                    Accessible.name: qsTr("DPI for stage %1").arg(stageRow.index + 1)
                }
            }
        }

        Label { text: qsTr("Polling rate (Hz)"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
        ComboBox {
            model: [ 125, 250, 500, 1000, 2000, 4000, 8000 ]
            Accessible.role: Accessible.ComboBox
            Accessible.name: qsTr("Polling rate")
        }

        Label { text: qsTr("Lift-off distance (mm)"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
        Slider {
            from: 1.0
            to: 2.0
            stepSize: 0.1
            value: 1.5
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Lift-off distance")
        }

        Item { Layout.fillHeight: true }
    }
}
