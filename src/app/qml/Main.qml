// SPDX-License-Identifier: GPL-3.0-or-later
//
// Main application window for AJAZZ Control Center.
//
// Layout (top to bottom):
//   * AppHeader    — product mark, search, minimize-to-tray.
//   * RowLayout
//     * DeviceList  — sidebar with the connected devices.
//     * ProfileEditor — middle pane with Keys/RGB/Encoders/Mouse tabs.
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            Layout.fillWidth: true
            onMinimizeRequested: root.hide()
            onPluginStoreRequested: pluginStoreDrawer.open()
            onLoadedPluginsRequested: loadedPluginsDrawer.open()
            onSettingsRequested: settingsDrawer.open()
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
                    editor.codename = codename;
                    editor.capabilities = DeviceModel.capabilitiesFor(codename);
                }
            }

            ProfileEditor {
                id: editor
                Layout.fillWidth: true
                Layout.fillHeight: true
                // TODO(profile-buttons): wire Apply / Revert to a real
                // profile path — `ProfileController.saveProfile(path)` /
                // `loadProfile(path)` need a default path resolution
                // (QStandardPaths::AppDataLocation/profile.json) plus a
                // file dialog for "Save as". Today these buttons toast
                // "not implemented yet" rather than calling non-existent
                // no-arg overloads (the previous code did the latter and
                // silently no-op'd at runtime).
                onApplyRequested: toast.show(qsTr("Save profile: not implemented yet"), "info")
                onRevertRequested: toast.show(qsTr("Revert profile: not implemented yet"), "info")
                onRestoreDefaultsRequested: toast.show(qsTr("Restore defaults: not implemented yet"), "info")
            }

            Inspector {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                visible: root.width >= 1100
                selectionLabel: editor.codename === ""
                    ? ""
                    : qsTr("Key %1").arg((editor.children.length > 0 ? 1 : 1))
            }
        }
    }

    Toast { id: toast }

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
