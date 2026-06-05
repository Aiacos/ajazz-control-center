// SPDX-License-Identifier: GPL-3.0-or-later
//
// Main application window for AJAZZ Control Center.
//
// Layout (top to bottom):
//   * AppHeader    — product mark, search, minimize-to-tray.
//   * RowLayout
//     * DeviceList  — sidebar with the connected devices.
//     * ProfileEditor — middle pane with Keys/RGB/Mouse tabs (encoders are
//       edited as dials on the device canvas inside the Keys tab).
//     * Inspector   — right pane with the form fields for the selected element.
//
// Responsive behaviour (F-17):
//   * Below 700 px the sidebar collapses to a fixed 64 px icons-only column.
//   * Below 1100 px the Inspector pane is hidden entirely.
//
// Toast notifications surface profile save / load events from
// ProfileController.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter
import "components"

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: Branding.productName
    color: Theme.bgBase

    // True when the device currently being edited is a Stream Deck
    // (DeviceFamily::StreamDeck == 1) — drives the OpenDeck pure-view chrome
    // (the per-device settings button in the nav; the deck has no editor tabs).
    readonly property bool _activeIsDeck:
        editor.capabilities && editor.capabilities.family === 1

    // True while the debounced deck auto-save is writing, so the profileSaved
    // toast stays silent for auto-saves (OpenDeck shows no per-edit toast).
    // Explicit Apply (keyboard/mouse) leaves this false, so it still toasts.
    property bool _autoSaving: false

    // Register the bundled Material Symbols Outlined icon font at startup so the
    // app-wide `font.family: "Material Symbols Outlined"` references resolve to
    // real glyphs instead of falling back to a system font (absent on stock
    // Fedora/Wayland), which renders the ligature names as literal text.
    FontLoader {
        id: materialSymbolsFont
        source: "qrc:/qt/qml/AjazzControlCenter/fonts/MaterialSymbolsOutlined-subset.ttf"
    }

    // Auto-select the first connected device on startup so the editor opens to a
    // real device instead of the empty "select a device" state (matching
    // OpenDeck/Elgato, which always open to a connected device).
    Component.onCompleted: {
        if (editor.codename === "") {
            DeviceModel.refresh();
            var cn = DeviceModel.firstConnectedCodename();
            if (cn !== "") {
                StreamDockControlService.setActiveDevice(cn);
                editor.codename = cn;
                editor.capabilities = DeviceModel.capabilitiesFor(cn);
                deviceSelector.selectCodename(cn); // sync the nav dropdown
            }
        }
    }

    // Material Design theme — bind to ThemeService.effectiveMode (always
    // resolved to "light" or "dark", never "auto"). This is the single
    // source of truth for "is the UI light or dark right now"; binding
    // here AND on each Popup (Drawer) below keeps Material's chrome
    // (SwitchDelegate / RadioButton text colour, etc.) in lockstep with
    // BrandingService's palette.
    //
    // We deliberately don't use Material.System for the auto branch — on
    // Wayland-only sessions without an XDG portal Material.System defaults
    // to Light while ThemeService's auto-resolution defaults to Dark,
    // producing Light Material chrome on Dark BrandingService surfaces
    // (black text on dark = invisible). effectiveMode resolves both
    // sides through the same QStyleHints query so they cannot disagree.
    readonly property int materialTheme:
        ThemeService.effectiveMode === "light" ? Material.Light : Material.Dark
    Material.theme: root.materialTheme
    // Pull the accent and primary tones from the branding palette so a custom
    // theme.json keeps the Material chrome on-brand.
    Material.accent: Theme.accent
    Material.primary: Theme.accent2

    // Surface the tray's "Show window" action.
    Connections {
        target: Tray
        function onShowWindowRequested() {
            root.show();
            root.raise();
            root.requestActivate();
        }
    }

    // Surface profile-controller IO results as toasts.
    Connections {
        target: ProfileController
        function onProfileSaved(path) {
            // Deck auto-saves are silent (OpenDeck shows no per-edit confirmation);
            // only an explicit Apply on the keyboard/mouse footer toasts.
            if (root._autoSaving) return;
            toast.show(qsTr("Profile saved"), "success");
        }
        function onSaveFailed(reason) {
            toast.show(qsTr("Save failed: %1").arg(reason), "error");
        }
        function onLoadFailed(reason) {
            toast.show(qsTr("Load failed: %1").arg(reason), "error");
        }
    }

    // Surface SettingsService outcomes (issue #57 — AK-series settings batch).
    // The per-device SettingsRow fires SettingsService.setSettings on Apply;
    // the success/failure signals bubble here so the toast is always anchored
    // at the bottom of the main window regardless of which drawer / tab is open.
    Connections {
        target: SettingsService
        function onSettingsApplied(codename) {
            toast.show(qsTr("Settings applied to %1").arg(codename), "success");
        }
        function onSettingsFailed(codename, message) {
            toast.show(qsTr("Settings failed for %1: %2").arg(codename).arg(message), "error");
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 2026-05-18 / docs/architecture/APP-AUTO-UPDATE.md: in-app update
        // banner. Visibility is bound to AppUpdate.status so the banner
        // appears only when a strictly-newer GitHub release is observed
        // (and only on non-Flatpak builds; Flatpak self-disables and the
        // status pins to Disabled). The banner row collapses to height 0
        // when hidden so it doesn't reserve layout space.
        UpdateBanner {
            Layout.fillWidth: true
            visible: AppUpdate.status === AppUpdate.UpdateAvailable
        }

        // ── OpenDeck-style top nav (replaces AppHeader + the left DeviceList) ──
        // Top-left: the device dropdown with the profile dropdown stacked
        // beneath it (OpenDeck's DeviceSelector + ProfileManager). Top-right:
        // the plugin / runtime / settings surfaces as compact buttons. The
        // former 320 px device sidebar is gone — OpenDeck has no device list.
        Rectangle {
            id: nav
            Layout.fillWidth: true
            // Fixed height tall enough for the two stacked dropdowns (device
            // over profile). Computing from navRow.implicitHeight collapsed to a
            // single row, letting the editor canvas paint over the profile
            // buttons; a floor keeps the whole nav cluster inside its band.
            Layout.preferredHeight: Math.max(104, navRow.implicitHeight + Theme.spacingMd * 2)
            color: Theme.bgSidebar

            RowLayout {
                id: navRow
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                anchors.topMargin: Theme.spacingMd
                anchors.bottomMargin: Theme.spacingMd
                spacing: Theme.spacingLg

                // Device + profile dropdowns, stacked (OpenDeck top-left).
                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.spacingXs

                    DeviceSelector {
                        id: deviceSelector
                        onDeviceSelected: codename => {
                            StreamDockControlService.setActiveDevice(codename); // REQ-26-A, closes GAP-25A
                            editor.codename = codename;
                            editor.capabilities = DeviceModel.capabilitiesFor(codename);
                        }
                    }

                    ProfileBar {
                        objectName: "navProfileBar"
                        visible: editor.codename !== ""
                        deviceCodename: editor.codename
                    }
                }

                Item { Layout.fillWidth: true }

                // Right cluster. The per-device settings/firmware button shows
                // only for decks (keyboards/mice still reach those via their
                // tabbed editor); the rest mirror the old AppHeader actions.
                ToolButton {
                    objectName: "navDeviceSettings"
                    visible: editor.codename !== "" && root._activeIsDeck
                    text: qsTr("Device")
                    font.pixelSize: Theme.fontMd
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Per-device settings and firmware")
                    onClicked: deviceDrawer.open()
                }
                ToolButton {
                    objectName: "navPlugins"
                    text: qsTr("Plugins")
                    font.pixelSize: Theme.fontMd
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open the plugin store")
                    onClicked: pluginStoreDrawer.open()
                }
                ToolButton {
                    objectName: "navLoaded"
                    text: qsTr("Loaded")
                    font.pixelSize: Theme.fontMd
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open the loaded-plugins panel")
                    onClicked: loadedPluginsDrawer.open()
                }
                ToolButton {
                    objectName: "navDebug"
                    text: qsTr("Debug")
                    font.pixelSize: Theme.fontMd
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open the plugin debug console")
                    onClicked: debugDrawer.open()
                }
                ToolButton {
                    objectName: "navSettings"
                    text: qsTr("Settings")
                    font.pixelSize: Theme.fontMd
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open application settings")
                    onClicked: settingsDrawer.open()
                }
                ToolButton {
                    objectName: "navMinimize"
                    text: "—"
                    font.pixelSize: Theme.fontLg
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Minimize to tray")
                    onClicked: root.hide()
                }
            }

            // Bottom hairline separator.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.borderSubtle
            }
        }

        // Center editor — full width (OpenDeck has no left sidebar). The action
        // library and property inspector live inside the editor's DeviceView.
        ProfileEditor {
            id: editor
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Apply/Revert/Restore stay wired for the keyboard/mouse footer;
            // decks have no footer and auto-save via the debounced timer below.
            onApplyRequested: ProfileController.saveActiveProfile()
            onRevertRequested: ProfileController.loadActiveProfile()
            onRestoreDefaultsRequested: ProfileController.resetActiveProfile()
        }
    }

    Toast { id: toast }

    // ── OpenDeck live-persistence model (decks only) ──────────────────────
    // OpenDeck has no Apply button: every edit persists immediately. We mirror
    // that by debouncing saveActiveProfile() on profileChanged(). Binding
    // mutations (commitKeyBinding / swap*) emit profileChanged() but do NOT
    // write to disk; this timer coalesces a burst of edits into one atomic
    // save ~500 ms after the last change. No signal loop: saveActiveProfile()
    // emits the distinct profileSaved(), never profileChanged(). _autoSaving
    // brackets the call so the profileSaved toast stays silent for auto-saves.
    Timer {
        id: autoSaveTimer
        interval: 500
        repeat: false
        onTriggered: {
            root._autoSaving = true;
            ProfileController.saveActiveProfile();
            root._autoSaving = false;
        }
    }
    Connections {
        target: ProfileController
        // Gate on _activeIsDeck: keyboards/mice keep their explicit Apply/Revert
        // footer, and Revert (loadActiveProfile) reloads the last *saved* file —
        // auto-saving them would overwrite that file on every edit and silently
        // break Revert. Only decks (no footer) use live-persistence.
        function onProfileChanged() { if (root._activeIsDeck) autoSaveTimer.restart(); }
    }

    // Compact green confirmation, top-right, ONLY for a user-initiated "Sync
    // time now" (manualSyncSucceeded). Automatic syncs (startup sweep,
    // on-connect, 15-min tick, hot-plug) stay silent per D-02 — otherwise they
    // stack one toast per device every tick. The device Settings tab still
    // shows an inline ✓/✗ status for both manual and automatic syncs.
    Notification { id: syncNote }
    Connections {
        target: TimeSyncService
        function onManualSyncSucceeded(codename) {
            syncNote.show(qsTr("Time synced: %1").arg(codename));
        }
    }

    // ----------------------------------------------------------------------
    // Plugin Store drawer.
    //
    // The Plugin Store is a Page that renders the catalogue exposed by the
    // C++-side PluginCatalogModel. We mount it inside a modal Drawer
    // anchored to the right edge so the user can browse / install plugins
    // without losing the device list and editor state in the background.
    // The drawer width clamps to 75 % of the window so the grid still has
    // room for at least three tile columns on a 1280 px-wide layout.
    // ----------------------------------------------------------------------
    Drawer {
        id: pluginStoreDrawer
        objectName: "pluginStoreDrawer"
        edge: Qt.RightEdge
        modal: true
        dragMargin: 0 // disable edge-drag — only the header button opens it.
        width: Math.min(960, Math.max(720, root.width * 0.75))
        height: root.height

        // Material attached props don't propagate from ApplicationWindow to
        // Popups; bind explicitly so Material.theme inside the drawer matches
        // ThemeService.effectiveMode (and stays in sync with BrandingService).
        Material.theme: root.materialTheme
        Material.accent: Theme.accent
        Material.primary: Theme.accent2

        background: Rectangle {
            color: Theme.surfaceContainer
            border.color: Theme.borderSubtle
            border.width: 1
        }

        PluginStore {
            anchors.fill: parent
        }
    }

    // ----------------------------------------------------------------------
    // Loaded plugins drawer (SEC-003 #51 trust UI).
    //
    // Mirrors the Plugin Store drawer dimensions because the row layout
    // benefits from the same horizontal real estate; the page itself is
    // a vertical list rather than a grid, but the visual rhythm matches.
    // ----------------------------------------------------------------------
    Drawer {
        id: loadedPluginsDrawer
        objectName: "loadedPluginsDrawer"
        edge: Qt.RightEdge
        modal: true
        dragMargin: 0
        width: Math.min(960, Math.max(720, root.width * 0.75))
        height: root.height

        Material.theme: root.materialTheme
        Material.accent: Theme.accent
        Material.primary: Theme.accent2

        background: Rectangle {
            color: Theme.surfaceContainer
            border.color: Theme.borderSubtle
            border.width: 1
        }

        LoadedPluginsPage {
            anchors.fill: parent
        }
    }

    // ----------------------------------------------------------------------
    // Plugin debug console drawer — protocol log + input/response simulation.
    // Same right-edge / modal pattern as the other drawers.
    // ----------------------------------------------------------------------
    Drawer {
        id: debugDrawer
        edge: Qt.RightEdge
        modal: true
        dragMargin: 0
        width: Math.min(960, Math.max(720, root.width * 0.75))
        height: root.height

        Material.theme: root.materialTheme
        Material.accent: Theme.accent
        Material.primary: Theme.accent2

        background: Rectangle {
            color: Theme.surfaceContainer
            border.color: Theme.borderSubtle
            border.width: 1
        }

        DebugConsole {
            anchors.fill: parent
        }
    }

    // ----------------------------------------------------------------------
    // Settings drawer.
    //
    // The Settings page is a single column of switches + a theme picker, so
    // a narrower drawer (clamped between 360 px and 560 px) is enough.
    // Same right-edge / modal pattern as the Plugin Store — only one drawer
    // is open at a time, since Drawer is a popup and Qt forces popups to be
    // mutually exclusive when both are modal.
    // ----------------------------------------------------------------------
    Drawer {
        id: settingsDrawer
        objectName: "settingsDrawer"
        edge: Qt.RightEdge
        modal: true
        dragMargin: 0
        width: Math.min(560, Math.max(360, root.width * 0.4))
        height: root.height

        // See pluginStoreDrawer — Material attached props don't propagate
        // into Popup scope, so we re-apply the same trio here.
        Material.theme: root.materialTheme
        Material.accent: Theme.accent
        Material.primary: Theme.accent2

        background: Rectangle {
            color: Theme.surfaceContainer
            border.color: Theme.borderSubtle
            border.width: 1
        }

        SettingsPage {
            anchors.fill: parent
        }
    }

    // ----------------------------------------------------------------------
    // Per-device settings + firmware drawer (OpenDeck parity).
    //
    // A deck's editor is now chrome-less (no Settings/Firmware tabs), so its
    // per-device time-sync settings and firmware updater live here, opened from
    // the nav "Device" button. Reuses the same SettingsRow + FirmwarePanel the
    // keyboard/mouse tabbed editor uses; properties are read off the active
    // device's capability map (editor.capabilities).
    // ----------------------------------------------------------------------
    Drawer {
        id: deviceDrawer
        objectName: "deviceDrawer"
        edge: Qt.RightEdge
        modal: true
        dragMargin: 0
        width: Math.min(560, Math.max(360, root.width * 0.4))
        height: root.height

        Material.theme: root.materialTheme
        Material.accent: Theme.accent
        Material.primary: Theme.accent2

        background: Rectangle {
            color: Theme.surfaceContainer
            border.color: Theme.borderSubtle
            border.width: 1
        }

        ScrollView {
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: Theme.spacingLg

                Text {
                    Layout.fillWidth: true
                    text: editor.capabilities && editor.capabilities.model
                              ? editor.capabilities.model
                              : editor.codename
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.fontXl
                    font.bold: true
                    elide: Text.ElideRight
                }

                SettingsRow {
                    Layout.fillWidth: true
                    deviceCodename: editor.codename
                    hasSettings: editor.capabilities && editor.capabilities.hasSettings
                                     ? editor.capabilities.hasSettings : false
                    hasClock: editor.capabilities && editor.capabilities.hasClock
                                  ? editor.capabilities.hasClock : false
                    deviceMaturity: editor.capabilities && editor.capabilities.maturity
                                        ? editor.capabilities.maturity : "scaffolded"
                }

                FirmwarePanel {
                    Layout.fillWidth: true
                    deviceCodename: editor.codename
                    deviceFamily: editor.capabilities && editor.capabilities.family !== undefined
                                      ? editor.capabilities.family : 0
                }
            }
        }
    }

    // ----------------------------------------------------------------------
    // Drag ghost (Phase 29) — the single cursor-following drag image for the
    // whole editor. Every drag source (action library, key cells, encoder
    // dials, touch-strip zones) routes through the DragRelay singleton because
    // native (Drag.Automatic) drag is not delivered on Wayland/niri. This item
    // carries `Drag.active`/`Drag.mimeData`, so internal-drag hit-testing of the
    // editor's DropAreas works off its scene geometry. It is a plain last child
    // of the window content (renders above the editor; same scene as the
    // DropAreas, origin (0,0) so scenePosition maps 1:1) and follows the cursor.
    // It has no input handlers, so it never steals the source DragHandler's grab.
    // (We deliberately do NOT reparent into Overlay.overlay: that put the single
    // item under two parents in the object tree and broke findByName addressing;
    // a drag never happens with a modal drawer open, so contentItem Z is fine.)
    // ----------------------------------------------------------------------
    Item {
        id: dragGhost
        objectName: "dragGhost"          // debug-channel addressable (qml.get)
        z: 100000
        width: 1
        height: 1
        visible: DragRelay.active

        // Top-left tracks the cursor's scene position; Drag.hotSpot is (0,0) so
        // the internal-drag hit point is exactly under the cursor.
        x: DragRelay.hotspot.x
        y: DragRelay.hotspot.y

        Drag.active: DragRelay.active
        Drag.dragType: Drag.Internal
        Drag.hotSpot: Qt.point(0, 0)
        Drag.keys: DragRelay.mimeKey !== "" ? [DragRelay.mimeKey] : []
        // Only one MIME format is ever active; branch instead of a computed key
        // so we never feed a DropArea an empty-string format it would falsely
        // match via hasFormat("").
        Drag.mimeData: DragRelay.mimeKey === "application/x-ajazz-action"
            ? ({ "application/x-ajazz-action": DragRelay.payload })
            : DragRelay.mimeKey === "application/x-ajazz-binding"
              ? ({ "application/x-ajazz-binding": DragRelay.payload })
              : ({})

        // Internal drag needs an explicit drop() to fire onDropped; the relay
        // emits dropRequested() on pointer release while active is still true.
        Connections {
            target: DragRelay
            function onDropRequested() { dragGhost.Drag.drop(); }
        }

        // Floating drag image — a rounded chip offset down-right of the cursor
        // so it doesn't sit directly under the pointer hot point.
        Rectangle {
            id: ghostChip
            x: 14
            y: 14
            width: ghostRow.implicitWidth + Theme.spacingMd * 2
            height: Math.max(40, ghostRow.implicitHeight + Theme.spacingSm * 2)
            radius: Theme.radiusMd
            color: Theme.surfaceContainer
            border.color: Theme.accent
            border.width: 1
            opacity: 0.96

            Row {
                id: ghostRow
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                Image {
                    visible: DragRelay.ghostIconUrl.toString() !== ""
                    source: DragRelay.ghostIconUrl
                    width: 24
                    height: 24
                    sourceSize.width: 24
                    sourceSize.height: 24
                    fillMode: Image.PreserveAspectFit
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: DragRelay.ghostIconUrl.toString() === ""
                             && DragRelay.ghostIconName !== ""
                    font.family: "Material Symbols Outlined"
                    font.pixelSize: 24
                    text: DragRelay.ghostIconName
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: DragRelay.ghostLabel !== ""
                    text: DragRelay.ghostLabel
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.typeBodyMedium.pixelSize
                    font.weight: Theme.typeBodyMedium.weight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
