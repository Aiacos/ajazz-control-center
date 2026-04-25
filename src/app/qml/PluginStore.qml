// SPDX-License-Identifier: GPL-3.0-or-later
//
// PluginStore.qml — Plugin Store browse page for AJAZZ Control Center.
//
// The page surfaces the catalogue exposed by the C++-side
// PluginCatalogModel (registered as the `pluginCatalog` QML context property
// by Application::exposeToQml) and lets the user install, enable / disable
// or uninstall a plugin. Catalogue entries come from three different
// upstream sources, each backed by a tab in the source switcher:
//
//   * "All"             — every catalogue row regardless of source.
//   * "Installed"       — only rows the user has already installed.
//   * "AJAZZ Streamdock" — first-class mirror of the official Streamdock
//                          plugin store (compatibility=streamdock).
//   * "Community"       — community-maintained third-party plugins.
//
// Layout:
//
//   PageHeader (title + total / installed count badges)
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │ [Tabs: All | Installed | Streamdock | Community]    [Search field]    │
//   └───────────────────────────────────────────────────────────────────────┘
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │ ┌────────────┐ ┌────────────┐ ┌────────────┐                          │
//   │ │ tile       │ │ tile       │ │ tile       │   (virtualised GridView) │
//   │ │ icon name  │ │ ...        │ │ ...        │                          │
//   │ │ author …   │ │            │ │            │                          │
//   │ │ [Install]  │ │            │ │            │                          │
//   │ └────────────┘ └────────────┘ └────────────┘                          │
//   └───────────────────────────────────────────────────────────────────────┘
//
// Performance notes:
//   * The grid is built on a `GridView` so off-screen tiles are recycled —
//     catalogues with hundreds of entries stay smooth. The `cellWidth` /
//     `cellHeight` are clamped so the tiles never reflow mid-scroll.
//   * Filtering (tab + search) is implemented by hiding non-matching
//     delegates (`visible` + `height: 0`). For larger catalogues we will
//     swap this for a `QSortFilterProxyModel` exposed from C++; that swap
//     is transparent to the QML page since both shapes implement the same
//     role names.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Page {
    id: root
    title: qsTr("Plugin Store")
    background: Rectangle { color: Theme.bgBase }

    /// Index into `tabs.contentChildren` mirroring the active source filter:
    ///   0 = All, 1 = Installed, 2 = Streamdock, 3 = Community.
    property int activeTab: 0

    /// Lower-cased search query; matched against name/description/tags.
    property string query: ""

    /// UUID of the plugin currently shown in the details pane, or "".
    property string selectedUuid: ""

    /// Bumped after every mutation on `pluginCatalog` so any binding that
    /// reads `entryFor()` (e.g. the side-sheet metadata pane) is forced to
    /// re-resolve. We need this because `entryFor()` returns a plain
    /// QVariantMap with no per-key change-notification.
    property int catalogRevision: 0

    Connections {
        target: pluginCatalog
        function onInstalledCountChanged() { root.catalogRevision += 1; }
        function onCountChanged()         { root.catalogRevision += 1; }
    }

    // ----------------------------------------------------------------------
    // Filter helper. Keeping the predicate in JS lets us swap the underlying
    // model (sort/filter proxy) without rewriting the QML.
    // ----------------------------------------------------------------------
    function rowMatches(name, description, tags, source, installed) {
        // Tab filter.
        switch (root.activeTab) {
        case 1: if (!installed) return false; break;
        case 2: if (source !== "streamdock") return false; break;
        case 3: if (source !== "community") return false; break;
        default: break;
        }
        // Search filter — case-insensitive substring across name, summary
        // and tag list. Empty query = match-all.
        if (root.query.length === 0) {
            return true;
        }
        const q = root.query;
        if (name.toLowerCase().indexOf(q) >= 0) return true;
        if (description.toLowerCase().indexOf(q) >= 0) return true;
        for (let i = 0; i < tags.length; ++i) {
            if (String(tags[i]).toLowerCase().indexOf(q) >= 0) return true;
        }
        return false;
    }

    // ----------------------------------------------------------------------
    // Page content.
    // ----------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Plugin Store")
            subtitle: pluginCatalog
                ? qsTr("%1 plugins available · %2 installed")
                    .arg(pluginCatalog.count)
                    .arg(pluginCatalog.installedCount)
                : qsTr("Plugin catalogue unavailable")
        }

        // -- Tab + search row -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            TabBar {
                id: tabs
                Layout.fillWidth: true
                currentIndex: root.activeTab
                onCurrentIndexChanged: root.activeTab = currentIndex

                // Background re-painted to match the dark theme so the
                // default Material light strip never flashes through.
                background: Rectangle {
                    color: Theme.bgSidebar
                    radius: Theme.radiusMd
                    border.color: Theme.borderSubtle
                    border.width: 1
                }

                TabButton { text: qsTr("All"); width: implicitWidth }
                TabButton { text: qsTr("Installed"); width: implicitWidth }
                TabButton { text: qsTr("AJAZZ Streamdock"); width: implicitWidth }
                TabButton { text: qsTr("Community"); width: implicitWidth }
            }

            TextField {
                id: searchField
                Layout.preferredWidth: 280
                placeholderText: qsTr("Search plugins, tags…")
                color: Theme.fgPrimary
                placeholderTextColor: Theme.fgMuted
                onTextChanged: root.query = text.toLowerCase()
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: Theme.tile
                    border.width: searchField.activeFocus ? Theme.focusRingWidth : 1
                    border.color: searchField.activeFocus ? Theme.accent : Theme.borderSubtle
                }
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Search the plugin catalogue")
            }
        }

        // -- Streamdock tab info banner ---------------------------------
        // Surfaced only on the Streamdock tab; explains where the catalogue
        // is sourced from and that installs go through the same sandboxed
        // lifecycle as native plugins. Fixes the recurring user question
        // "are these plugins coming from outside the app?". The lower row
        // is a live status pill bound to PluginCatalogModel.streamdockState
        // / streamdockFetchedAtUnixMs so the user can tell at a glance
        // whether the rows are fresh, cached, or the bundled fallback.
        Frame {
            id: streamdockBanner
            visible: root.activeTab === 2
            Layout.fillWidth: true

            // Cache the fetch timestamp delta so the relative-time text
            // updates every minute without binding directly to a Timer
            // tick on every frame.
            property int relativeAgeTick: 0
            Timer {
                interval: 30000
                running: streamdockBanner.visible
                repeat: true
                onTriggered: streamdockBanner.relativeAgeTick++
            }

            // Translate a unix-ms timestamp into a short human delta
            // ("just now", "3 min ago", "2 days ago"). Returns the empty
            // string for unknown timestamps so callers can elide the
            // suffix entirely.
            function relativeAge(unixMs) {
                if (!unixMs || unixMs <= 0) {
                    return "";
                }
                streamdockBanner.relativeAgeTick;  // depend on the tick
                var deltaSec = Math.max(0, Math.floor((Date.now() - unixMs) / 1000));
                if (deltaSec < 45) {
                    return qsTr("just now");
                }
                if (deltaSec < 3600) {
                    return qsTr("%1 min ago").arg(Math.round(deltaSec / 60));
                }
                if (deltaSec < 86400) {
                    return qsTr("%1 h ago").arg(Math.round(deltaSec / 3600));
                }
                return qsTr("%1 days ago").arg(Math.round(deltaSec / 86400));
            }

            readonly property string streamdockState:
                pluginCatalog ? pluginCatalog.streamdockState : "loading"
            readonly property color statusColor: {
                switch (streamdockBanner.streamdockState) {
                case "online":  return Theme.accent;
                case "cached":  return Theme.accent2;
                case "offline": return Theme.fgMuted;
                default:        return Theme.accent2;  // loading
                }
            }
            readonly property string statusGlyph: {
                switch (streamdockBanner.streamdockState) {
                case "online":  return "●";  // ●
                case "cached":  return "◐";  // ◐
                case "offline": return "○";  // ○
                default:        return "◔";  // ◔ (loading)
                }
            }
            readonly property string statusLine: {
                var ts = pluginCatalog ? pluginCatalog.streamdockFetchedAtUnixMs : 0;
                var rel = streamdockBanner.relativeAge(ts);
                switch (streamdockBanner.streamdockState) {
                case "online":
                    return rel.length > 0
                        ? qsTr("Live catalogue · updated %1").arg(rel)
                        : qsTr("Live catalogue");
                case "cached":
                    return rel.length > 0
                        ? qsTr("Showing cached catalogue · last refreshed %1").arg(rel)
                        : qsTr("Showing cached catalogue");
                case "offline":
                    return qsTr("Offline — using bundled snapshot");
                default:
                    return qsTr("Fetching latest plugins from space.key123.vip…");
                }
            }

            background: Rectangle {
                color: Theme.tile
                border.color: streamdockBanner.statusColor
                border.width: 1
                radius: Theme.radiusMd
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingXs
                Label {
                    text: qsTr("AJAZZ Streamdock store")
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontMd
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Plugins on this tab are mirrored from the official "
                             + "AJAZZ Streamdock catalogue and run inside the same "
                             + "sandbox as native plugins. Verified entries carry a "
                             + "Sigstore signature.")
                    color: Theme.fgMuted
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    Label {
                        text: streamdockBanner.statusGlyph
                        color: streamdockBanner.statusColor
                        font.pixelSize: Theme.fontMd
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: streamdockBanner.statusLine
                        color: Theme.fgMuted
                        font.pixelSize: Theme.fontSm
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }
            }
        }

        // -- Catalogue grid ---------------------------------------------
        // GridView wrapped in a ScrollView for keyboard / wheel scrolling.
        // The wrapping Rectangle gives us the border / radius without a
        // second nested Frame.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgSidebar
            border.color: Theme.borderSubtle
            border.width: 1
            radius: Theme.radiusMd

            // Empty-state fallback when no rows match the current filter.
            EmptyState {
                anchors.centerIn: parent
                visible: grid.visibleCount === 0
                width: Math.min(parent.width - Theme.spacingXl * 2, 360)
                title: root.query.length > 0
                    ? qsTr("No plugins match \u201C%1\u201D").arg(searchField.text)
                    : root.activeTab === 1
                        ? qsTr("No plugins installed yet")
                        : qsTr("Nothing here yet")
                body: root.query.length > 0
                    ? qsTr("Try a different keyword or clear the filter.")
                    : root.activeTab === 1
                        ? qsTr("Install a plugin from the All or AJAZZ Streamdock tab "
                             + "to see it here.")
                        : qsTr("This catalogue source has no entries right now.")
            }

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                clip: true

                /// Recomputed by the delegate's filtering Binding so the
                /// empty-state fallback knows when to show itself.
                property int visibleCount: 0

                // Tile sizing — 240 wide × 200 tall, with 12 px gutter.
                cellWidth: 252
                cellHeight: 212
                cacheBuffer: cellHeight * 2

                model: pluginCatalog
                delegate: pluginTile

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    active: hovered || pressed
                }
            }
        }
    }

    // ----------------------------------------------------------------------
    // Tile delegate — kept in a Component so the GridView can recycle.
    // ----------------------------------------------------------------------
    Component {
        id: pluginTile

        Rectangle {
            id: tile
            // Filtering is applied here so non-matching rows fold to zero
            // height and the GridView reflows; Component recycling keeps
            // this cheap even on catalogues with hundreds of entries.
            readonly property bool matches: root.rowMatches(
                model.name, model.description, model.tags, model.source, model.installed)
            visible: matches
            width: matches ? grid.cellWidth - Theme.spacingMd : 0
            height: matches ? grid.cellHeight - Theme.spacingMd : 0
            radius: Theme.radiusMd
            color: tileMouse.containsMouse ? Theme.tileHover : Theme.tile
            border.color: root.selectedUuid === model.uuid ? Theme.accent : Theme.borderSubtle
            border.width: root.selectedUuid === model.uuid ? Theme.focusRingWidth : 1

            // Bump the visible-count whenever this delegate is shown.
            // Component.onCompleted fires per recycled instance, so we
            // rely on the binding-based counter pattern instead.
            onVisibleChanged: {
                if (visible) grid.visibleCount += 1;
                else grid.visibleCount = Math.max(0, grid.visibleCount - 1);
            }
            Component.onCompleted: if (visible) grid.visibleCount += 1
            Component.onDestruction: if (visible) grid.visibleCount = Math.max(0, grid.visibleCount - 1)

            MouseArea {
                id: tileMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.selectedUuid = model.uuid
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingSm

                // Header row: icon + name + verified badge.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Image {
                        source: model.iconUrl
                        sourceSize.width: 80
                        sourceSize.height: 80
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        Accessible.role: Accessible.Graphic
                        Accessible.name: qsTr("%1 icon").arg(model.name)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs

                            Label {
                                text: model.name
                                color: Theme.fgPrimary
                                font.pixelSize: Theme.fontMd
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // Verified Sigstore badge — small accent dot.
                            Rectangle {
                                visible: model.verified
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: Theme.accent
                                ToolTip.visible: verifiedHover.hovered
                                ToolTip.text: qsTr("Sigstore-verified plugin")
                                HoverHandler { id: verifiedHover }
                            }
                        }
                        Label {
                            text: qsTr("v%1 · %2").arg(model.version).arg(model.author)
                            color: Theme.fgMuted
                            font.pixelSize: Theme.fontXs
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                // One-line summary.
                Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: model.description
                    color: Theme.fgFaint
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                // Footer: source / compat badge + size + action button.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    // Compatibility badge — colour-coded by mode so users
                    // can tell native vs. compat-shimmed plugins apart.
                    Rectangle {
                        radius: Theme.radiusSm
                        color: model.compatibility === "native"
                            ? Theme.accent
                            : model.compatibility === "streamdock"
                                ? Theme.accent2
                                : Theme.borderSubtle
                        Layout.preferredHeight: 18
                        Layout.preferredWidth: badge.implicitWidth + Theme.spacingMd
                        Label {
                            id: badge
                            anchors.centerIn: parent
                            text: model.compatibility
                            color: Theme.fgPrimary
                            font.pixelSize: Theme.fontXs
                            font.bold: true
                        }
                    }
                    Label {
                        text: model.sizeBytes
                        color: Theme.fgMuted
                        font.pixelSize: Theme.fontXs
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    // Action button: switches between Install / Installed
                    // depending on the current row state. The Material
                    // accent makes it stand out without an explicit icon.
                    Button {
                        text: model.installed ? qsTr("Installed") : qsTr("Install")
                        flat: model.installed
                        Material.foreground: model.installed ? Theme.fgMuted : "white"
                        Material.background: model.installed ? "transparent" : Theme.accent
                        onClicked: {
                            if (!pluginCatalog) return;
                            if (model.installed) {
                                pluginCatalog.uninstall(model.uuid);
                            } else {
                                pluginCatalog.install(model.uuid);
                            }
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: model.installed
                            ? qsTr("Uninstall %1").arg(model.name)
                            : qsTr("Install %1").arg(model.name)
                    }
                }
            }
        }
    }

    // ----------------------------------------------------------------------
    // Details side-sheet — opens when a tile is selected. Built as a Drawer
    // anchored to the right edge so it overlays the grid without forcing a
    // layout reshuffle. A second click on the same tile closes the sheet.
    // ----------------------------------------------------------------------
    Drawer {
        id: details
        edge: Qt.RightEdge
        width: Math.min(380, root.width * 0.4)
        height: root.height
        modal: false
        // Open whenever a tile is selected; close when the user clicks the
        // same tile again or chooses "Close".
        visible: root.selectedUuid.length > 0

        background: Rectangle {
            color: Theme.bgSidebar
            border.color: Theme.borderSubtle
            border.width: 1
        }

        // Re-resolved through entryFor() each time the selection or the
        // catalogue revision changes, so we always see the latest install
        // / enabled state without wiring up per-property bindings on the
        // returned QVariantMap (which has no NOTIFY signal).
        readonly property var entry: {
            // eslint-disable-next-line no-unused-expressions
            root.catalogRevision; // dependency: force re-eval on mutations.
            return pluginCatalog && root.selectedUuid.length > 0
                ? pluginCatalog.entryFor(root.selectedUuid)
                : ({});
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd
                Image {
                    source: details.entry.iconUrl || ""
                    sourceSize.width: 96
                    sourceSize.height: 96
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: details.entry.name || ""
                        color: Theme.fgPrimary
                        font.pixelSize: Theme.fontLg
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("v%1 · %2")
                            .arg(details.entry.version || "")
                            .arg(details.entry.author || "")
                        color: Theme.fgMuted
                        font.pixelSize: Theme.fontSm
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                ToolButton {
                    text: "✕"
                    Accessible.name: qsTr("Close details panel")
                    onClicked: root.selectedUuid = ""
                }
            }

            // Description.
            Label {
                Layout.fillWidth: true
                text: details.entry.description || ""
                color: Theme.fgPrimary
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }

            // Metadata grid.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Theme.spacingSm
                columnSpacing: Theme.spacingMd

                Label { text: qsTr("Source"); color: Theme.fgMuted; font.pixelSize: Theme.fontXs }
                Label {
                    text: details.entry.source === "streamdock"
                        ? qsTr("AJAZZ Streamdock store")
                        : details.entry.source === "community"
                            ? qsTr("Community")
                            : qsTr("First-party (Aiacos)")
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontSm
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label { text: qsTr("Compatibility"); color: Theme.fgMuted; font.pixelSize: Theme.fontXs }
                Label {
                    text: details.entry.compatibility || ""
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontSm
                }

                Label {
                    text: qsTr("Streamdock product")
                    color: Theme.fgMuted
                    font.pixelSize: Theme.fontXs
                    visible: details.entry.source === "streamdock"
                }
                Label {
                    text: details.entry.streamdockProductId || ""
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontSm
                    visible: details.entry.source === "streamdock"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label { text: qsTr("Category"); color: Theme.fgMuted; font.pixelSize: Theme.fontXs }
                Label {
                    text: details.entry.category || ""
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontSm
                }

                Label { text: qsTr("Devices"); color: Theme.fgMuted; font.pixelSize: Theme.fontXs }
                Label {
                    text: (details.entry.devices || []).join(", ")
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontSm
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Label { text: qsTr("Size"); color: Theme.fgMuted; font.pixelSize: Theme.fontXs }
                Label {
                    text: details.entry.sizeBytes || ""
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontSm
                }
            }

            Item { Layout.fillHeight: true }

            // Action row — install / uninstall + enable toggle.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Switch {
                    text: qsTr("Enabled")
                    visible: details.entry.installed === true
                    checked: details.entry.enabled === true
                    onToggled: {
                        if (pluginCatalog) {
                            pluginCatalog.toggleEnabled(root.selectedUuid);
                            // Bump the revision so the side-sheet re-binds.
                            root.catalogRevision += 1;
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: details.entry.installed ? qsTr("Uninstall") : qsTr("Install")
                    Material.background: details.entry.installed
                        ? Theme.borderSubtle
                        : Theme.accent
                    Material.foreground: "white"
                    onClicked: {
                        if (!pluginCatalog) return;
                        if (details.entry.installed) {
                            pluginCatalog.uninstall(root.selectedUuid);
                        } else {
                            pluginCatalog.install(root.selectedUuid);
                        }
                    }
                }
            }
        }
    }
}
