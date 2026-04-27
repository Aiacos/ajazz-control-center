// SPDX-License-Identifier: GPL-3.0-or-later
//
// NativePropertyInspector.qml
// ===========================
//
// Schema-driven Property Inspector renderer (closes #39).
//
// Reads a JS object that conforms to docs/schemas/property_inspector.schema.json
// and renders a list of QtQuick.Controls inputs. Whenever the user edits any
// field the component emits `settingsJsonChanged(json)` carrying a JSON string
// ready to be stored in `Action.settingsJson`.
//
// This is the unconditional fallback path: it ships in every build, has no
// external dependencies beyond QtQuick.Controls 6, and is selected by
// `PropertyInspector.qml` whenever the active action has a JSON schema (vs.
// an HTML PI page provided by a plugin). The HTML path lives in
// `PIWebView.qml` and is only compiled into the QML module when Qt
// WebEngine was found at configure time (see `AJAZZ_BUILD_PROPERTY_INSPECTOR`).
//
// Editor materialisation:
//   The earlier revision used a per-row `Loader` whose `item` was typed
//   as `QObject*`. Calls to `item.applyValue()` / `item.committed.connect`
//   tripped `[unqualified]` qmllint warnings because the dynamically-
//   selected sub-component's interface is not visible at static-analysis
//   time. This file now inlines all seven typed editors inside a
//   `StackLayout` whose `currentIndex` is driven by the field type. Each
//   editor is a typed control (TextField, SpinBox, ...) so every member
//   access is statically resolvable. The seven controls are constructed
//   per row but only one is laid out and visible — the cost is minor for
//   a property inspector and the win is zero qmllint warnings without a
//   single skip directive (see TODO.md "Quality bar").

pragma ComponentBehavior: Bound
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
 * Emits `settingsJsonChanged(string json)` after every edit so the caller can
 * persist the change.
 */
ColumnLayout {
    id: root

    /// Schema to render; assign a parsed JS object.
    property var schema: ({ title: "", fields: [] })

    /// Current values keyed by field id.
    property var settings: ({})

    /// Emitted with a JSON string of `settings` after every edit. Named
    /// *settingsJsonChanged* (not just *settingsChanged*) because the
    /// `property var settings` declaration above auto-generates a
    /// `settingsChanged()` signal — sharing the name would cause a
    /// `[duplicated-name]` qmllint warning at build time.
    signal settingsJsonChanged(string json)

    spacing: Theme.spacingSm

    // StackLayout child indices — keep in sync with the editor order
    // declared in the delegate below. The lookup lives at the file scope
    // so the delegate's `currentIndex` binding stays a pure expression.
    readonly property var _typeIndex: ({
        "text":     0,
        "textarea": 1,
        "number":   2,
        "boolean":  3,
        "select":   4,
        "color":    5,
        "section":  6
    })

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
                root.settingsJsonChanged(JSON.stringify(root.settings))
            }

            Text {
                text: row.modelData.label
                color: Theme.fgPrimary
                font.pixelSize: Theme.fontSm
                Layout.fillWidth: true
                visible: row.modelData.type !== "section"
            }

            // Typed editor stack. `currentIndex` selects exactly one child
            // to lay out and show; the others are constructed (so they
            // can run their `Component.onCompleted` initial-value hook
            // without surprises) but receive no layout space and are
            // invisible. All member accesses below are typed (TextField.text,
            // SpinBox.value, …) so qmllint resolves them statically.
            StackLayout {
                Layout.fillWidth: true
                currentIndex: {
                    const t = row.modelData ? row.modelData.type : ""
                    const idx = root._typeIndex[t]
                    return idx === undefined ? 0 : idx
                }

                // 0 — text
                TextField {
                    placeholderText: "value"
                    Component.onCompleted: {
                        const v = row.currentValue()
                        text = v === undefined ? "" : String(v)
                    }
                    onEditingFinished: row.commit(text)
                }

                // 1 — textarea
                TextArea {
                    wrapMode: TextArea.Wrap
                    Component.onCompleted: {
                        const v = row.currentValue()
                        text = v === undefined ? "" : String(v)
                    }
                    onEditingFinished: row.commit(text)
                }

                // 2 — number
                SpinBox {
                    from: -10000
                    to: 10000
                    Component.onCompleted: {
                        const v = Number(row.currentValue())
                        value = isFinite(v) ? v : 0
                    }
                    onValueModified: row.commit(value)
                }

                // 3 — boolean
                CheckBox {
                    Component.onCompleted: checked = !!row.currentValue()
                    onToggled: row.commit(checked)
                }

                // 4 — select
                ComboBox {
                    id: combo
                    textRole: "label"
                    valueRole: "value"
                    model: row.modelData && row.modelData.options ? row.modelData.options : []
                    Component.onCompleted: {
                        const v = row.currentValue()
                        const idx = combo.indexOfValue(v)
                        if (idx !== -1) {
                            combo.currentIndex = idx
                        }
                    }
                    onActivated: row.commit(combo.currentValue)
                }

                // 5 — color (free-form hex for now; a colour picker can replace this later)
                TextField {
                    placeholderText: "#rrggbb"
                    Component.onCompleted: {
                        const v = row.currentValue()
                        text = v === undefined ? "" : String(v)
                    }
                    onEditingFinished: row.commit(text)
                }

                // 6 — section divider (no value, no commit)
                Rectangle {
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
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
}
