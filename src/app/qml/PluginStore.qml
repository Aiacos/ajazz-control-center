// SPDX-License-Identifier: GPL-3.0-or-later
//
// PluginStore.qml — Plugin Store browse page for AJAZZ Control Center.
//
// The page surfaces the catalogue exposed by the C++-side
// PluginCatalogModel (registered as the `PluginCatalog` QML context property
// by Application::exposeToQml) and lets the user install, enable / disable
// or uninstall a plugin. Catalogue entries come from four different
// upstream sources, each backed by a tab in the source switcher:
//
//   * "All"              — every catalogue row regardless of source.
//   * "Installed"        — only rows the user has already installed.
//   * "AJAZZ Streamdock" — first-class mirror of the official Streamdock
//                          plugin store at space.key123.vip (the AJAZZ-curated
//                          catalogue; compatibility=streamdock). Default tab.
//   * "OpenDeck"         — mirror of the archived Elgato Stream Deck plugin
//                          store at plugins.amankhanna.me (community-
//                          maintained; compatibility=opendeck).
//   * "Community"        — community-maintained third-party plugins.
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
pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Page {
    id: root
    title: qsTr("Plugin Store")
    background: Rectangle { color: Theme.bgBase }

    /// Index into `tabs.contentChildren` mirroring the active source filter:
    ///   0 = All, 1 = Installed, 2 = AJAZZ Streamdock, 3 = OpenDeck, 4 = Community.
    ///
    /// Default lands on AJAZZ Streamdock (index 2): the live catalogue mirror
    /// of `https://space.key123.vip/interface/user/productInfo/list` is the
    /// curated, vendor-blessed source — first-time users should see those
    /// entries, not the noisy union of every source under "All". The
    /// user's last-active choice is persisted via the `Settings` block
    /// below so subsequent app launches restore it; the AJAZZ default
    /// only applies on the very first launch (when QSettings has no
    /// stored value yet).
    property int activeTab: 2

    /// Persist the user's tab choice across app restarts. The alias
    /// makes Settings read the stored value at component creation
    /// (overriding the QML default if present) and write back on every
    /// change. The `category` keeps the key under
    /// `Aiacos/AjazzControlCenter.conf [PluginStore] activeTab=...`
    /// so future page-scoped persistence can co-exist without clashes.
    Settings {
        category: "PluginStore"
        property alias activeTab: root.activeTab
    }

    /// Lower-cased search query; matched against name/description/tags.
    property string query: ""

    /// UUID of the plugin currently shown in the details pane, or "".
    property string selectedUuid: ""

    /// Bumped after every mutation on `PluginCatalog` so any binding that
    /// reads `entryFor()` (e.g. the side-sheet metadata pane) is forced to
    /// re-resolve. We need this because `entryFor()` returns a plain
    /// QVariantMap with no per-key change-notification.
    property int catalogRevision: 0

    /// Path of the last local-install attempt; used by the self-signed
    /// confirmation dialog to re-issue installFromFile with confirm=true.
    property string pendingLocalInstallPath: ""

    /// Status text shown in the install-from-file banner (success / error).
    property string localInstallStatus: ""

    /// IN-01: shared relative-age formatter (replaces two identical copies
    /// that previously lived inside streamdockBanner and opendeckBanner).
    /// @param unixMs  Unix timestamp in milliseconds (0 = unknown).
    /// @param tick    The caller's relativeAgeTick property (triggers re-eval).
    /// @returns Short human delta: "just now", "3 min ago", "2 days ago", or "".
    function relativeAge(unixMs, tick) {
        void(tick); // dependency: force re-eval when the caller's tick increments
        if (!unixMs || unixMs <= 0) {
            return "";
        }
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

    Connections {
        target: PluginCatalog
        function onInstalledCountChanged() { root.catalogRevision += 1; }
        function onCountChanged()         { root.catalogRevision += 1; }
        function onInstallFinished(path, success, error) {
            // Only handle local-install outcomes here; tile-level installs
            // are handled by the tile Connections block below.
            // WR-01: use a portable check that covers Unix paths (/...),
            // file: URLs, and Windows absolute paths (C:\... or C:/...).
            // UUID-keyed signals (tile installs) never start with these.
            const isLocalPath = path.startsWith("/")
                || path.startsWith("file:")
                || /^[A-Za-z]:[\\/]/.test(path);
            if (!isLocalPath) {
                return;
            }
            if (success) {
                root.localInstallStatus = qsTr("Plugin installed successfully.");
                root.pendingLocalInstallPath = "";
                root.catalogRevision += 1;
            } else if (error === "self-signed plugin -- confirm to install") {
                // Self-signed plugin: show the confirmation dialog.
                selfSignedDialog.open();
            } else if (error === "unsigned plugin -- confirm to install") {
                // Unsigned (no signature) developer sideload: show the unsigned
                // confirmation dialog. The backend gates promotion on the
                // confirm=true re-issue (Unsigned branch only; a tampered /
                // Refused package is NEVER offered a confirm -- CR-01).
                unsignedDialog.open();
            } else {
                root.localInstallStatus = qsTr("Install failed: %1").arg(error);
                root.pendingLocalInstallPath = "";
            }
        }
    }

    // ------------------------------------------------------------------
    // FileDialog: local .sdPlugin / .zip file picker (PLUGIN-14).
    // The selectedFile is a file:// URL — passed directly to installFromFile
    // which normalises it via QUrl::fromUserInput / toLocalFile.
    // ------------------------------------------------------------------
    FileDialog {
        id: fileDialog
        title: qsTr("Install plugin from file")
        nameFilters: [qsTr("Plugin files (*.sdPlugin *.zip)"), qsTr("All files (*)")]
        onAccepted: {
            if (!PluginCatalog) return;
            root.pendingLocalInstallPath = selectedFile.toString();
            root.localInstallStatus = "";
            // installFromFile without confirm; the Connections block above
            // handles the self-signed -> confirm flow.
            PluginCatalog.installFromFile(selectedFile.toString(), false);
        }
    }

    // ------------------------------------------------------------------
    // Self-signed plugin confirmation dialog.
    // Shown when installFromFile emits "self-signed plugin -- confirm to install".
    // ------------------------------------------------------------------
    Dialog {
        id: selfSignedDialog
        title: qsTr("Self-signed plugin")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        anchors.centerIn: parent
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("This plugin has a self-signed signature and has not been "
                + "verified by a trusted publisher. It may have been created by "
                + "a developer for local testing.\n\n"
                + "Do you want to install it anyway?")
        }
        onAccepted: {
            if (!PluginCatalog || root.pendingLocalInstallPath.length === 0) return;
            // Re-issue installFromFile with explicit user confirmation.
            PluginCatalog.installFromFile(root.pendingLocalInstallPath, true);
            root.pendingLocalInstallPath = "";
        }
        onRejected: {
            root.pendingLocalInstallPath = "";
            root.localInstallStatus = qsTr("Install cancelled.");
        }
    }

    // ------------------------------------------------------------------
    // Unsigned plugin confirmation dialog (PLUGIN-16 / Phase 27).
    // Shown when installFromFile emits "unsigned plugin -- confirm to install"
    // (Unsigned = no signature block, a developer sideload). A tampered
    // (Refused) package is NEVER routed here -- CR-01: the backend Refused
    // branch quarantines unconditionally and emits no confirm string.
    // ------------------------------------------------------------------
    Dialog {
        id: unsignedDialog
        objectName: "unsignedConfirmDialog"
        title: qsTr("Unsigned plugin")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        anchors.centerIn: parent
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("This plugin is not signed — it is a developer sideload with "
                + "no publisher signature to verify. Only install plugins you trust.\n\n"
                + "Install it anyway?")
        }
        onAccepted: {
            if (!PluginCatalog || root.pendingLocalInstallPath.length === 0) return;
            // Re-issue with explicit consent; backend promotes via the
            // Unsigned branch (tampered packages can never reach here).
            PluginCatalog.installFromFile(root.pendingLocalInstallPath, true);
            root.pendingLocalInstallPath = "";
        }
        onRejected: {
            root.pendingLocalInstallPath = "";
            root.localInstallStatus = qsTr("Install cancelled.");
        }
    }

    // ----------------------------------------------------------------------
    // Filter proxy. Tab + query filtering used to live in QML JS
    // (rowMatches() applied per-delegate via `visible: matches`), which
    // left phantom slots in the GridView when non-matching tiles
    // collapsed to width:0/height:0 and made the empty-state predicate
    // drift on delegate recycle. PluginCatalogProxyModel (C++,
    // QSortFilterProxyModel) moves that logic into the model layer so
    // the GridView only sees rows that should render.
    // ----------------------------------------------------------------------
    PluginCatalogProxy {
        id: catalogProxy
        sourceModel: PluginCatalog
        activeTab: root.activeTab
        query: root.query
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
            subtitle: PluginCatalog
                ? qsTr("%1 plugins available · %2 installed")
                    .arg(PluginCatalog.count)
                    .arg(PluginCatalog.installedCount)
                : qsTr("Plugin catalogue unavailable")
        }

        // -- Local install + online catalog opt-in row -----------------
        // PLUGIN-14 anti-feature: install from a local .sdPlugin/.zip file.
        // The online catalog is opt-in (default OFF); the user can enable it
        // via the Switch and refresh manually via the "Refresh" button.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Button {
                text: qsTr("Install from file...")
                Material.background: Theme.accent2
                Material.foreground: "white"
                onClicked: fileDialog.open()
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Install a plugin from a local .sdPlugin or .zip file")
                ToolTip.text: qsTr("Install a local .sdPlugin or .zip archive")
                ToolTip.visible: hovered
                ToolTip.delay: 400
            }

            // Status label for local-install outcomes.
            Label {
                Layout.fillWidth: true
                text: root.localInstallStatus
                color: root.localInstallStatus.startsWith(qsTr("Install failed"))
                    ? "#ef5350"
                    : Theme.accent
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
                visible: root.localInstallStatus.length > 0
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true; visible: root.localInstallStatus.length === 0 }

            // Online catalog opt-in toggle (PLUGIN-14 / T-22-phonehome).
            Switch {
                id: onlineCatalogSwitch
                text: qsTr("Online catalog")
                checked: PluginCatalog ? PluginCatalog.onlineCatalogEnabled : false
                onToggled: {
                    if (PluginCatalog) {
                        PluginCatalog.setOnlineCatalogEnabled(checked);
                    }
                }
                ToolTip.text: qsTr("Enable live fetch from the Streamdock and OpenDeck "
                    + "online catalogues. Off by default (local install only).")
                ToolTip.visible: hovered
                ToolTip.delay: 400
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Enable online catalog fetch")
            }

            // Allow-unsigned-plugins opt-in toggle (PLUGIN-16 / Phase 27).
            // Controls PluginCatalog.allowUnsignedPlugins: whether unsigned
            // (.sdPlugin, no signature) plugins install without a per-plugin
            // confirm. Lives here, next to the catalog toggle, because both are
            // PluginCatalog install-policy settings and this is where .sdPlugin
            // plugins are installed. Tampered plugins are ALWAYS blocked
            // regardless of this setting (CR-01).
            Switch {
                id: allowUnsignedSwitch
                objectName: "allowUnsignedSwitch"
                text: qsTr("Allow unsigned plugins")
                checked: PluginCatalog ? PluginCatalog.allowUnsignedPlugins : false
                onToggled: {
                    if (PluginCatalog) {
                        PluginCatalog.setAllowUnsignedPlugins(checked);
                    }
                }
                ToolTip.text: qsTr("When on, unsigned (developer sideload) plugins install "
                    + "without a per-plugin confirm. Tampered plugins are always blocked.")
                ToolTip.visible: hovered
                ToolTip.delay: 400
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Allow unsigned plugins")
            }
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
                TabButton { text: qsTr("OpenDeck"); width: implicitWidth }
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

            readonly property string streamdockState:
                PluginCatalog ? PluginCatalog.streamdockState : "loading"
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
                var ts = PluginCatalog ? PluginCatalog.streamdockFetchedAtUnixMs : 0;
                // IN-01: use the page-level relativeAge, passing the local tick.
                var rel = root.relativeAge(ts, streamdockBanner.relativeAgeTick);
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
                    // Retry — re-runs the live online fetch (refreshOnline).
                    // WR-04: disabled when onlineCatalogEnabled is OFF so the
                    // button cannot trigger outbound HTTP when the user has
                    // explicitly opted out. The C++ refreshOnline() also
                    // guards this, but the UI should reflect the state.
                    ToolButton {
                        text: "↻" // ↻
                        font.pixelSize: Theme.fontMd
                        enabled: streamdockBanner.streamdockState !== "loading"
                            && PluginCatalog
                            && PluginCatalog.onlineCatalogEnabled
                        onClicked: if (PluginCatalog) PluginCatalog.refreshOnline()
                        ToolTip.text: PluginCatalog && PluginCatalog.onlineCatalogEnabled
                            ? qsTr("Refresh catalogue (fetches from the internet)")
                            : qsTr("Enable the Online catalog switch to refresh")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Refresh Streamdock catalogue online")
                    }
                }
            }
        }

        // -- OpenDeck tab info banner -----------------------------------
        // Surfaced only on the OpenDeck tab. Same shape as the Streamdock
        // banner above but bound to PluginCatalogModel.opendeckState /
        // opendeckFetchedAtUnixMs so the user can tell at a glance
        // whether the rows are fresh, cached, or the bundled fallback.
        // The wording explicitly attributes the upstream so the user
        // knows these plugins come from the archived Elgato App Store
        // mirror, not from AJAZZ themselves.
        Frame {
            id: opendeckBanner
            visible: root.activeTab === 3
            Layout.fillWidth: true

            property int relativeAgeTick: 0
            Timer {
                interval: 30000
                running: opendeckBanner.visible
                repeat: true
                onTriggered: opendeckBanner.relativeAgeTick++
            }

            readonly property string opendeckState:
                PluginCatalog ? PluginCatalog.opendeckState : "loading"
            readonly property color statusColor: {
                switch (opendeckBanner.opendeckState) {
                case "online":  return Theme.accent;
                case "cached":  return Theme.accent2;
                case "offline": return Theme.fgMuted;
                default:        return Theme.accent2;
                }
            }
            readonly property string statusGlyph: {
                switch (opendeckBanner.opendeckState) {
                case "online":  return "●";
                case "cached":  return "◐";
                case "offline": return "○";
                default:        return "◔";
                }
            }
            readonly property string statusLine: {
                var ts = PluginCatalog ? PluginCatalog.opendeckFetchedAtUnixMs : 0;
                // IN-01: use the page-level relativeAge, passing the local tick.
                var rel = root.relativeAge(ts, opendeckBanner.relativeAgeTick);
                switch (opendeckBanner.opendeckState) {
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
                    return qsTr("Fetching latest plugins from plugins.amankhanna.me…");
                }
            }

            background: Rectangle {
                color: Theme.tile
                border.color: opendeckBanner.statusColor
                border.width: 1
                radius: Theme.radiusMd
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingXs
                Label {
                    text: qsTr("OpenDeck plugin store")
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontMd
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Plugins on this tab come from the OpenDeck community "
                             + "mirror of the archived Elgato Stream Deck plugin store. "
                             + "They run through the Stream Deck SDK 2 compatibility shim "
                             + "and the same sandbox as native plugins.")
                    color: Theme.fgMuted
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    Label {
                        text: opendeckBanner.statusGlyph
                        color: opendeckBanner.statusColor
                        font.pixelSize: Theme.fontMd
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: opendeckBanner.statusLine
                        color: Theme.fgMuted
                        font.pixelSize: Theme.fontSm
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                    // WR-04: same gate as the Streamdock Refresh button.
                    ToolButton {
                        text: "↻" // ↻
                        font.pixelSize: Theme.fontMd
                        enabled: opendeckBanner.opendeckState !== "loading"
                            && PluginCatalog
                            && PluginCatalog.onlineCatalogEnabled
                        onClicked: if (PluginCatalog) PluginCatalog.refreshOnline()
                        ToolTip.text: PluginCatalog && PluginCatalog.onlineCatalogEnabled
                            ? qsTr("Refresh catalogue (fetches from the internet)")
                            : qsTr("Enable the Online catalog switch to refresh")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Refresh OpenDeck catalogue online")
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
            // `catalogProxy.count` is the single source of truth for the
            // visible-row count now that the proxy filters in C++.
            EmptyState {
                anchors.centerIn: parent
                visible: catalogProxy.count === 0
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

                // Tile sizing — 240 wide × 200 tall, with 12 px gutter.
                cellWidth: 252
                cellHeight: 212
                cacheBuffer: cellHeight * 2

                model: catalogProxy
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
            // Required model-role declarations let qmllint statically resolve
            // every `model.foo` access inside this delegate (without these,
            // every binding triggers an `[unqualified]` warning even with
            // `pragma ComponentBehavior: Bound`).
            required property string uuid
            required property string name
            required property string version
            required property string author
            required property string description
            required property url iconUrl
            required property var tags
            required property string compatibility
            required property string sizeBytes
            required property bool verified
            required property bool installed
            required property string source
            // Direct archive URL (empty for rows whose upstream only exposes a
            // landing page — e.g. the AJAZZ Streamdock store, whose CDN base is
            // not in-app resolvable). Drives the Install-vs-open-browser label so
            // a browser-only row never looks like a broken install.
            required property url downloadUrl
            // In-flight install state: set by the action button onClick,
            // cleared by the Connections block listening on
            // PluginCatalogModel::installFinished. Drives the inline
            // ProgressBar + the "Installing… NN%" button label.
            property bool installing: false
            property int installProgress: 0
            // Filtering is now done in C++ by `catalogProxy`
            // (PluginCatalogProxyModel). The GridView only sees rows that
            // should render, so every delegate is unconditionally visible
            // and sized — no more `visible:false; width:0; height:0`
            // phantom slots, no more recycle-prone `visibleCount` math.
            width: grid.cellWidth - Theme.spacingMd
            height: grid.cellHeight - Theme.spacingMd
            radius: Theme.radiusMd
            color: tileMouse.containsMouse ? Theme.tileHover : Theme.tile
            border.color: root.selectedUuid === tile.uuid ? Theme.accent : Theme.borderSubtle
            border.width: root.selectedUuid === tile.uuid ? Theme.focusRingWidth : 1

            MouseArea {
                id: tileMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.selectedUuid = tile.uuid
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
                        id: tileIcon
                        source: tile.iconUrl
                        sourceSize.width: 80
                        sourceSize.height: 80
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        asynchronous: true
                        Accessible.role: Accessible.Graphic
                        Accessible.name: qsTr("%1 icon").arg(tile.name)

                        // Subtle placeholder shown while the icon is loading
                        // or when the upstream URL fails (e.g. DNS error on
                        // appstore.elgato.com which hosts Stream Deck plugin
                        // icons cross-listed by the Streamdock catalogue).
                        // Keeps the tile geometry stable so switching tabs
                        // doesn't reflow the GridView around broken images.
                        Rectangle {
                            anchors.fill: parent
                            visible: tileIcon.status !== Image.Ready
                            color: Theme.bgRowHover
                            radius: Theme.radiusSm
                            border.color: Theme.borderSubtle
                            border.width: 1
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs

                            Label {
                                text: tile.name
                                color: Theme.fgPrimary
                                font.pixelSize: Theme.fontMd
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // Verified Sigstore badge — small accent dot.
                            Rectangle {
                                visible: tile.verified
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
                            text: qsTr("v%1 · %2").arg(tile.version).arg(tile.author)
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
                    text: tile.description
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
                        color: tile.compatibility === "native"
                            ? Theme.accent
                            : tile.compatibility === "streamdock"
                                ? Theme.accent2
                                : Theme.borderSubtle
                        Layout.preferredHeight: 18
                        Layout.preferredWidth: badge.implicitWidth + Theme.spacingMd
                        Label {
                            id: badge
                            anchors.centerIn: parent
                            text: tile.compatibility
                            color: Theme.fgPrimary
                            font.pixelSize: Theme.fontXs
                            font.bold: true
                        }
                    }
                    Label {
                        text: tile.sizeBytes
                        color: Theme.fgMuted
                        font.pixelSize: Theme.fontXs
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    // Action row + inline download progress.
                    //
                    // Single primary button: Install / Installed / "x %"
                    // depending on the row state. Real in-app download:
                    // PluginCatalogModel.install() issues an HTTPS GET
                    // against the upstream catalogue's `download` URL
                    // (Streamdock: cdn1.key123.vip; OpenDeck: GitHub
                    // release asset) and saves the .sdPlugin archive
                    // under the user plugins directory so the plugin
                    // host picks it up on next start. installProgress
                    // updates a per-tile property; installFinished
                    // resets it.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        // A row is in-app installable only when it carries a
                        // direct archive URL; otherwise install() falls back to
                        // opening the upstream page in the browser, so the button
                        // says so plainly rather than appearing to install.
                        readonly property bool tileInstallable: tile.downloadUrl.toString() !== ""

                        Button {
                            Layout.fillWidth: true
                            text: tile.installing
                                ? qsTr("Installing… %1%").arg(tile.installProgress)
                                : tile.installed
                                    ? qsTr("Installed")
                                    : (parent.tileInstallable ? qsTr("Install")
                                                              : qsTr("Open page ↗"))
                            enabled: !tile.installing
                            flat: tile.installed
                            Material.foreground: tile.installed ? Theme.fgMuted : "white"
                            Material.background: tile.installed
                                ? "transparent"
                                : (parent.tileInstallable ? Theme.accent : Theme.surfaceContainerHigh)
                            onClicked: {
                                if (!PluginCatalog) return;
                                if (tile.installed) {
                                    PluginCatalog.uninstall(tile.uuid);
                                } else if (parent.tileInstallable) {
                                    tile.installing = true;
                                    tile.installProgress = 0;
                                    PluginCatalog.install(tile.uuid);
                                } else {
                                    // Browser-only row: open the upstream store page.
                                    PluginCatalog.install(tile.uuid);
                                }
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: tile.installed
                                ? qsTr("Uninstall %1").arg(tile.name)
                                : (parent.tileInstallable
                                    ? qsTr("Install %1").arg(tile.name)
                                    : qsTr("Open %1 store page in browser").arg(tile.name))
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            visible: tile.installing
                            from: 0
                            to: 100
                            value: tile.installProgress
                        }

                        Connections {
                            target: PluginCatalog
                            function onInstallProgressChanged(uuid, percent) {
                                if (uuid === tile.uuid) {
                                    tile.installProgress = percent;
                                }
                            }
                            function onInstallFinished(uuid, success, error) {
                                if (uuid !== tile.uuid) return;
                                tile.installing = false;
                                tile.installProgress = 0;
                                if (!success && error.length > 0) {
                                    console.warn("Install failed for", tile.name, "-", error);
                                }
                            }
                        }
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
            return PluginCatalog && root.selectedUuid.length > 0
                ? PluginCatalog.entryFor(root.selectedUuid)
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
                    id: detailsIcon
                    source: details.entry.iconUrl || ""
                    sourceSize.width: 96
                    sourceSize.height: 96
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    asynchronous: true

                    // Same placeholder pattern as the tile icon — see
                    // tileIcon for rationale. Keeps the details panel
                    // header layout stable when the upstream URL fails.
                    Rectangle {
                        anchors.fill: parent
                        visible: detailsIcon.status !== Image.Ready
                        color: Theme.bgRowHover
                        radius: Theme.radiusSm
                        border.color: Theme.borderSubtle
                        border.width: 1
                    }
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
                        if (PluginCatalog) {
                            PluginCatalog.toggleEnabled(root.selectedUuid);
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
                        if (!PluginCatalog) return;
                        if (details.entry.installed) {
                            PluginCatalog.uninstall(root.selectedUuid);
                        } else {
                            PluginCatalog.install(root.selectedUuid);
                        }
                    }
                }
            }
        }
    }
}
