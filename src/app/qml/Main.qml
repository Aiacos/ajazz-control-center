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

        AppHeader {
            Layout.fillWidth: true
            onMinimizeRequested: root.hide()
            onPluginStoreRequested: pluginStoreDrawer.open()
            onLoadedPluginsRequested: loadedPluginsDrawer.open()
            onSettingsRequested: settingsDrawer.open()
            onDebugConsoleRequested: debugDrawer.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            DeviceList {
                id: sidebar
                Layout.preferredWidth: root.width < 700 ? 64 : 320
                Layout.fillHeight: true
                model: DeviceModel
                onDeviceSelected: codename => {
                    StreamDockControlService.setActiveDevice(codename); // REQ-26-A, closes GAP-25A
                    editor.codename = codename;
                    editor.capabilities = DeviceModel.capabilitiesFor(codename);
                }
            }

            ProfileEditor {
                id: editor
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Phase 16-02 (PROFILE-01): Apply -> saveActiveProfile persists
                // the active profile to AppDataLocation/profiles/<id>.json via the
                // atomic core writer (profileToJson + writeProfileToDisk). Revert ->
                // loadActiveProfile reloads the last saved version from the same
                // default path, resetting any unsaved edits (profileChanged fires and
                // the UI refreshes). RestoreDefaults: explicit clear + save so the
                // user always gets an honest behaviour (no silent no-op); the profile
                // fields are reset to empty/defaults and saved at the default path.
                onApplyRequested: ProfileController.saveActiveProfile()
                onRevertRequested: ProfileController.loadActiveProfile()
                onRestoreDefaultsRequested: ProfileController.resetActiveProfile()
            }

            // The per-key Inspector now lives inside KeyDesigner (Keys tab)
            // so it has direct access to the binding ListModel and stays in
            // sync with cell-preview updates without cross-component
            // plumbing. Quick task 260514-1je. The previous top-level
            // Inspector placeholder is removed — non-Keys tabs (RGB, Mouse)
            // will grow their own embedded inspectors as those features
            // mature.
        }
    }

    Toast { id: toast }

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
}
