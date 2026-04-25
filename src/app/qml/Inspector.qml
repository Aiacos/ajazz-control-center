// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inspector.qml — right-hand pane for configuring the currently selected
// element from KeyDesigner / EncoderPanel (F-07 from ui-review).
//
// Form fields:
//   * Action type (key macro / launch app / OS shortcut / plugin action)
//   * Action params (free-form text — extensible to a property-inspector
//     JSON schema in #39)
//   * Icon (file path placeholder)
//   * Label (user-visible string drawn on the LCD key)
//   * Long-press behaviour (disabled / repeat / alternate action)
//
// Properties:
//   * `selectionLabel` — string shown in the header (e.g. "Key 3").
//   * `hasSelection`   — when false, the Inspector renders an EmptyState
//     instead of the form fields.
//
// Emits:
//   * `restoreDefaultsRequested()`  — user clicked "Restore defaults".
//   * `applyRequested(QVariantMap)` — user clicked "Apply"; map carries the
//     current form values so ProfileController can persist them.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Rectangle {
    id: root

    property string selectionLabel: ""
    property bool   hasSelection: selectionLabel.length > 0

    signal restoreDefaultsRequested()
    signal applyRequested(var values)

    color: Theme.bgSidebar

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Inspector")
            subtitle: root.hasSelection ? qsTr("Editing: %1").arg(root.selectionLabel)
                                          : qsTr("No selection")
        }

        // Empty-state path ----------------------------------------------------
        EmptyState {
            visible: !root.hasSelection
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Nothing selected")
            body: qsTr("Click a key or encoder to configure its action, label, icon, and long-press behaviour.")
        }

        // Form path -----------------------------------------------------------
        ColumnLayout {
            visible: root.hasSelection
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Label { text: qsTr("Action type"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            ComboBox {
                id: actionType
                Layout.fillWidth: true
                model: [ qsTr("None"), qsTr("Key macro"), qsTr("Launch application"),
                         qsTr("OS shortcut"), qsTr("Plugin action") ]
                Accessible.role: Accessible.ComboBox
                Accessible.name: qsTr("Action type")
            }

            Label { text: qsTr("Action params"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            TextField {
                id: actionParams
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. Ctrl+Alt+P or /usr/bin/firefox")
                color: Theme.fgPrimary
                placeholderTextColor: Theme.fgMuted
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Action parameters")
            }

            Label { text: qsTr("Label"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            TextField {
                id: keyLabel
                Layout.fillWidth: true
                placeholderText: qsTr("Visible label")
                color: Theme.fgPrimary
                placeholderTextColor: Theme.fgMuted
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Label drawn on the key")
            }

            Label { text: qsTr("Icon path"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            TextField {
                id: iconPath
                Layout.fillWidth: true
                placeholderText: qsTr("Path or qrc:/ uri")
                color: Theme.fgPrimary
                placeholderTextColor: Theme.fgMuted
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Icon path")
            }

            Label { text: qsTr("Long-press behaviour"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            ComboBox {
                id: longPress
                Layout.fillWidth: true
                model: [ qsTr("Disabled"), qsTr("Repeat"), qsTr("Alternate action") ]
                Accessible.role: Accessible.ComboBox
                Accessible.name: qsTr("Long-press behaviour")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                SecondaryButton {
                    text: qsTr("Restore defaults")
                    onClicked: root.restoreDefaultsRequested()
                }
                Item { Layout.fillWidth: true }
            }
        }

        Item { Layout.fillHeight: true }
    }

    /// Collect the current form values into a QVariantMap.
    function snapshot() {
        return {
            "actionType": actionType.currentText,
            "actionParams": actionParams.text,
            "label": keyLabel.text,
            "iconPath": iconPath.text,
            "longPress": longPress.currentText
        };
    }
}
