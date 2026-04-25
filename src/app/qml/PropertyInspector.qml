// SPDX-License-Identifier: GPL-3.0-or-later
//
// PropertyInspector.qml
// =====================
//
// Generic, schema-driven Property Inspector renderer.
//
// Reads a JS object that conforms to docs/schemas/property_inspector.schema.json
// and renders a list of QtQuick.Controls inputs. Whenever the user edits
// any field the component emits `settingsChanged(json)` carrying a JSON
// string ready to be stored in `Action.settingsJson`.
//
// Closes #39 (Property Inspector schema + generic renderer).

import QtQuick 6
import QtQuick.Controls 6
import QtQuick.Layouts 6
import AjazzControlCenter

/**
 * Generic schema-driven inspector pane.
 *
 * @property schema    Plain JS object matching property_inspector.schema.json.
 * @property settings  JS object holding the current values; bidirectional.
 *
 * Emits `settingsChanged(string json)` after every edit so the caller can
 * persist the change.
 */
ColumnLayout {
    id: root

    /// Schema to render; assign a parsed JS object.
    property var schema: ({ title: "", fields: [] })

    /// Current values keyed by field id.
    property var settings: ({})

    /// Emitted with a JSON string of `settings` after every edit.
    signal settingsChanged(string json)

    spacing: Theme.spacingSm

    Text {
        text: root.schema.title || ""
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontLg
        font.bold: true
        visible: text.length > 0
        Layout.fillWidth: true
    }

    Text {
        text: root.schema.description || ""
        color: Theme.fgMuted
        font.pixelSize: Theme.fontSm
        wrapMode: Text.Wrap
        visible: text.length > 0
        Layout.fillWidth: true
    }

    // Body — one Repeater entry per declared field.
    Repeater {
        model: root.schema.fields || []

        delegate: ColumnLayout {
            id: row
            required property var modelData
            spacing: 4
            Layout.fillWidth: true

            // Fetch the live value, falling back to the schema default.
            function currentValue() {
                if (root.settings && Object.prototype.hasOwnProperty.call(root.settings, row.modelData.id)) {
                    return root.settings[row.modelData.id]
                }
                return row.modelData.default !== undefined ? row.modelData.default : ""
            }

            // Push a new value back into `settings` and notify listeners.
            function commit(value) {
                root.settings[row.modelData.id] = value
                root.settingsChanged(JSON.stringify(root.settings))
            }

            Text {
                text: row.modelData.label
                color: Theme.fgPrimary
                font.pixelSize: Theme.fontSm
                Layout.fillWidth: true
                visible: row.modelData.type !== "section"
            }

            Loader {
                Layout.fillWidth: true
                sourceComponent: {
                    switch (row.modelData.type) {
                        case "text":      return textField
                        case "textarea":  return textArea
                        case "number":    return spinBox
                        case "boolean":   return checkBox
                        case "select":    return comboBox
                        case "color":     return colorField
                        case "section":   return sectionHeader
                        default:          return textField
                    }
                }

                onLoaded: {
                    // Bridge the loaded item with the current value/commit pair.
                    if (item.applyValue) item.applyValue(row.currentValue())
                    if (item.committed) item.committed.connect(row.commit)
                }
            }

            Text {
                text: row.modelData.help || ""
                color: Theme.fgMuted
                font.pixelSize: Theme.fontXs
                wrapMode: Text.Wrap
                visible: text.length > 0
                Layout.fillWidth: true
            }
        }
    }

    // ---------------------------------------------------------------- //
    // Per-type sub-components. Each declares applyValue() + committed() //
    // so the outer Loader.onLoaded bridge can wire them generically.    //
    // ---------------------------------------------------------------- //
    Component {
        id: textField
        TextField {
            placeholderText: "value"
            signal committed(var value)
            function applyValue(v) { text = v === undefined ? "" : String(v) }
            onEditingFinished: committed(text)
        }
    }

    Component {
        id: textArea
        TextArea {
            wrapMode: TextArea.Wrap
            signal committed(var value)
            function applyValue(v) { text = v === undefined ? "" : String(v) }
            onEditingFinished: committed(text)
        }
    }

    Component {
        id: spinBox
        SpinBox {
            from: -10000
            to: 10000
            signal committed(var value)
            function applyValue(v) { value = Number(v) || 0 }
            onValueModified: committed(value)
        }
    }

    Component {
        id: checkBox
        CheckBox {
            signal committed(var value)
            function applyValue(v) { checked = !!v }
            onToggled: committed(checked)
        }
    }

    Component {
        id: comboBox
        ComboBox {
            id: combo
            textRole: "label"
            valueRole: "value"
            signal committed(var value)
            function applyValue(v) {
                const idx = combo.indexOfValue(v)
                if (idx !== -1) combo.currentIndex = idx
            }
            onActivated: committed(combo.currentValue)
        }
    }

    Component {
        id: colorField
        TextField {
            placeholderText: "#rrggbb"
            signal committed(var value)
            function applyValue(v) { text = v === undefined ? "" : String(v) }
            onEditingFinished: committed(text)
        }
    }

    Component {
        id: sectionHeader
        Rectangle {
            height: 1
            color: Theme.borderSubtle
        }
    }
}
