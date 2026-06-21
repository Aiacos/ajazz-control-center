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
    objectName: "inspector" // debug-channel addressing (Property Inspector dock)

    property string selectionLabel: ""
    property bool   hasSelection: selectionLabel.length > 0
    property var    binding: null

    // Wire context inputs — fed by DeviceView so the PI settings context
    // matches the wire context id (device#page#controller#row#column).
    // See PLUGIN-22 / 29-02-PLAN.md: Inspector.qml does NOT know the codename
    // or numeric index on its own; DeviceView binds them.
    property string deviceCodename: ""
    property int    keyIndex: -1
    // Dial selection: when >= 0 (and keyIndex < 0) the PI targets an Encoder
    // context instead of a Keypad context, so dial plugin actions configure.
    property int    encoderIndex: -1

    signal bindingFieldChanged(string field, var value)

    // ----- Toggle Action states (Delta C) -----------------------------------
    // A key is a multi-state Toggle when it carries >= 2 states; the input
    // service cycles currentState on press and the device repaints
    // states[currentState]. The editor reads/writes the states straight through
    // ProfileController (which owns the active page), so no nested ListModel role
    // is needed. _toggleStates is a JS array of {title, image}.
    property var _toggleStates: []
    function _refreshToggleStates() {
        root._toggleStates = (root.keyIndex >= 0 && typeof ProfileController !== "undefined")
            ? ProfileController.toggleStatesForKey(root.keyIndex) : [];
    }
    function _commitToggleStates() {
        if (root.keyIndex >= 0)
            ProfileController.commitToggleStates(root.keyIndex, root._toggleStates);
    }
    function _setToggleTitle(i, title) {
        var arr = root._toggleStates.slice();
        arr[i] = { title: title, image: arr[i].image ? arr[i].image : "" };
        root._toggleStates = arr;
        root._commitToggleStates();
    }
    function _addToggleState() {
        var arr = root._toggleStates.slice();
        // Seed the first conversion with two states (a toggle needs >= 2); after
        // that each click appends one more.
        if (arr.length === 0) {
            arr.push({ title: qsTr("State 1"), image: "" });
            arr.push({ title: qsTr("State 2"), image: "" });
        } else {
            arr.push({ title: qsTr("State %1").arg(arr.length + 1), image: "" });
        }
        root._toggleStates = arr;
        root._commitToggleStates();
    }
    function _removeToggleState(i) {
        var arr = root._toggleStates.slice();
        arr.splice(i, 1);
        root._toggleStates = arr; // commit; < 2 states reverts the key to single-state
        root._commitToggleStates();
        root._refreshToggleStates(); // backend may have dropped the instance
    }
    Connections {
        target: typeof ProfileController !== "undefined" ? ProfileController : null
        function onProfileChanged() { root._refreshToggleStates(); }
    }

    // Clip so the form can never paint outside the docked pane and bleed
    // onto the brightness row / footer below it (the fields are taller than
    // the pane; the ScrollView below makes them scrollable instead).
    clip: true

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
        var ctx = root._contextUuid();
        if (ctx === "") {
            // No valid key selection — cannot build a wire context id yet.
            PropertyInspectorController.closeInspector();
            return;
        }
        PropertyInspectorController.loadInspector(pluginUuid, piAbs, actionId, ctx);
    }

    // Stable wire context id that the PIBridge and the plugin WebSocket path both
    // key their per-context settings records on (PLUGIN-22 / 29-02-PLAN.md).
    // Format: device#page#controller#row#column — matches ContextRegistry::deriveContextId
    // byte-for-byte.  AKP05E grid is 5-wide: col = keyIndex % 5, row = floor(keyIndex / 5).
    // Returns "" when keyIndex < 0 (no key selected) so loadInspector is not called
    // with a bogus context.
    function _contextUuid() {
        if (root.deviceCodename === "") {
            return "";
        }
        // Keypad context: device#root#Keypad#row#col (AKP05E grid is 5-wide).
        if (root.keyIndex >= 0) {
            var col = root.keyIndex % 5;
            var row = Math.floor(root.keyIndex / 5);
            return root.deviceCodename + "#root#Keypad#" + row + "#" + col;
        }
        // Encoder (dial) context: device#root#Encoder#0#column — matches the
        // bridge's encoder registration (row=0, column=encoderIndex) in
        // populateContextsForActivePage byte-for-byte so the PI and the plugin
        // WebSocket path key the SAME per-context settings record.
        if (root.encoderIndex >= 0) {
            return root.deviceCodename + "#root#Encoder#0#" + root.encoderIndex;
        }
        return "";
    }

    // React to binding changes (new key selected, selection cleared, binding updated).
    onBindingChanged: maybeLoadInspector()
    // Also reload when the selected control's index settles, so the wire context
    // id (_contextUuid) is rebuilt from the final keyIndex/encoderIndex rather
    // than a stale value when switching key<->dial. The last call wins.
    onKeyIndexChanged: { maybeLoadInspector(); _refreshToggleStates(); }
    onEncoderIndexChanged: maybeLoadInspector()

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

        // Explicit Property-Inspector open/close affordances (PI-02) ----------
        // The HTML PI already auto-opens on selection via maybeLoadInspector();
        // these are the discrete, debug-addressable controls the headless
        // success criteria require. Both are plain Buttons (SecondaryButton) so
        // scripts/ajazz-debug `qml.invoke` fires their onClicked — a Switch's
        // toggled() side-effect cannot be reproduced by the harness (CLAUDE.md).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            visible: root.hasSelection

            SecondaryButton {
                objectName: "openPiButton"
                Layout.fillWidth: true
                text: qsTr("Open inspector")
                // Resolve the PI path + plugin uuid + wire context for the
                // current selection and call loadInspector (reuses the same
                // path the selection auto-trigger uses).
                onClicked: root.maybeLoadInspector()
            }

            SecondaryButton {
                objectName: "closePiButton"
                Layout.fillWidth: true
                text: qsTr("Close inspector")
                enabled: PropertyInspectorController.hasHtmlInspector
                onClicked: PropertyInspectorController.closeInspector()
            }
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
            // Debug-channel addressability (PI-02): qml.get confirms
            // active/visible to verify the PI panel was shown.
            objectName: "piPanelLoader"
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
        // Wrapped in a ScrollView so the form (which is taller than the docked
        // pane) scrolls within the Inspector instead of overflowing onto the
        // brightness row / footer below it.
        ScrollView {
            id: formScroll
            visible: root.hasSelection && !(PropertyInspectorController.webEngineAvailable
                                            && PropertyInspectorController.hasHtmlInspector)
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
            width: formScroll.availableWidth
            spacing: Theme.spacingMd

            // -- Dominant key preview (US2 / FR-009) --------------------------
            // Elgato-faithful: the action visual is the dominant element at the
            // top of the config form, rendered exactly like the on-canvas key
            // (image + title overlaid at the bottom, live via image://livekey).
            // A bound action with no image/title falls back to a sensible
            // placeholder (glyph + action name) rather than an empty/broken tile
            // (spec edge case). Centred so it reads as the "what this key looks
            // like" hero, mirroring the Stream Deck software's selected-key card.
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.spacingXs
                Layout.preferredWidth: 140
                Layout.preferredHeight: 140

                Rectangle {
                    id: keyPreview
                    objectName: "keyPreview"
                    anchors.fill: parent
                    radius: Theme.radiusLg
                    // Lit-LCD black behind an icon (hardware-faithful), like KeyCell.
                    readonly property bool hasIcon: root.binding && root.binding.iconSource
                        && root.binding.iconSource.toString() !== ""
                    readonly property string previewLabel:
                        root.binding && root.binding.label ? root.binding.label : ""
                    color: keyPreview.hasIcon ? "#000000" : Theme.tile
                    border.color: Theme.accent
                    border.width: 1

                    Image {
                        anchors.fill: parent
                        anchors.margins: 6
                        source: keyPreview.hasIcon ? root.binding.iconSource : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        asynchronous: true
                        visible: keyPreview.hasIcon
                    }

                    // Title overlay — bottom-aligned over the icon, exactly like
                    // KeyCell. Suppressed for live device renders (image://livekey
                    // bakes the title into the frame, avoiding double text).
                    Text {
                        anchors.fill: parent
                        anchors.margins: 6
                        visible: keyPreview.previewLabel !== ""
                            && (!keyPreview.hasIcon
                                || root.binding.iconSource.toString().indexOf("image://livekey") !== 0)
                        text: keyPreview.previewLabel
                        color: Theme.fgPrimary
                        font.pixelSize: Theme.fontMd
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: keyPreview.hasIcon ? Text.AlignBottom : Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        style: keyPreview.hasIcon ? Text.Outline : Text.Normal
                        styleColor: "#000000"
                    }

                    // Sensible placeholder: a bound action with neither image nor
                    // title shows a glyph + the action name so the tile never reads
                    // as empty or broken (spec edge case).
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.spacingXs
                        visible: !keyPreview.hasIcon && keyPreview.previewLabel === ""
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: "▢" // ▢ white square with rounded corners
                            color: Theme.fgMuted
                            font.pixelSize: 36
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: 124
                            horizontalAlignment: Text.AlignHCenter
                            text: root.hasSelection ? root.selectionLabel : ""
                            color: Theme.fgMuted
                            font.pixelSize: Theme.fontSm
                            elide: Text.ElideRight
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                        }
                    }
                }
            }

            // -- Icon row -----------------------------------------------------
            // The dominant preview above is the 1:1 visual; this row is just the
            // Choose/Clear controls (the redundant small swatch was removed).
            Label { text: qsTr("Icon"); color: Theme.fgMuted; font.pixelSize: Theme.fontSm }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

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

            // -- Toggle states (multi-state key, Delta C) ---------------------
            // Keys only (a ProfilePage carries keys; dials stay single-state).
            // Each state is one title the device shows for that step; pressing
            // the key cycles them. The renderer composites title (and image, if
            // set) over the base, live on the device + editor canvas.
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSm
                height: 1
                color: Theme.borderSubtle
                visible: root.keyIndex >= 0
            }
            Label {
                objectName: "toggleStatesSection"
                text: qsTr("Toggle states (multi-state key)")
                color: Theme.fgMuted
                font.pixelSize: Theme.fontSm
                visible: root.keyIndex >= 0
            }
            Label {
                text: qsTr("A single tap cycles these states on the key.")
                color: Theme.fgFaint
                font.pixelSize: Theme.typeLabelSmall.pixelSize
                visible: root.keyIndex >= 0 && root._toggleStates.length === 0
            }
            Repeater {
                model: root._toggleStates
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    visible: root.keyIndex >= 0
                    Label {
                        text: (index + 1) + "."
                        color: Theme.fgFaint
                        Layout.alignment: Qt.AlignVCenter
                    }
                    TextField {
                        objectName: "toggleStateTitle_" + index
                        Layout.fillWidth: true
                        text: modelData.title ? modelData.title : ""
                        placeholderText: qsTr("State title")
                        color: Theme.fgPrimary
                        placeholderTextColor: Theme.fgMuted
                        onEditingFinished: root._setToggleTitle(index, text)
                    }
                    Button {
                        objectName: "removeToggleState_" + index
                        text: "✕"
                        flat: true
                        Layout.preferredWidth: 32
                        onClicked: root._removeToggleState(index)
                    }
                }
            }
            Button {
                objectName: "addToggleStateButton"
                text: root._toggleStates.length === 0
                      ? qsTr("Make this a multi-state key")
                      : qsTr("Add state")
                visible: root.keyIndex >= 0
                Layout.fillWidth: true
                onClicked: root._addToggleState()
            }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
