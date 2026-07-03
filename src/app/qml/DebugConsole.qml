// SPDX-License-Identifier: GPL-3.0-or-later
//
// DebugConsole.qml -- developer harness for the plugin/device protocol.
//
// Two halves:
//   * Simulate controls (top) -- inject synthetic device input (key / encoder /
//     touch) into the same PluginDeviceBridge path real hardware uses, and
//     inject synthetic plugin->host actions, so the plugin runtime can be
//     exercised and debugged WITHOUT physical hardware or a live plugin.
//   * Live transcript (bottom) -- a timestamped, direction-tagged log of plugin
//     protocol traffic (PluginDebug.lines, newest first).
//
// Backed entirely by the PluginDebug C++ singleton; this is a thin view.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter

Page {
    id: root
    background: Rectangle { color: Theme.surfaceContainer }

    header: ToolBar {
        Material.elevation: 0
        background: Rectangle { color: Theme.bgSidebar }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingMd
            Label {
                text: qsTr("Plugin debug console")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleSmall.pixelSize
                font.weight: Theme.typeTitleSmall.weight
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("device: %1").arg(PluginDebug.activeDeviceId() || qsTr("none"))
                color: Theme.fgMuted
                font.pixelSize: Theme.fontSm
            }
            ToolButton {
                text: qsTr("Clear")
                onClicked: PluginDebug.clear()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // ---- Simulate: device input ---------------------------------------
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Simulate device input")
            Material.accent: Theme.accent

            GridLayout {
                anchors.fill: parent
                columns: 4
                rowSpacing: Theme.spacingSm
                columnSpacing: Theme.spacingSm

                Label { text: qsTr("Key"); color: Theme.fgFaint; Layout.alignment: Qt.AlignVCenter }
                SpinBox {
                    id: keySpin
                    from: 1; to: 32; value: 1
                    Material.accent: Theme.accent
                }
                Button {
                    text: qsTr("Key down")
                    onClicked: PluginDebug.simulateKey(keySpin.value, true)
                }
                Button {
                    text: qsTr("Key up")
                    onClicked: PluginDebug.simulateKey(keySpin.value, false)
                }

                Label { text: qsTr("Encoder"); color: Theme.fgFaint; Layout.alignment: Qt.AlignVCenter }
                SpinBox {
                    id: encSpin
                    from: 0; to: 7; value: 0
                    Material.accent: Theme.accent
                }
                Button {
                    text: qsTr("Turn -1")
                    onClicked: PluginDebug.simulateEncoder(encSpin.value, -1)
                }
                Button {
                    text: qsTr("Turn +1")
                    onClicked: PluginDebug.simulateEncoder(encSpin.value, 1)
                }

                Label { text: qsTr("Touch X"); color: Theme.fgFaint; Layout.alignment: Qt.AlignVCenter }
                SpinBox {
                    id: touchSpin
                    from: 0; to: 255; value: 64
                    Material.accent: Theme.accent
                }
                Button {
                    text: qsTr("Tap")
                    onClicked: {
                        PluginDebug.simulateTouch(touchSpin.value, 0); // down
                        PluginDebug.simulateTouch(touchSpin.value, 2); // up
                    }
                }
                Button {
                    text: qsTr("Encoder press")
                    onClicked: {
                        PluginDebug.simulateEncoderPress(encSpin.value, true);
                        PluginDebug.simulateEncoderPress(encSpin.value, false);
                    }
                }
            }
        }

        // ---- Simulate: plugin -> host action ------------------------------
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Simulate plugin response (plugin -> host)")
            Material.accent: Theme.accent

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Label { text: qsTr("Plugin UUID"); color: Theme.fgFaint; Layout.alignment: Qt.AlignVCenter }
                    TextField {
                        id: uuidField
                        Layout.fillWidth: true
                        text: "com.test.demo"
                        Material.accent: Theme.accent
                    }
                }
                TextArea {
                    id: jsonField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    wrapMode: TextEdit.WrapAnywhere
                    text: '{"event":"setTitle","context":"editor","payload":{"title":"Hi"}}'
                    Material.accent: Theme.accent
                }
                Button {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Send as plugin")
                    highlighted: true
                    Material.accent: Theme.accent
                    onClicked: PluginDebug.simulatePluginAction(uuidField.text, jsonField.text)
                }
            }
        }

        // ---- Live transcript ----------------------------------------------
        Label {
            text: qsTr("Transcript (%1)").arg(PluginDebug.count)
            color: Theme.fgFaint
            font.pixelSize: Theme.fontSm
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgBase
            border.color: Theme.borderSubtle
            border.width: 1
            radius: Theme.radiusSm

            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                clip: true
                model: PluginDebug.lines
                delegate: Text {
                    required property string modelData
                    width: ListView.view ? ListView.view.width : 0
                    text: modelData
                    color: Theme.fgPrimary
                    font.family: "monospace"
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WrapAnywhere
                }

                Label {
                    anchors.centerIn: parent
                    visible: PluginDebug.count === 0
                    text: qsTr("No traffic yet. Simulate input above, or interact with a plugin.")
                    color: Theme.fgFaint
                    font.pixelSize: Theme.fontSm
                }
            }
        }
    }
}
