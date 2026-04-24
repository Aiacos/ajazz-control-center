// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: qsTr("AJAZZ Control Center")

    RowLayout {
        anchors.fill: parent
        spacing: 0

        DeviceList {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            model: deviceModel
            onDeviceSelected: codename => stack.currentCodename = codename
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            property string currentCodename: ""

            // index 0: profile editor (default)
            ProfileEditor {
                codename: stack.currentCodename
            }
        }
    }
}
