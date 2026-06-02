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
        { actionLabel: qsTr("Open folder"),     kind: 5, iconName: "folder_open" }
    ]

    function _rebuild() {
        actionModel.clear();

        const builtinGroup = qsTr("Built-in actions");
        for (let i = 0; i < _builtins.length; ++i) {
            const b = _builtins[i];
            // Built-in actions are Key-capable (affordanceMask: 1 = Key).
            actionModel.append({
                group: builtinGroup, actionLabel: b.actionLabel, kind: b.kind,
                iconName: b.iconName, actionId: "", pluginName: "",
                iconUrl: "", propertyInspectorPath: "", isPlugin: false, isHint: false,
                controllers: [], affordanceMask: 1
            });
        }

        const pluginGroup = qsTr("Plugins");
        const actions = (typeof PluginCatalog !== "undefined" && PluginCatalog)
            ? PluginCatalog.installedActions() : [];
        if (actions.length === 0) {
            // A non-draggable hint so the empty Plugins section explains itself.
            // affordanceMask: 0 = non-draggable (hint rows cannot be dropped).
            actionModel.append({
                group: pluginGroup, actionLabel: qsTr("Install plugins from the store"),
                kind: 0, iconName: "extension", actionId: "", pluginName: "",
                iconUrl: "", propertyInspectorPath: "", isPlugin: false, isHint: true,
                controllers: [], affordanceMask: 0
            });
        } else {
            for (let j = 0; j < actions.length; ++j) {
                const a = actions[j];
                actionModel.append({
                    group: pluginGroup, actionLabel: a.actionName, kind: 0,
                    iconName: "extension", actionId: a.actionId, pluginName: a.pluginName,
                    iconUrl: a.icon || "", propertyInspectorPath: a.propertyInspectorPath || "",
                    isPlugin: true, isHint: false,
                    controllers: a.controllers || [], affordanceMask: a.affordanceMask || 0
                });
            }
        }
    }

    Component.onCompleted: root._rebuild()

    Connections {
        target: (typeof PluginCatalog !== "undefined") ? PluginCatalog : null
        function onInstalledCountChanged() { root._rebuild(); }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header row.
        Item {
            Layout.fillWidth: true
            implicitHeight: headerLabel.implicitHeight + Theme.spacingMd * 2

            Text {
                id: headerLabel
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.spacingLg
                }
                text: qsTr("Actions")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleSmall.pixelSize
                font.weight: Theme.typeTitleSmall.weight
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
                required property string section
                width: ListView.view ? ListView.view.width : 0
                height: 28
                color: Theme.bgSidebar
                Text {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: Theme.spacingLg
                    }
                    text: parent.section
                    color: Theme.fgMuted
                    font.pixelSize: Theme.typeLabelMedium.pixelSize
                    font.weight: Font.DemiBold
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
        height: 48
        enabled: !isHint

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
