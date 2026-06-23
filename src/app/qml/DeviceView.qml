// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceView.qml -- geometry-driven three-stacked-rows device editor.
//
// Replaces KeyDesigner.qml for all LCD-key SKUs (REQ-26-B, Phase 26).
//
// Layout: three-column RowLayout:
//   1. ActionLibraryPane (240px fixed, left)
//   2. Device chassis area (fills remaining width, center)
//   3. Inspector.qml (280px fixed, right)
//
// Chassis area renders three stacked rows:
//   Row 1..keyRows:  keyRows x keyColumns LCD-key cells (KeyCell.qml)
//   Row keyRows+1:   (when encoderCount > 0) Repeater over encoderCount -> EncoderDial.qml
//   Row keyRows+2:   (when touchZoneCount > 0) TouchStripLane.qml
//
// Photo background strategy (D-04, D-07):
//   loadLayout(codename) fetches qrc:/qt/qml/AjazzControlCenter/device-layouts/<codename>.json
//   via XMLHttpRequest. On success: render per-SKU photo + viewBox-positioned cells.
//   On failure / missing JSON: render outline-frame fallback (D-07) from DeviceDescriptor
//   geometry (keyRows, keyColumns, encoderCount, touchZoneCount).
//
// NOTE: Phase 26 v1 ships with the outline-frame fallback wired unconditionally
//       because per-SKU layout JSONs land in Plan 26-06. The photo path is
//       fully implemented; it engages once 26-06 populates device-layouts/.
//
// Trash zone (D-09, UI-SPEC):
//   RoundButton top-right of chassis; 30% opacity idle; 100% during any cell drag.
//   On drop: calls ProfileController.commitKeyBinding/commitTouchZoneBinding with
//   empty params to clear the binding.
//   // Phase 26 v1: ProfileController.clearBinding() does not exist yet; trash
//   // drops route through commitXxxBinding with empty params (effectively clears
//   // the visual; a dedicated clearBinding Q_INVOKABLE lands in a follow-up plan).
//
// Keyboard navigation:
//   v1: basic Tab/Shift-Tab chain through ActionLibraryPane -> chassis -> Inspector.
//   Full grid arrow-key navigation is a follow-up (arrow keys within chassis deferred).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root
    objectName: "deviceView" // debug-channel addressing (qml.get selectedKeyIndex etc.)

    // ---- Property contract (extends KeyDesigner.qml contract) ---------------
    property int    keyCount:    0
    property int    gridColumns: 0   // falls back from keyCount math when 0
    property int    keyRows:     0   // from DeviceDescriptor; 0 => infer from keyCount/gridColumns
    property int    encoderCount:    0
    property int    touchZoneCount:  0
    property string codename:    ""

    property int    selectedKeyIndex:     -1
    property int    selectedEncoderIndex: -1
    property int    selectedZoneIndex:    -1

    signal keySelected(int idx)
    signal keyActivated(int idx)
    signal encoderSelected(int idx)
    signal zoneSelected(int idx)

    // ---- Resolved grid dimensions ------------------------------------------
    readonly property int _keyColumnsResolved: gridColumns > 0
        ? gridColumns
        : Math.max(1, Math.ceil(keyCount / Math.max(1, keyRowsResolved)))
    readonly property int keyRowsResolved: keyRows > 0
        ? keyRows
        : Math.max(1, Math.ceil(keyCount / Math.max(1, gridColumns > 0 ? gridColumns : 5)))

    // ---- Overflow predicate (EDIT-01, 32-UI-SPEC) ---------------------------
    // The grid is "overflowing" (vertical scroll becomes reachable) when the
    // device geometry exceeds OpenDeck's overflow rule: more than 8 key columns
    // OR more than 4 rows, where the row count includes the encoder lane row and
    // the touch-zone lane row (not just key rows). This is the spec-level
    // acceptance predicate; whether the scrollbar actually paints is ultimately
    // driven by content-height-vs-viewport (ScrollBar.AsNeeded).
    readonly property bool _gridOverflows:
        (_keyColumnsResolved > 8)
        || ((keyRowsResolved
             + (encoderCount > 0 ? 1 : 0)
             + (touchZoneCount > 0 ? 1 : 0)) > 4)

    // ---- Observability accessors for tests (Phase 26 Plan 26-05, REQ-26-B) -
    // Pure read-only mirrors of the Repeater counts exposed for offscreen QML
    // tests (test_device_view_geometry.qml).  No behaviour change at runtime.
    readonly property int keyCellsRendered:   keyCount          // equals bindings.count after _ensureBindings
    readonly property int encoderDialsRendered: encoderCount    // drives encoder Repeater model directly
    readonly property int touchZonesRendered: touchZoneCount    // drives TouchStripLane.touchZoneCount

    // ---- Per-key binding model (mirrors KeyDesigner.qml) -------------------
    ListModel { id: bindings }

    // ---- Per-encoder binding model (US3: dial owns its touch-strip segment) -
    // One row per dial {iconSource,label,actionKind,actionId}. Drives BOTH the
    // dial knob and the touch-strip segment above it (segment N mirrors dial N),
    // so the segment renders the dial's bound action — the Elgato Stream Deck +
    // model where the dial and its segment are one control. Populated from
    // ProfileController.activeEncoderBindings() in _syncFromProfile().
    ListModel { id: encoderBindings }

    // Delta A: cached folder breadcrumb (root -> ... -> active page) as a list of
    // {id, name}. Refreshed on profileChanged (which enterFolder/goBack emit) so
    // the breadcrumb bar and folder-depth visibility stay current.
    property var _breadcrumb: []
    function _refreshBreadcrumb() {
        root._breadcrumb = ProfileController.pageBreadcrumb();
    }

    // Phase 29 (OpenDeck parity / update_state): when the device renders a key
    // (a plugin's setImage/setTitle, a built-in icon, or a profile repaint), the
    // control service emits keyImageAssigned so the on-screen cell mirrors the
    // SAME live frame the device shows. Point the cell's iconSource at the
    // "livekey" image provider; the ?r=<revision> query busts QML's image cache.
    Connections {
        target: StreamDockControlService
        function onKeyImageAssigned(keyIndex, revision) {
            if (keyIndex >= 0 && keyIndex < bindings.count) {
                bindings.setProperty(keyIndex, "iconSource",
                                     "image://livekey/" + keyIndex + "?r=" + revision);
            }
        }
        // OpenDeck parity: when a key is cleared (action moved/removed away), drop
        // the live-render URL so the cell reverts to the empty-tile look instead of
        // keeping the stale render of the action that used to live there.
        function onKeyImageCleared(keyIndex) {
            if (keyIndex >= 0 && keyIndex < bindings.count) {
                bindings.setProperty(keyIndex, "iconSource", "");
            }
        }
        // Delta B (Elgato dial parity): when the device renders a dial feedback
        // layout (a plugin's setFeedback/setFeedbackLayout, or an encoder setState),
        // the control service emits encoderImageAssigned so the on-screen EncoderDial
        // AND its touch-strip segment (both fed by encoderBindings) mirror the SAME
        // live frame the device LCD shows.
        function onEncoderImageAssigned(encoderIndex, revision) {
            if (encoderIndex >= 0 && encoderIndex < encoderBindings.count) {
                encoderBindings.setProperty(encoderIndex, "iconSource",
                                            "image://liveencoder/" + encoderIndex + "?r=" + revision);
            }
        }
    }

    function _ensureBindings() {
        while (bindings.count < root.keyCount) {
            bindings.append({ iconSource: "", label: "", actionKind: 0, actionParams: "",
                              actionId: "", isFolder: false, folderTarget: "" });
        }
        while (bindings.count > root.keyCount) {
            bindings.remove(bindings.count - 1);
        }
        if (root.selectedKeyIndex >= bindings.count) {
            root.selectedKeyIndex = -1;
        }
    }

    function _ensureEncoderBindings() {
        while (encoderBindings.count < root.encoderCount) {
            encoderBindings.append({ iconSource: "", label: "", actionKind: 0, actionId: "" });
        }
        while (encoderBindings.count > root.encoderCount) {
            encoderBindings.remove(encoderBindings.count - 1);
        }
    }

    onKeyCountChanged: { _ensureBindings(); _syncFromProfile(); }
    onEncoderCountChanged: { _ensureEncoderBindings(); _syncFromProfile(); }
    Component.onCompleted: {
        _ensureBindings(); _ensureEncoderBindings(); _refreshBreadcrumb(); _syncFromProfile();
    }

    // Rebuild the preview model from the active profile's key bindings. Called
    // on profileChanged so switching profiles (or any commit) refreshes the
    // tiles — the QML model is the editor's source of truth for the preview,
    // and ProfileController.activeKeyBindings() reflects the committed state.
    function _syncFromProfile() {
        for (var i = 0; i < bindings.count; ++i) {
            bindings.set(i, { iconSource: "", label: "", actionKind: 0, actionParams: "",
                              actionId: "", isFolder: false, folderTarget: "" });
        }
        var kb = ProfileController.activeKeyBindings();
        for (var j = 0; j < kb.length; ++j) {
            var b = kb[j];
            if (b.index >= 0 && b.index < bindings.count) {
                bindings.set(b.index, { iconSource: b.iconSource ? b.iconSource : "",
                                        label: b.label ? b.label : "",
                                        actionKind: b.actionKind,
                                        actionParams: "",
                                        actionId: b.actionId ? b.actionId : "",
                                        isFolder: b.isFolder ? true : false,
                                        folderTarget: b.folderTarget ? b.folderTarget : "" });
            }
        }
        // Re-point cells with a cached live-render frame back at the livekey
        // provider. The rebuild above wiped iconSource; a continuously-rendering
        // plugin (System Monitor) re-asserts within a tick via keyImageAssigned,
        // but a ONE-SHOT render (the manifest default image painted at mount,
        // e.g. Weather's icon) stayed on the device while vanishing from the
        // canvas. The store still holds the frame — restore the URL.
        for (var k = 0; k < bindings.count; ++k) {
            var rev = StreamDockControlService.liveKeyRevision(k);
            if (rev >= 0 && !bindings.get(k).iconSource) {
                bindings.setProperty(k, "iconSource", "image://livekey/" + k + "?r=" + rev);
            }
        }

        // US3: rebuild the per-encoder model so each touch-strip segment mirrors
        // its dial's bound action (icon + label). The segment is owned by the dial.
        for (var e = 0; e < encoderBindings.count; ++e) {
            encoderBindings.set(e, { iconSource: "", label: "", actionKind: 0, actionId: "" });
        }
        var eb = ProfileController.activeEncoderBindings();
        for (var m = 0; m < eb.length; ++m) {
            var enc = eb[m];
            if (enc.index >= 0 && enc.index < encoderBindings.count) {
                encoderBindings.set(enc.index, {
                    iconSource: enc.iconSource ? enc.iconSource : "",
                    label: enc.label ? enc.label : "",
                    actionKind: enc.actionKind,
                    actionId: enc.actionId ? enc.actionId : "" });
            }
        }
        // Delta B: re-point encoders with a cached live dial-feedback frame back at
        // the liveencoder provider (the rebuild above wiped iconSource). Mirrors the
        // livekey re-point above: a continuously-rendering dial plugin re-asserts
        // within a tick, but a one-shot feedback render would vanish from the canvas
        // while still on the device. The store still holds the frame -- restore it.
        for (var n = 0; n < encoderBindings.count; ++n) {
            var erev = StreamDockControlService.liveEncoderRevision(n);
            if (erev >= 0 && !encoderBindings.get(n).iconSource) {
                encoderBindings.setProperty(n, "iconSource",
                                            "image://liveencoder/" + n + "?r=" + erev);
            }
        }
    }

    Connections {
        target: ProfileController
        function onProfileChanged() {
            root._refreshBreadcrumb(); // Delta A: page nav also emits profileChanged.
            root._syncFromProfile();
            root._refreshSelectedKeyActionList();
            root._refreshSelectedEncoderBinding();
        }
    }

    readonly property var selectedBinding:
        selectedKeyIndex >= 0 && selectedKeyIndex < bindings.count
            ? bindings.get(selectedKeyIndex)
            : null

    // PLUGIN-23: full onPress action list for the selected key.
    // Derived from activeKeyBindings() "actionList" field. Updated on profileChanged.
    property var _selectedKeyActionList: []

    function _refreshSelectedKeyActionList() {
        if (root.selectedKeyIndex < 0) {
            root._selectedKeyActionList = [];
            return;
        }
        var kb = ProfileController.activeKeyBindings();
        for (var i = 0; i < kb.length; ++i) {
            var b = kb[i];
            if (b.index === root.selectedKeyIndex) {
                root._selectedKeyActionList = b.actionList ? b.actionList : [];
                return;
            }
        }
        root._selectedKeyActionList = [];
    }

    onSelectedKeyIndexChanged: { root._refreshSelectedKeyActionList(); }

    // Selected ENCODER (dial) binding, resolved from activeEncoderBindings() so
    // the Property Inspector can configure a plugin action bound to a dial. Kept
    // reactive by re-resolving when the encoder selection changes AND on
    // profileChanged (a drag-bind/move mutates the profile, not this property).
    property var _selectedEncoderBinding: null
    function _refreshSelectedEncoderBinding() {
        if (root.selectedEncoderIndex < 0) {
            root._selectedEncoderBinding = null;
            return;
        }
        var eb = ProfileController.activeEncoderBindings();
        for (var i = 0; i < eb.length; ++i) {
            if (eb[i].index === root.selectedEncoderIndex) {
                root._selectedEncoderBinding = eb[i];
                return;
            }
        }
        root._selectedEncoderBinding = null;
    }
    onSelectedEncoderIndexChanged: { root._refreshSelectedEncoderBinding(); }

    // The control whose action the Inspector edits: the selected key, else the
    // selected dial. (Touch-zone PI is a follow-up; zones already bind via tap.)
    readonly property var selectedInspectorBinding:
        selectedKeyIndex >= 0
            ? selectedBinding
            : (selectedEncoderIndex >= 0 ? _selectedEncoderBinding : null)

    function updateSelectedBinding(field, value) {
        if (root.selectedKeyIndex < 0 || root.selectedKeyIndex >= bindings.count) return;
        bindings.setProperty(root.selectedKeyIndex, field, value);
        var row = bindings.get(root.selectedKeyIndex);
        ProfileController.commitKeyBinding(root.selectedKeyIndex, row.iconSource,
                                           row.label, row.actionKind, row.actionParams,
                                           row.actionId !== undefined ? row.actionId : "");
    }

    // ---- Track any active drag for trash-zone opacity (WR-02) ---------------
    // Counter tracks how many cell drags are currently in flight. Drives the
    // anyDragActive bool which controls trash-zone opacity (D-10 / D-09).
    // Using a counter (not a simple bool) handles the case where two drags
    // start before the first ends (rare, but avoids a stuck-high bug).
    property int  _activeDragCount: 0
    // anyDragActive: true during any cell-to-cell drag OR any KeyBindingList row drag.
    // DragRelay.active covers both (all drag sources call DragRelay.begin/finish).
    readonly property bool anyDragActive: _activeDragCount > 0 || DragRelay.active

    // ---- Layout JSON loader (D-06, D-07) ------------------------------------
    // Fetches qrc:/qt/qml/AjazzControlCenter/device-layouts/<codename>.json
    // asynchronously (WR-05: sync XHR deprecated in Qt 6.8+; async avoids
    // blocking the QML main thread). Sets root._layout as a side effect;
    // bindings on _layout update when the async load completes.
    // JSON parse is wrapped in try/catch (T-26-11 mitigate).
    function loadLayout(cn) {
        if (cn === "") {
            root._layout = null;
            return;
        }
        var url = "qrc:/qt/qml/AjazzControlCenter/device-layouts/" + cn + ".json";
        var xhr = new XMLHttpRequest();   // WR-05/IN-04: standard constructor, not Qt.createQmlObject
        xhr.open("GET", url, true);       // async=true; avoids deprecated sync-XHR warning
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    try {
                        root._layout = JSON.parse(xhr.responseText);
                    } catch (e) {
                        // Parse failure -- fall through to null (D-07 outline-frame fallback).
                        root._layout = null;
                    }
                } else {
                    root._layout = null;
                }
            }
        };
        xhr.send();
        // _layout starts null; binding updates when the async request completes.
    }

    property var _layout: null

    onCodenameChanged: {
        root._layout = null;   // reset immediately so stale layout does not flicker
        loadLayout(root.codename);
    }

    // TODO(Phase 26 Plan 26-06): _hasPhoto gates per-SKU photo rendering. Currently
    // the outline-frame fallback is always active (v1 note, lines 22-25). When device-
    // layouts/ JSONs land with a "photo" key, this binding activates the photo layer.
    // Until then, _hasPhoto is computed but not yet consumed by any visual element.
    readonly property bool _hasPhoto: _layout !== null && _layout.photo !== undefined
                                       && _layout.photo !== ""

    // ---- Three-column outer layout -----------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // Left/center column (OpenDeck single-view): device canvas on top,
        // Property Inspector docked beneath it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            // ---- Folder breadcrumb (Delta A) -------------------------------
            // Shown only inside a folder (depth > 1). "Back" pops one level; each
            // crumb navigates straight to that page. Editing follows the active
            // page via ProfileController's page-aware key verbs.
            Rectangle {
                id: folderBreadcrumbBar
                objectName: "folderBreadcrumbBar"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                visible: root._breadcrumb.length > 1
                color: Theme.surfaceContainerLow
                radius: Theme.radiusMd

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Button {
                        objectName: "folderBackButton"
                        text: "‹ " + qsTr("Back")
                        flat: true
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: ProfileController.goBackPage()
                    }

                    Repeater {
                        model: root._breadcrumb
                        delegate: RowLayout {
                            required property var modelData
                            required property int index
                            spacing: Theme.spacingSm
                            Text {
                                text: "/"
                                visible: index > 0
                                color: Theme.fgFaint
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Text {
                                objectName: "breadcrumb_" + modelData.id
                                text: modelData.name
                                color: index === root._breadcrumb.length - 1
                                       ? Theme.fgPrimary : Theme.accent
                                font.pixelSize: Theme.typeLabelSmall.pixelSize
                                font.bold: index === root._breadcrumb.length - 1
                                Layout.alignment: Qt.AlignVCenter
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: index < root._breadcrumb.length - 1
                                    onClicked: ProfileController.enterFolder(modelData.id)
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- Canvas header (Stream Deck style) -------------------------
            // A control bar pinned above the device canvas: device identity +
            // grid summary on the left; live-device brightness + clear-all on
            // the right (the controls a Stream Deck user expects next to the
            // canvas, not buried below). Drives the StreamDockControlService
            // singleton with this view's codename — the same backend the old
            // bottom row used. Only shown for an LCD-key device.
            Rectangle {
                id: canvasHeader
                objectName: "canvasHeader"
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                visible: root.codename !== "" && root.keyCount > 0
                color: Theme.surfaceContainerLow
                radius: Theme.radiusMd

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingLg
                    anchors.rightMargin: Theme.spacingLg
                    spacing: Theme.spacingMd

                    ColumnLayout {
                        spacing: 2
                        Layout.alignment: Qt.AlignVCenter

                        // Device identity lives in the editor header and the
                        // left sidebar already switches devices, so the canvas
                        // header just summarises the grid geometry here.
                        Text {
                            text: {
                                let parts = [qsTr("%1 keys").arg(root.keyCount)];
                                if (root.encoderCount > 0)
                                    parts.push(qsTr("%1 dials").arg(root.encoderCount));
                                if (root.touchZoneCount > 0)
                                    parts.push(qsTr("touch strip"));
                                return parts.join(" · ");
                            }
                            color: Theme.fgMuted
                            font.pixelSize: Theme.typeLabelSmall.pixelSize
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: qsTr("Brightness")
                        color: Theme.fgFaint
                        font.pixelSize: Theme.typeLabelSmall.pixelSize
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Slider {
                        id: brightnessSlider
                        objectName: "brightnessSlider"
                        Layout.preferredWidth: 160
                        Layout.alignment: Qt.AlignVCenter
                        from: 0
                        to: 100
                        stepSize: 1
                        value: 80 // matches kDefaultBrightnessPercent in the service
                        Accessible.role: Accessible.Slider
                        Accessible.name: qsTr("Panel brightness")

                        // Debounce LIG writes during drag (T-16a-01 / DISPLAY-09):
                        // at most one write per ~80 ms, plus a final write on release.
                        Timer {
                            id: brightnessDebounce
                            interval: 80
                            repeat: false
                            onTriggered: StreamDockControlService.setBrightness(
                                root.codename, brightnessSlider.value)
                        }
                        onMoved: brightnessDebounce.restart()
                        onPressedChanged: {
                            if (!pressed) {
                                brightnessDebounce.stop();
                                StreamDockControlService.setBrightness(root.codename, value);
                            }
                        }
                    }

                    SecondaryButton {
                        objectName: "clearAllKeysButton"
                        text: qsTr("Clear all")
                        enabled: root.codename !== ""
                        onClicked: StreamDockControlService.clearAll(root.codename)
                        accessibleDescription: qsTr("Blank all LCD keys on the device")
                    }
                }
            }

            // Device chassis area (center, fills remaining height).
            FocusScope {
                id: chassisScope
                Layout.fillWidth: true
                // Canvas/Inspector height split. When the grid does NOT overflow
                // (the common case: AKP05/AKP03/AKP153 all sit at/under the >8-col
                // OR >4-lane-row threshold), the canvas sizes to its NATURAL content
                // height and the Inspector becomes the sole fillHeight pane, taking
                // all the remaining column height. This is what guarantees a
                // non-oversized grid is shown in full without a phantom vertical
                // scrollbar (regression caught by test_under_threshold_is_not_scrollable
                // + the live AKP05E screenshot, where the 2nd key row was clipped:
                // when BOTH panes were fillHeight, the Inspector's larger
                // preferredHeight biased the split and starved the canvas below its
                // content height). When the grid DOES overflow (e.g. a 45-key SKU),
                // the canvas reverts to a shared-flex 250 px pane and its own
                // ScrollView scrolls. The Inspector stays fillHeight in both modes, so
                // it is never pushed below the fold (the toggle-state-editor fix).
                // minimumHeight still guards EDIT-01: a short window must not squeeze
                // the canvas to ~0 (clip:true would hide it); the ScrollView scrolls.
                Layout.fillHeight: root._gridOverflows
                Layout.preferredHeight: root._gridOverflows
                    ? 250
                    : (deviceCanvas.implicitHeight + 2 * Theme.spacingMd)
                Layout.minimumHeight: 200

            Accessible.role: Accessible.Table
            Accessible.name: root.codename !== ""
                ? qsTr("%1 device editor").arg(root.codename)
                : qsTr("Device editor")

            // Chassis content: photo path OR outline-frame fallback.
            // UI-REVIEW.md fix: D-04 photo background now actually renders.
            // When _hasPhoto is true (layout JSON declares a photo, Plan 26-06
            // shipped 17 layout JSONs), the Image{} below provides the Elgato-
            // pattern background and the outlineFrame Rectangle yields the
            // visual to the photo. When _hasPhoto is false (layout missing,
            // photo field absent, or image fails to load), D-07 outline-frame
            // remains the always-renders fallback.
            Item {
                id: chassisArea
                anchors.fill: parent

                // ---- D-04 photo background (Elgato-pattern primary visual) ----
                // Renders behind the cell-grid Flickable when a layout JSON declares
                // a photo and the asset resolves. fillMode = PreserveAspectFit so
                // the photo respects its native aspect ratio inside the chassis area.
                Image {
                    id: chassisPhoto
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    visible: root._hasPhoto && status === Image.Ready
                    // Photos live under the qrc tree at icons/devices/products/
                    // per components/DeviceImage.qml convention (CMake auto-globs
                    // resources/devices/products/product-<codename>.png there).
                    source: root._hasPhoto
                        ? "qrc:/qt/qml/AjazzControlCenter/icons/devices/products/" + root._layout.photo
                        : ""
                    // Accessibility: photo is decorative; cell-grid below carries
                    // the actionable Accessible descriptions.
                    Accessible.ignored: true
                }

                // ---- Device-shaped schematic (D-07 always-renders fallback) ----
                // Geometry-driven OpenDeck-style canvas: a centered device frame
                // with the physical Stream Dock stack — key grid on top, the
                // horizontal touch strip beneath it, rotary dials at the bottom
                // (one under each strip zone). Active whenever no per-SKU photo is
                // available; the editor is NEVER broken (renders from descriptor
                // geometry alone). All cell intent is forwarded to the same
                // ProfileController + selection wiring as before.
                // ---- EDIT-01: vertical-only scroll wrapper -----------------
                // Wrap the device grid in a width-constrained vertical ScrollView
                // so oversized SKU grids (>8 cols OR >4 rows incl. encoder+touch
                // lanes) scroll instead of clipping/overflowing (32-UI-SPEC).
                // The ScrollView takes over the canvas's former anchors.fill +
                // Theme.spacingMd margins. trashBtn + EmptyState + chassisPhoto
                // stay pinned chassisArea siblings (NOT inside the scroll). The
                // content width is constrained to availableWidth so no phantom
                // horizontal scrollbar is ever produced (vertical only). Both the
                // ScrollView and its contentItem Flickable are debug-addressable
                // (objectName) so qml.get can read scroll-position properties
                // (CLAUDE.md every-control-debug-addressable rule).
                ScrollView {
                    id: deviceCanvasScroll
                    objectName: "deviceCanvasScroll"
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    clip: true
                    visible: !chassisPhoto.visible
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    contentItem.objectName: "deviceCanvasFlick"

                DeviceCanvas {
                    id: deviceCanvas
                    objectName: "deviceCanvas"
                    width: deviceCanvasScroll.availableWidth
                    // Natural content height ONLY (the device frame's implicitHeight).
                    // Binding height to deviceCanvasScroll.availableHeight created a
                    // viewport<->content feedback path that collapsed the ScrollView
                    // (availableHeight resolved to 0 -> chassis height -24 -> canvas
                    // invisible in the real app; the offscreen explicitly-sized unit
                    // tests masked it). The ScrollView derives contentHeight from this
                    // and scrolls (AsNeeded) when the frame is taller than the viewport.
                    // Width is constrained to availableWidth so no horizontal scrollbar
                    // is ever produced (vertical-only).
                    height: implicitHeight

                    keyRows:        root.keyRowsResolved
                    keyColumns:     root._keyColumnsResolved
                    keyCount:       root.keyCount
                    encoderCount:   root.encoderCount
                    touchZoneCount: root.touchZoneCount
                    deviceName:     root.codename
                    bindings:       bindings
                    encoderBindings: encoderBindings
                    selectedKeyIndex:     root.selectedKeyIndex
                    selectedEncoderIndex: root.selectedEncoderIndex
                    selectedZoneIndex:    root.selectedZoneIndex

                    // WR-02: drive the trash-zone opacity counter. Math.max guards
                    // against underflow if a cell is destroyed mid-drag.
                    onCellDragActiveChanged: function(active) {
                        root._activeDragCount = active
                            ? root._activeDragCount + 1
                            : Math.max(0, root._activeDragCount - 1);
                    }

                    onKeyClicked: function(index) {
                        root.selectedKeyIndex = index;
                        root.selectedEncoderIndex = -1;
                        root.selectedZoneIndex = -1;
                        root.keySelected(index);
                        root.keyActivated(index);
                    }
                    onKeySwapRequested: function(src, dst) {
                        // Move/swap a bound action between two keys. The atomic
                        // whole-Binding swap lives in C++ (ProfileController.
                        // swapKeyBindings) so a multi-action onPress chain survives
                        // the move intact — the old two-commitKeyBinding workaround
                        // collapsed it to a single action and dropped onRelease/
                        // onLongPress. The visual `bindings` model + the live key
                        // render are refreshed by the profileChanged() handler that
                        // rebuilds the model, and the plugin lifecycle (willDisappear
                        // on the vacated key, willAppear on the new one) is driven by
                        // the bridge's context reconcile on the same signal.
                        if (src < 0 || dst < 0 || src === dst) return;
                        ProfileController.swapKeyBindings(src, dst);
                    }

                    // Library -> key drop (Workstream B): update the live preview
                    // model AND commit. For a plugin action, payload.actionId is the
                    // dotted UUID the plugin host routes to; iconUrl is a file:// URL
                    // resolved from the plugin's manifest icon.
                    onKeyActionDropped: function(index, payload) {
                        if (index < 0 || index >= bindings.count) return;
                        // Delta A: the "Create folder" built-in (OpenFolder, kind 5)
                        // makes a NEW child page and binds the key to it (a bare
                        // OpenFolder with no target page would be a dead key). The
                        // backend seeds a BackToParent key on the new page.
                        if (payload.actionKind === 5) {
                            ProfileController.createFolderOnKey(index, qsTr("Folder"));
                            root.selectedKeyIndex = index;
                            root.selectedEncoderIndex = -1;
                            root.selectedZoneIndex = -1;
                            root.keySelected(index);
                            return;
                        }
                        var icon = payload.iconUrl ? payload.iconUrl : "";
                        var lbl = payload.label ? payload.label : "";
                        var aid = payload.actionId ? payload.actionId : "";
                        bindings.set(index, { iconSource: icon, label: lbl,
                                              actionKind: payload.actionKind, actionParams: "",
                                              actionId: aid });
                        ProfileController.commitKeyBinding(index, icon, lbl,
                                                           payload.actionKind, "", aid);
                        root.selectedKeyIndex = index;
                        root.selectedEncoderIndex = -1;
                        root.selectedZoneIndex = -1;
                        root.keySelected(index);
                    }

                    // Delta A: double-clicking a folder key navigates the editor
                    // INTO its child page. The folder's target page id lives on the
                    // bindings model (folderTarget, from activeKeyBindings).
                    onKeyFolderOpenRequested: function(index) {
                        if (index < 0 || index >= bindings.count) return;
                        var target = bindings.get(index).folderTarget;
                        if (target && target.length > 0)
                            ProfileController.enterFolder(target);
                    }

                    onEncoderClicked: function(index) {
                        root.selectedKeyIndex = -1;
                        root.selectedEncoderIndex = index;
                        root.selectedZoneIndex = -1;
                        root.encoderSelected(index);
                    }
                    onEncoderSwapRequested: function(src, dst) {
                        ProfileController.swapEncoderBindings(src, dst);
                    }

                    onZoneClicked: function(index) {
                        root.selectedKeyIndex = -1;
                        root.selectedEncoderIndex = -1;
                        root.selectedZoneIndex = index;
                        root.zoneSelected(index);
                    }
                    onZoneSwapRequested: function(src, dst) {
                        ProfileController.swapTouchZoneBindings(src, dst);
                    }
                }
                } // ScrollView#deviceCanvasScroll

                // ---- Trash zone (top-right of chassis, D-09) ---------------
                RoundButton {
                    id: trashBtn
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    width:  Theme.minTouchTarget
                    height: Theme.minTouchTarget
                    opacity: root.anyDragActive ? 1.0 : 0.3

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationShort; easing.type: Theme.easingStandard }
                    }

                    contentItem: Text {
                        anchors.centerIn: parent
                        font.family: "Material Symbols Outlined"
                        font.pixelSize: 22
                        text: "delete"
                        color: Theme.errorAccent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: trashBtn.width / 2
                        color: trashDropArea.containsDrag
                            ? Theme.chipBgError
                            : "transparent"
                        border.width: trashBtn.activeFocus ? Theme.focusRingWidth : 0
                        border.color: Theme.accent
                    }

                    DropArea {
                        id: trashDropArea
                        anchors.fill: parent
                        keys: ["application/x-ajazz-binding"]

                        onDropped: function(drop) {
                            if (DragRelay.mimeKey === "application/x-ajazz-binding") {
                                var bp = JSON.parse(DragRelay.payload);
                                if (bp.controller === "Keypad") {
                                    if (bp.actionPos !== undefined) {
                                        // PLUGIN-23: drag from KeyBindingList row — remove just
                                        // that action from the onPress chain (not the whole key binding).
                                        ProfileController.removeKeyActionAt(bp.position, bp.actionPos);
                                    } else {
                                        // Phase 26 v1: key-cell drag — clear the entire binding.
                                        ProfileController.commitKeyBinding(bp.position, "", "", 0, "");
                                        if (bp.position >= 0 && bp.position < bindings.count) {
                                            bindings.set(bp.position, {
                                                iconSource: "", label: "",
                                                actionKind: 0, actionParams: ""
                                            });
                                        }
                                    }
                                } else if (bp.controller === "TouchZone") {
                                    ProfileController.commitTouchZoneBinding(bp.position, "", "", 0, "");
                                }
                                drop.acceptProposedAction();
                            } else {
                                drop.accepted = false;
                            }
                        }
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Clear binding")
                    Accessible.description: qsTr("Drop a configured cell here to remove its binding")
                }

                // ---- Empty state (no keys configured) ----------------------
                EmptyState {
                    anchors.centerIn: parent
                    visible: root.keyCount === 0 && root.encoderCount === 0
                             && root.touchZoneCount === 0
                    title: qsTr("No layout data for this device")
                    body: qsTr("You can still configure keys using the grid below.")
                }
            }
        }

            // ---- Per-key multi-action binding list (PLUGIN-23, Phase 29-03) -----
            // Shown only when a key is selected AND it has >= 1 action in its
            // onPress chain. Surfaces the full action list for reorder/remove/append.
            // Hosted here in DeviceView, NOT inside Inspector.qml (merge-safety).
            KeyBindingList {
                objectName: "keyBindingList"
                Layout.fillWidth: true
                Layout.maximumHeight: 260
                visible: root.selectedKeyIndex >= 0
                         && root._selectedKeyActionList.length > 0
                keyIndex: root.selectedKeyIndex
                actionList: root._selectedKeyActionList
            }

            // Property Inspector docked at the bottom (OpenDeck layout).
            Inspector {
                Layout.fillWidth: true
                // The Inspector is the fillHeight pane: it grows to take all the
                // vertical space the canvas (bounded preferredHeight) leaves, so
                // the full native form (icon + label + action type + params +
                // toggle-state editor) is on-screen at the default window size and
                // expands further when the window is taller. minimumHeight 320
                // keeps the form usable (it still clips + scrolls internally) when
                // a short window forces the panes to their floors.
                Layout.fillHeight: true
                Layout.preferredHeight: 480
                Layout.minimumHeight: 300
                selectionLabel: root.selectedKeyIndex >= 0
                    ? qsTr("Key %1").arg(root.selectedKeyIndex + 1)
                    : (root.selectedEncoderIndex >= 0
                       ? qsTr("Encoder %1").arg(root.selectedEncoderIndex + 1)
                       : (root.selectedZoneIndex >= 0
                          ? qsTr("Zone %1").arg(root.selectedZoneIndex + 1)
                          : ""))
                // Key OR dial binding, so the PI configures whichever control is
                // selected (goal: configure plugin settings for dials too).
                binding: root.selectedInspectorBinding
                // PLUGIN-22: feed the wire context inputs so the Inspector can
                // assemble the wire context id. Controller-aware: keyIndex for a
                // Keypad context (device#root#Keypad#row#col), encoderIndex for an
                // Encoder context (device#root#Encoder#0#col).
                deviceCodename: root.codename
                keyIndex: root.selectedKeyIndex
                encoderIndex: root.selectedEncoderIndex
                onBindingFieldChanged: function(field, value) {
                    root.updateSelectedBinding(field, value);
                }
            }
        }

        // Right: Action library sidebar (OpenDeck-style, fixed width).
        ActionLibraryPane {
            Layout.preferredWidth: 288
            Layout.fillHeight: true
        }
    }
}
