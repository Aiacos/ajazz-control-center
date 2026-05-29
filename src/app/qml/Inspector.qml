// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inspector.qml — right-hand pane for configuring the currently selected
// element from KeyDesigner / EncoderPanel (F-07 from ui-review).
//
// As of quick task 260514-1je (Stream Dock KeyDesigner slice), the Inspector
// is **live-bound** to a `binding` JS object that represents the currently
// selected key's binding row in the parent's KeyBindingModel. Every form
// field reads from `binding.<field>` and emits `bindingFieldChanged(field,
// value)` on edit; the parent (KeyDesigner) catches the signal and calls
// `bindings.setProperty(selectedIndex, field, value)` so the cell preview
// updates immediately. There is no Apply button — edits propagate live, in
// line with modern Stream-Deck-style editor UX.
//
// Form fields surfaced today (single-state slice):
//   * Action type — Plugin / KeyPress / RunCommand / OpenUrl / OpenFolder
//                   (matches ActionKind in src/core/include/ajazz/core/profile.hpp).
//   * Action params (free-form text — multi-action chain editor deferred).
//   * Label — overlay text drawn on top of the key icon.
//   * Icon — file picker (filesystem only; bundled icon library deferred).
//
// Properties:
//   * `selectionLabel` — string shown in the header (e.g. "Key 3").
//   * `hasSelection`   — when false, the Inspector renders an EmptyState
//     instead of the form fields.
//   * `binding`        — JS object with keys actionKind, actionParams, label,
//     iconSource, AND the optional PI fields: propertyInspectorPath, pluginUuid,
//     actionUuid, contextUuid. When null/undefined the form goes blank and the
//     HTML PI is closed.
//
// Emits:
//   * `bindingFieldChanged(string field, var value)` — fired on every form
//     edit. The parent persists the change into its own binding store.
//
// Property Inspector wiring (Plan 20-03 / PLUGIN-09):
//   When `binding.propertyInspectorPath` is a non-empty string AND
//   PropertyInspectorController.webEngineAvailable is true, Inspector calls
//   PropertyInspectorController.loadInspector(pluginUuid, propertyInspectorPath,
//   actionUuid, contextUuid) so the HTML PI loads in PIWebView. When the
//   selected action has no PI path (built-in/native) or WebEngine is absent,
//   closeInspector() is called so PropertyInspector.qml's Loader falls back
//   to NativePropertyInspector. Selecting nothing (binding == null) also
//   closes the inspector. The singleton is accessed by type name only, never
//   instantiated.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Rectangle {
    id: root

    property string selectionLabel: ""
    property bool   hasSelection: selectionLabel.length > 0
    property var    binding: null

    signal bindingFieldChanged(string field, var value)

    // -- Property Inspector wiring (Workstream C) ----------------------------
    // Called whenever `binding` changes. When the selected binding is a plugin
    // action (actionKind == 0, non-empty actionId) whose installed manifest
    // declares a Property Inspector HTML page, load it; otherwise close any
    // HTML PI so the native form takes over. The PI path + plugin uuid are
    // resolved live from PluginCatalog.actionInfo(actionId) rather than stored
    // in the profile (the Profile schema has no PI field).
    function maybeLoadInspector() {
        if (!root.binding || !PropertyInspectorController.webEngineAvailable
                || typeof PluginCatalog === "undefined" || !PluginCatalog) {
            PropertyInspectorController.closeInspector();
            return;
        }

        var kind = root.binding.actionKind;
        var actionId = root.binding.actionId ? root.binding.actionId : "";
        if (kind !== 0 /* ActionKind.Plugin */ || actionId === "") {
            PropertyInspectorController.closeInspector();
            return;
        }

        var info = PluginCatalog.actionInfo(actionId);
        var piAbs = (info && info.propertyInspectorAbsPath) ? info.propertyInspectorAbsPath : "";
        if (piAbs === "") {
            // Plugin action without an HTML PI -> native form.
            PropertyInspectorController.closeInspector();
            return;
        }

        var pluginUuid = (info && info.pluginUuid) ? info.pluginUuid : "";
        PropertyInspectorController.loadInspector(pluginUuid, piAbs, actionId, root._contextUuid());
    }

    // Stable per-profile, per-selection settings context so the PIBridge
    // persists settings to a deterministic file across sessions. Distinct
    // profiles/keys get distinct contexts (so settings do not bleed across).
    function _contextUuid() {
        var pid = (typeof ProfileController !== "undefined" && ProfileController)
            ? ProfileController.activeProfileId() : "";
        var sel = root.selectionLabel.replace(/[^A-Za-z0-9_-]/g, "_");
        return (pid === "" ? "default" : pid) + "_" + (sel === "" ? "sel" : sel);
    }

    // React to binding changes (new key selected, selection cleared, binding updated).
    onBindingChanged: maybeLoadInspector()

    color: Theme.bgSidebar

    // Filesystem icon picker. Filesystem-only is the Stream Dock slice
    // decision (D1 in 260514-1je-FINDINGS.md §5); a bundled qrc icon
    // library can land in a follow-up.
    FileDialog {
        id: iconPicker
        title: qsTr("Choose key icon")
        nameFilters: [
            qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"),
            qsTr("All files (*)")
        ]
        // FileDialog.selectedFile is a `url` ("file:///path/to/icon.png"); the
        // ListModel role `iconSource` is first seeded as String at
        // KeyDesigner.qml._ensureBindings(), so writing the url directly hits
        // Qt's "Can't assign to existing role 'iconSource' of different type
        // [Url -> String]" and silently no-ops (Phase 25 VERIFY-05 Test 1 FAIL,
        // 2026-05-28 walkthrough). Converting to a String here keeps the role
        // typing consistent.
        //
        // We keep the `file://` URL form (not a bare /home/... path) so that
        // QML Image consumers (KeyCell, Inspector preview, both `url` typed)
        // recognise it as an absolute URL — bare paths get resolved relative
        // to the QML context (qrc:/), which silently breaks the preview with
        // "QML QQuickImage: Cannot open: qrc:/home/...". The C++ load site at
        // StreamDockControlService strips the `file://` prefix before
        // QImage(QString).
        onAccepted: root.bindingFieldChanged(
            "iconSource", iconPicker.selectedFile.toString())
    }

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
            body: qsTr("Click a key on the left to configure its action, label, and icon.")
        }

        // HTML Property Inspector path (Workstream C) -------------------------
        // When a bound plugin action declares a Property Inspector page, the
        // controller reports hasHtmlInspector and we render the plugin's HTML
        // PI here (loaded lazily so non-WebEngine builds never touch the file).
        // The native form below is hidden while the HTML PI is active.
        Loader {
            id: htmlPiLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.hasSelection
                     && PropertyInspectorController.webEngineAvailable
                     && PropertyInspectorController.hasHtmlInspector
            active: visible
            // String source (not a hard import) so the missing PIWebView.qml in
            // a no-WebEngine build does not become a compile error.
            source: "PIWebView.qml"
            onStatusChanged: {
                if (status === Loader.Error) {
                    console.error("Inspector: failed to load PIWebView.qml");
                }
            }
        }

        // Form path -----------------------------------------------------------
        ColumnLayout {
            visible: root.hasSelection && !(PropertyInspectorController.webEngineAvailable
                                            && PropertyInspectorController.hasHtmlInspector)
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            // -- Icon row -----------------------------------------------------
            Label { text: qsTr("Icon"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                // Preview swatch — mirrors the LCD-key visual at 1:1 size.
                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    radius: Theme.radiusMd
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        source: root.binding && root.binding.iconSource ? root.binding.iconSource : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        visible: source.toString() !== ""
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    SecondaryButton {
                        Layout.fillWidth: true
                        text: qsTr("Choose file…")
                        onClicked: iconPicker.open()
                    }
                    SecondaryButton {
                        Layout.fillWidth: true
                        text: qsTr("Clear")
                        enabled: root.binding && root.binding.iconSource && root.binding.iconSource.toString() !== ""
                        onClicked: root.bindingFieldChanged("iconSource", "")
                    }
                }
            }

            // -- Label --------------------------------------------------------
            Label { text: qsTr("Label"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            TextField {
                id: keyLabel
                Layout.fillWidth: true
                placeholderText: qsTr("Visible label")
                color: Theme.fgPrimary
                placeholderTextColor: Theme.fgMuted
                // One-way bind: read from the model, never assign upward
                // except via the signal. The `text:` line re-fires when
                // `binding` changes, which is what gives us the "selecting
                // a different cell repopulates the form" behaviour.
                text: root.binding && root.binding.label ? root.binding.label : ""
                onTextEdited: root.bindingFieldChanged("label", text)
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Label drawn on the key")
            }

            // -- Action type --------------------------------------------------
            Label { text: qsTr("Action type"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            ComboBox {
                id: actionType
                Layout.fillWidth: true
                // Values match ActionKind enum positions in profile.hpp:
                //   0 Plugin · 1 Sleep · 2 KeyPress · 3 RunCommand
                //   4 OpenUrl · 5 OpenFolder · 6 BackToParent
                // We expose only the user-meaningful subset for the slice.
                model: [
                    qsTr("Plugin action"),     // 0
                    qsTr("Key macro"),         // 2 (we map Sleep→1 below)
                    qsTr("Launch command"),    // 3
                    qsTr("Open URL"),          // 4
                    qsTr("Open folder")        // 5
                ]
                readonly property var _kindByIndex: [0, 2, 3, 4, 5]
                readonly property var _indexByKind: ({0: 0, 2: 1, 3: 2, 4: 3, 5: 4})
                currentIndex: root.binding && root.binding.actionKind !== undefined
                    ? (actionType._indexByKind[root.binding.actionKind] || 0)
                    : 0
                onActivated: root.bindingFieldChanged("actionKind", _kindByIndex[currentIndex])
                Accessible.role: Accessible.ComboBox
                Accessible.name: qsTr("Action type")
            }

            // -- Action params ------------------------------------------------
            Label { text: qsTr("Action params"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            TextField {
                id: actionParams
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. Ctrl+Alt+P or /usr/bin/firefox")
                color: Theme.fgPrimary
                placeholderTextColor: Theme.fgMuted
                text: root.binding && root.binding.actionParams ? root.binding.actionParams : ""
                onTextEdited: root.bindingFieldChanged("actionParams", text)
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Action parameters")
            }
        }

        Item { Layout.fillHeight: true }
    }
}
