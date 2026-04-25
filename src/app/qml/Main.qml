// SPDX-License-Identifier: GPL-3.0-or-later
//
// Main application window for AJAZZ Control Center.
//
// Layout (top to bottom):
//   * AppHeader    — product mark, search, minimize-to-tray.
//   * RowLayout
//     * DeviceList  — sidebar with the connected devices.
//     * ProfileEditor — middle pane with Keys/RGB/Encoders/Mouse tabs.
//     * Inspector   — right pane with the form fields for the selected element.
//
// Responsive behaviour (F-17):
//   * Below 700 px the sidebar collapses to a fixed 64 px icons-only column.
//   * Below 1100 px the Inspector pane is hidden entirely.
//
// Toast notifications surface profile save / load events from
// ProfileController.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
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

    // Surface profile-controller IO results as toasts.
    Connections {
        target: profileController
        function onProfileSaved(path) {
            toast.show(qsTr("Profile saved"), "success");
        }
        function onSaveFailed(reason) {
            toast.show(qsTr("Save failed: %1").arg(reason), "error");
        }
        function onLoadFailed(reason) {
            toast.show(qsTr("Load failed: %1").arg(reason), "error");
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            Layout.fillWidth: true
            onMinimizeRequested: root.hide()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            DeviceList {
                id: sidebar
                Layout.preferredWidth: root.width < 700 ? 64 : 320
                Layout.fillHeight: true
                model: deviceModel
                onDeviceSelected: codename => {
                    editor.codename = codename;
                    editor.capabilities = deviceModel.capabilitiesFor(codename);
                }
            }

            ProfileEditor {
                id: editor
                Layout.fillWidth: true
                Layout.fillHeight: true
                onApplyRequested: profileController.save()
                onRevertRequested: profileController.load()
                onRestoreDefaultsRequested: toast.show(qsTr("Restore defaults: not implemented yet"), "info")
            }

            Inspector {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                visible: root.width >= 1100
                selectionLabel: editor.codename === ""
                    ? ""
                    : qsTr("Key %1").arg((editor.children.length > 0 ? 1 : 1))
            }
        }
    }

    Toast { id: toast }
}
