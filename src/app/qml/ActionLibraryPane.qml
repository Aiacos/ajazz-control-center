// SPDX-License-Identifier: GPL-3.0-or-later
//
// ActionLibraryPane.qml -- left-panel drag-source library for DeviceView.
//
// Two grouped sections (ListView section headers):
//   * "Built-in actions" -- the fixed ActionKind tiles (Key macro, Launch
//     command, Open URL, Open folder).
//   * "Plugins" -- one tile per declared action across every installed plugin,
//     sourced live from PluginCatalog.installedActions() and refreshed whenever
//     installedCountChanged fires (install / uninstall / enable). Dragging a
//     plugin tile binds ActionKind.Plugin with the action's dotted UUID so the
//     plugin host routes the key event to it (Workstream B).
//
// MIME: "application/x-ajazz-action" with payload
//       { actionKind: int, label: string, iconName: string,
//         actionId: string, iconUrl: string, propertyInspectorPath: string,
//         affordanceMask: int }.
//   actionId is "" for built-in kinds and the action UUID for plugin actions.
//   affordanceMask is Key=1, Dial=2, TouchZone=4 (bitmask); 0 = non-draggable.
//   Built-in actions carry affordanceMask:1 (Key-capable); hint row is 0.
//
// Width: fixed 240px (configurable via Layout.preferredWidth on the parent).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Rectangle {
    id: root
    objectName: "actionLibrary" // debug-channel addressing (Stream Deck actions list)

    color: Theme.bgBase

    Layout.preferredWidth: 240
    implicitWidth:          240
    implicitHeight:         360

    // ---- Library model -----------------------------------------------------
    // Rebuilt imperatively (not a static ListModel) so the Plugins section can
    // track installs at runtime. Each row carries a `group` role that drives the
    // ListView section headers, plus the per-action drag payload fields.
    ListModel { id: actionModel }

    readonly property var _builtins: [
        { actionLabel: qsTr("Key macro"),      kind: 2, iconName: "keyboard"    },
        { actionLabel: qsTr("Launch command"), kind: 3, iconName: "terminal"    },
        { actionLabel: qsTr("Open URL"),        kind: 4, iconName: "link"        },
        { actionLabel: qsTr("Create folder"),   kind: 5, iconName: "create_new_folder" }
    ]

    /// Free-text filter (bound to the search field). Empty = show everything.
    property string searchText: ""

    // ---- Collapsible sections ----------------------------------------------
    /// Set of collapsed section names ({name: true}). _collapseRev is bumped on
    /// every toggle so the delegate height/visible bindings re-evaluate (mutating
    /// a JS object in place does not by itself notify QML bindings).
    property var _collapsedSections: ({})
    property int _collapseRev: 0
    function _toggleSection(name) {
        root._collapsedSections[name] = !root._collapsedSections[name];
        root._collapseRev++;
    }
    function _isCollapsed(name) {
        void root._collapseRev; // register the dependency for binding reactivity
        return root._collapsedSections[name] === true;
    }

    function _matches(label, plugin) {
        if (root.searchText === "")
            return true;
        const q = root.searchText.toLowerCase();
        return label.toLowerCase().indexOf(q) !== -1
            || (plugin !== "" && plugin.toLowerCase().indexOf(q) !== -1);
    }

    /// Cached plugin-action list. installedActions() does disk I/O (scans the
    /// plugins dir + parses every manifest), so it is fetched ONLY when the set
    /// of installed plugins changes — never per keystroke. Search filtering and
    /// section collapse operate on this cache.
    property var _pluginActions: []
    function _refetch() {
        root._pluginActions = (typeof PluginCatalog !== "undefined" && PluginCatalog)
            ? PluginCatalog.installedActions() : [];
        root._rebuild();
    }

    function _rebuild() {
        actionModel.clear();

        const builtinGroup = qsTr("Built-in actions");
        for (let i = 0; i < _builtins.length; ++i) {
            const b = _builtins[i];
            if (!root._matches(b.actionLabel, ""))
                continue;
            // Built-in actions are Key-capable (affordanceMask: 1 = Key).
            actionModel.append({
                group: builtinGroup, actionLabel: b.actionLabel, kind: b.kind,
                iconName: b.iconName, actionId: "", pluginName: "",
                iconUrl: "", propertyInspectorPath: "", isPlugin: false, isHint: false,
                controllers: [], affordanceMask: 1
            });
        }

        // System group: built-in actions that bind to a DIAL (affordanceMask 2 =
        // Encoder). "Volume" drives the OS volume via the in-process
        // com.hotspot.streamdock.system.volume action (rotate up/down, press =
        // mute) — the dial drop handler routes this id to commitEncoderVolume.
        const systemGroup = qsTr("System");
        if (root._matches(qsTr("Volume"), "")) {
            actionModel.append({
                group: systemGroup, actionLabel: qsTr("Volume"), kind: 0,
                iconName: "volume_up",
                actionId: "com.hotspot.streamdock.system.volume", pluginName: "",
                iconUrl: "", propertyInspectorPath: "", isPlugin: false, isHint: false,
                controllers: ["Encoder"], affordanceMask: 2
            });
        }

        const actions = root._pluginActions;
        // Stream Deck groups actions UNDER each plugin: one collapsible section per
        // plugin, not a single "Plugins" bucket. ListView sections require items of
        // the same section to be CONSECUTIVE, so sort by plugin name (then action
        // name) before appending — otherwise a plugin's actions would split across
        // repeated headers.
        const sorted = actions.slice().sort(function (a, b) {
            const pa = (a.pluginName || "").toLowerCase();
            const pb = (b.pluginName || "").toLowerCase();
            if (pa !== pb)
                return pa < pb ? -1 : 1;
            return (a.actionName || "").toLowerCase() < (b.actionName || "").toLowerCase() ? -1 : 1;
        });
        let shown = 0;
        for (let j = 0; j < sorted.length; ++j) {
            const a = sorted[j];
            if (!root._matches(a.actionName || "", a.pluginName || ""))
                continue;
            actionModel.append({
                // Section header = the owning plugin's display name.
                group: a.pluginName || qsTr("Plugins"), actionLabel: a.actionName, kind: 0,
                iconName: "extension", actionId: a.actionId, pluginName: a.pluginName,
                iconUrl: a.icon || "", propertyInspectorPath: a.propertyInspectorPath || "",
                isPlugin: true, isHint: false,
                controllers: a.controllers || [], affordanceMask: a.affordanceMask || 0
            });
            ++shown;
        }
        // Empty-state hint only when nothing plugin-side is shown AND no search is
        // narrowing the list (a search miss is self-explanatory).
        if (shown === 0 && root.searchText === "") {
            actionModel.append({
                group: qsTr("Plugins"), actionLabel: qsTr("Install plugins from the store"),
                kind: 0, iconName: "extension", actionId: "", pluginName: "",
                iconUrl: "", propertyInspectorPath: "", isPlugin: false, isHint: true,
                controllers: [], affordanceMask: 0
            });
        }
    }

    onSearchTextChanged: root._rebuild()

    Component.onCompleted: root._refetch()

    Connections {
        target: (typeof PluginCatalog !== "undefined") ? PluginCatalog : null
        // Re-read from disk only when the installed set actually changes.
        function onInstalledCountChanged() { root._refetch(); }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: title + search field (Stream Deck's actions panel is search-first).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            Text {
                id: headerLabel
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingXs
                text: qsTr("Actions")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleSmall.pixelSize
                font.weight: Theme.typeTitleSmall.weight
            }

            TextField {
                id: searchField
                objectName: "actionLibrarySearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Search actions…")
                text: root.searchText
                onTextChanged: root.searchText = text
                selectByMouse: true
                Accessible.name: qsTr("Search actions")
            }
        }

        // Divider.
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
        }

        // Tile list.
        ListView {
            id: actionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0
            model: actionModel

            section.property: "group"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                id: sectionHeader
                required property string section
                width: ListView.view ? ListView.view.width : 0
                height: 28
                color: sectionMouse.containsMouse ? Theme.bgRowHover : Theme.bgSidebar

                // Chevron: points down when expanded, right when collapsed.
                Text {
                    id: chevron
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: Theme.spacingMd
                    }
                    font.family: "Material Symbols Outlined"
                    font.pixelSize: 16
                    text: "expand_more"
                    color: Theme.fgMuted
                    rotation: root._isCollapsed(sectionHeader.section) ? -90 : 0
                    Behavior on rotation { NumberAnimation { duration: 120 } }
                }
                Text {
                    anchors {
                        left: chevron.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: Theme.spacingXs
                    }
                    text: sectionHeader.section
                    color: Theme.fgMuted
                    font.pixelSize: Theme.typeLabelMedium.pixelSize
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    id: sectionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root._toggleSection(sectionHeader.section)
                }
            }

            delegate: LibraryTile {}

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Action library")
        }

        Item { Layout.fillHeight: true }
    }

    // -------------------------------------------------------------------------
    // LibraryTile -- inline private component for each action row.
    // -------------------------------------------------------------------------
    component LibraryTile: ItemDelegate {
        id: tile

        required property string group
        required property string actionLabel
        required property int    kind
        required property string iconName
        required property string actionId
        required property string pluginName
        required property string iconUrl
        required property string propertyInspectorPath
        required property bool   isPlugin
        required property bool   isHint
        required property var    controllers   ///< QStringList (from installedActions)
        required property int    affordanceMask ///< Key=1,Dial=2,TouchZone=4 bitmask
        // Qt 6 required-property delegate: when a delegate declares any required
        // model-role property, the implicit `index` context property is NOT
        // auto-injected — it must be declared required too, or bare `index`
        // throws "index is not defined" (caught live via the debug channel).
        required property int    index

        // objectName enables debug-channel addressing (qml.get/set/invoke/click).
        // Every LibraryTile is indexed by its ListView position per CLAUDE.md rule.
        objectName: "libraryTile_" + index

        width:  ListView.view ? ListView.view.width : root.implicitWidth
        // Collapse: a tile in a collapsed section takes no space and is hidden,
        // so its section header acts as an accordion toggle.
        readonly property bool _collapsed: root._isCollapsed(group)
        height: _collapsed ? 0 : 48
        visible: !_collapsed
        clip: true
        enabled: !isHint && !_collapsed

        // ----- Drag source (disabled for the hint row) ---------------------
        // DragHandler grabs the pointer + enforces the threshold; the drag is
        // carried by the shared overlay ghost via DragRelay (Phase 29 — native
        // Drag.Automatic is not delivered on Wayland/niri). MIME
        // "application/x-ajazz-action" is the OpenDeck "action" (copy) drag.
        DragHandler {
            id: tileDragHandler
            target: null
            enabled: !tile.isHint
            acceptedButtons: Qt.LeftButton
            // dragThreshold prevents accidental drag during list flick.
            dragThreshold: 8

            readonly property string _payload: JSON.stringify({
                actionKind: tile.kind,
                label: tile.actionLabel,
                iconName: tile.iconName,
                actionId: tile.actionId,
                iconUrl: tile.iconUrl,
                propertyInspectorPath: tile.propertyInspectorPath,
                affordanceMask: tile.affordanceMask
            })
            onActiveChanged: {
                if (active)
                    DragRelay.begin("application/x-ajazz-action", _payload,
                                    tile.iconUrl, tile.iconName, tile.actionLabel,
                                    centroid.scenePosition.x, centroid.scenePosition.y);
                else
                    DragRelay.finish();
            }
        }
        // Feed the live cursor position to the ghost while dragging.
        Binding {
            target: DragRelay
            property: "hotspot"
            value: tileDragHandler.centroid.scenePosition
            when: tileDragHandler.active
            restoreMode: Binding.RestoreNone
        }

        // Cursor affordance.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            cursorShape: tile.isHint ? Qt.ArrowCursor : Qt.OpenHandCursor
        }

        background: Rectangle {
            color: tile.hovered && !tile.isHint ? Theme.bgRowHover : Theme.bgBase
        }

        contentItem: RowLayout {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: Theme.spacingLg
                rightMargin: Theme.spacingLg
            }
            spacing: Theme.spacingSm

            // Plugin icon image when available; otherwise a Material Symbols glyph.
            Image {
                visible: tile.iconUrl !== ""
                source: tile.iconUrl
                sourceSize.width: 20
                sourceSize.height: 20
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                fillMode: Image.PreserveAspectFit
                smooth: true
                asynchronous: true
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                visible: tile.iconUrl === ""
                font.family: "Material Symbols Outlined"
                font.pixelSize: 20
                text: tile.iconName
                color: tile.isHint ? Theme.fgFaint : Theme.accent
                Layout.alignment: Qt.AlignVCenter
            }

            // Labels: action name, with the owning plugin name beneath it.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: tile.actionLabel
                    color: tile.isHint ? Theme.fgFaint : Theme.fgPrimary
                    font.pixelSize: Theme.typeBodyMedium.pixelSize
                    font.weight: Theme.typeBodyMedium.weight
                    font.italic: tile.isHint
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: tile.isPlugin && tile.pluginName !== ""
                    text: tile.pluginName
                    color: Theme.fgMuted
                    font.pixelSize: Theme.typeLabelSmall.pixelSize
                    elide: Text.ElideRight
                }
            }
        }

        Accessible.role: tile.isHint ? Accessible.StaticText : Accessible.Button
        Accessible.name: tile.isPlugin
            ? qsTr("%1 (%2 plugin action)").arg(tile.actionLabel).arg(tile.pluginName)
            : qsTr("%1 action").arg(tile.actionLabel)
        Accessible.description: tile.isHint
            ? qsTr("Open the plugin store to install Stream Deck plugins")
            : qsTr("Drag to assign %1 to a key").arg(tile.actionLabel)
    }
}
