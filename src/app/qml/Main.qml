// SPDX-License-Identifier: GPL-3.0-or-later
//
// Main application window for AJAZZ Control Center.
// Hosts a DeviceList sidebar on the left and a StackLayout content area
// on the right that switches to ProfileEditor when a device is selected.
// Emits no signals itself; child DeviceList emits deviceSelected(codename).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    // Visible by default — main.cpp may hide() the window when
    // tray.startMinimized && tray.trayAvailable so the app launches into the
    // system tray. Keep it true so the window can be reopened from the tray.
    visible: true
    title: branding ? branding.productName : qsTr("AJAZZ Control Center")
    color: Theme.bgBase

    // Surface the tray's "Show window" action.
    Connections {
        target: tray
        function onShowWindowRequested() {
            root.show();
            root.raise();
            root.requestActivate();
        }
    }

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
