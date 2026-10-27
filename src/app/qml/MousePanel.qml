// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse configuration backed by MouseService. Values are pushed over HID by
// ProfileEditor when the user presses Apply.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root
    objectName: "mousePanel"
    property string deviceCodename: ""
    property int dpiStageCount: 0
    property var dpiValues: []
    property int pollingRate: 125
    property real liftOff: 1.5

    function reload() {
        var snapshot = MouseService.current(root.deviceCodename)
        if (snapshot.available) {
            root.dpiValues = snapshot.dpi
            root.pollingRate = snapshot.pollingRate
            root.liftOff = snapshot.liftOff
        } else {
            var defaults = []
            for (var i = 0; i < root.dpiStageCount; ++i) defaults.push(800 + i * 400)
            root.dpiValues = defaults
        }
    }

    function applySettings() {
        return MouseService.apply(root.deviceCodename, root.dpiValues,
                                  root.pollingRate, root.liftOff)
    }

    onDeviceCodenameChanged: reload()
    Component.onCompleted: reload()

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
                Label { text: qsTr("Stage %1").arg(stageRow.index + 1); color: Theme.fgMuted }
                SpinBox {
                    objectName: "dpiStage%1".arg(stageRow.index + 1)
                    from: 50; to: 42000; stepSize: 50
                    value: root.dpiValues.length > stageRow.index ? root.dpiValues[stageRow.index] : 800 + stageRow.index * 400
                    onValueModified: {
                        var next = root.dpiValues.slice()
                        while (next.length <= stageRow.index) next.push(800 + next.length * 400)
                        next[stageRow.index] = value
                        root.dpiValues = next
                    }
                    Accessible.name: qsTr("DPI for stage %1").arg(stageRow.index + 1)
                }
            }
        }
        Label { text: qsTr("Polling rate (Hz)"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
        ComboBox {
            objectName: "pollingRateBox"
            model: [125, 250, 500, 1000, 2000, 4000, 8000]
            currentIndex: Math.max(0, model.indexOf(root.pollingRate))
            onActivated: root.pollingRate = model[currentIndex]
            Accessible.name: qsTr("Polling rate")
        }
        Label { text: qsTr("Lift-off distance (mm)"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
        Slider {
            objectName: "liftOffSlider"
            from: 1.0; to: 2.0; stepSize: 1.0; value: root.liftOff
            onMoved: root.liftOff = value
            Accessible.name: qsTr("Lift-off distance")
        }
        Item { Layout.fillHeight: true }
    }
}
