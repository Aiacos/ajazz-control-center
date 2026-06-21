// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsPage.qml — application settings page (theme, tray behaviour,
// autostart). Named *Page* (not just *Settings*) because `QtCore.Settings`
// is the QSettings binding type — using the same name would cause
// unqualified-access ambiguity in any file that imports both modules
// (e.g. PluginStore.qml, which uses `QtCore.Settings` for tab persistence).
//
// All state lives in the C++ services exposed as QML context properties:
//   - themeService.mode (QString: "auto" | "light" | "dark")
//   - autostart.launchOnLogin (bool)
//   - autostart.startMinimised (bool)
//   - tray.startMinimized (bool)
//
// The page is a Material-styled scrollable column of switches and a single
// theme-mode picker. It is intentionally minimal — Settings is a leaf page,
// not a full preferences modal, so the component count stays small.
//
// `pragma ComponentBehavior: Bound` makes the outer `themeGroup` ButtonGroup
// id resolvable inside the Repeater RadioButton delegate so qmllint can
// type-check the ButtonGroup.group binding.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter

Page {
    id: root
    title: qsTr("Settings")
    background: Rectangle { color: Theme.bgBase }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        contentWidth: availableWidth

        ColumnLayout {
            width: root.width - Theme.spacingLg * 2
            spacing: Theme.spacingLg

            // --------------------------------------------------------------
            // Appearance
            // --------------------------------------------------------------
            Label {
                text: qsTr("Appearance")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleMedium.pixelSize
                font.weight: Theme.typeTitleMedium.weight
                font.letterSpacing: Theme.typeTitleMedium.letterSpacing
                Accessible.role: Accessible.Heading
            }

            Frame {
                Layout.fillWidth: true
                background: Rectangle {
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusMd
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingMd

                    Label {
                        text: qsTr("Theme mode")
                        color: Theme.fgPrimary
                        font.pixelSize: Theme.typeBodyMedium.pixelSize
                        font.weight: Theme.typeBodyMedium.weight
                        font.letterSpacing: Theme.typeBodyMedium.letterSpacing
                    }
                    Label {
                        text: qsTr("Choose between Auto (follow system), Light, or Dark.")
                        color: Theme.fgMuted
                        font.pixelSize: Theme.typeBodySmall.pixelSize
                        font.weight: Theme.typeBodySmall.weight
                        font.letterSpacing: Theme.typeBodySmall.letterSpacing
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: Theme.spacingMd

                        ButtonGroup { id: themeGroup }

                        Repeater {
                            model: [
                                { id: "auto",  label: qsTr("Auto") },
                                { id: "light", label: qsTr("Light") },
                                { id: "dark",  label: qsTr("Dark") }
                            ]
                            delegate: RadioButton {
                                required property var modelData
                                text: modelData.label
                                ButtonGroup.group: themeGroup
                                checked: ThemeService.mode === modelData.id
                                onToggled: {
                                    if (checked) {
                                        ThemeService.mode = modelData.id;
                                    }
                                }
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: text
                            }
                        }
                    }

                    // Accent colour (Delta F): user-selectable; the choice
                    // re-tints the whole palette via Theme.accent. "" = AJAZZ brand.
                    Label {
                        text: qsTr("Accent colour")
                        color: Theme.fgPrimary
                        font.pixelSize: Theme.typeBodyMedium.pixelSize
                        font.weight: Theme.typeBodyMedium.weight
                    }
                    Label {
                        text: qsTr("Tint the app. Default follows AJAZZ branding.")
                        color: Theme.fgMuted
                        font.pixelSize: Theme.typeBodySmall.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        objectName: "accentSwatchRow"
                        spacing: Theme.spacingMd
                        Repeater {
                            model: [
                                { id: "",        sw: "#41CD52", label: qsTr("Brand") },
                                { id: "#204cfe", sw: "#204cfe", label: qsTr("Elgato blue") },
                                { id: "#e1342b", sw: "#e1342b", label: qsTr("Red") },
                                { id: "#a855f7", sw: "#a855f7", label: qsTr("Purple") }
                            ]
                            delegate: AbstractButton {
                                required property var modelData
                                objectName: "accentSwatch_" + (modelData.id === "" ? "brand" : modelData.id)
                                implicitWidth: 32
                                implicitHeight: 32
                                ToolTip.visible: hovered
                                ToolTip.text: modelData.label
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.label
                                onClicked: ThemeService.accentHex = modelData.id
                                background: Rectangle {
                                    radius: width / 2
                                    color: modelData.id === "" ? Branding.accent : modelData.sw
                                    // Ring the active choice.
                                    border.width: (ThemeService.accentHex === modelData.id) ? 3 : 0
                                    border.color: Theme.fgPrimary
                                }
                            }
                        }
                    }
                }
            }

            // --------------------------------------------------------------
            // Startup behaviour
            // --------------------------------------------------------------
            Label {
                text: qsTr("Startup")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleMedium.pixelSize
                font.weight: Theme.typeTitleMedium.weight
                font.letterSpacing: Theme.typeTitleMedium.letterSpacing
                Accessible.role: Accessible.Heading
            }

            Frame {
                Layout.fillWidth: true
                background: Rectangle {
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusMd
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    SwitchDelegate {
                        Layout.fillWidth: true
                        text: qsTr("Launch on login")
                        checked: Autostart.launchOnLogin
                        onToggled: Autostart.launchOnLogin = checked
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.description: qsTr("When enabled, the app starts automatically when you log in to your computer.")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderSubtle
                    }

                    SwitchDelegate {
                        Layout.fillWidth: true
                        text: qsTr("Start minimised to tray")
                        checked: Autostart.startMinimised
                        enabled: Tray.trayAvailable
                        onToggled: {
                            Autostart.startMinimised = checked;
                            Tray.startMinimized = checked;
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.description: qsTr("When enabled, the application window stays hidden at startup; access it from the system tray.")
                    }
                }
            }

            // --------------------------------------------------------------
            // Per-app profiles (Phase 34-05 / APROF-03)
            //
            // Maps an application name to a profile so the active profile
            // auto-switches when that app takes focus (the watcher backend +
            // applicationHints matcher land in Plans 03/04). The
            // capability-warning chip adjacent to the heading is visible ONLY
            // when the active foreground-window watcher reports no capability
            // (degraded Wayland/GNOME desktop) -- amber/warning family, never
            // error red: per-app switching is unavailable but manual switching
            // still works (graceful degradation, T-34-05-02/03).
            //
            // Mapping writes go through ProfileController.addAppProfileMapping /
            // removeAppProfileMapping (Profile::applicationHints writer); the
            // app-name token is length-bounded + treated purely as a match key
            // (T-34-05-01). All controls are objectName-addressable (VERIF-01)
            // and use Button/SecondaryButton, NOT Switch (qml harness gap).
            // --------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd

                Label {
                    text: qsTr("Per-app profiles")
                    color: Theme.fgPrimary
                    font.pixelSize: Theme.typeTitleMedium.pixelSize
                    font.weight: Theme.typeTitleMedium.weight
                    font.letterSpacing: Theme.typeTitleMedium.letterSpacing
                    Accessible.role: Accessible.Heading
                }

                Item { Layout.fillWidth: true }

                // Capability-warning chip (mirrors LoadedPluginsPage trust-chip,
                // amber/warning family). Visible only when the foreground-window
                // capability is absent. `warningText` is exposed addressably so a
                // headless qml.get asserts non-empty detail copy without hover.
                Rectangle {
                    id: waylandCapabilityWarningChip
                    objectName: "waylandCapabilityWarningChip"

                    // Visible-on-ABSENT: no chip = positive signal (capability ok).
                    visible: !ProfileController.foregroundCapabilityAvailable

                    // Addressable warning detail (success criterion 2): a headless
                    // qml.get on this chip must return non-empty warningText on the
                    // capability-absent path.
                    property string warningText: ProfileController.foregroundCapabilityWarning

                    Layout.preferredHeight: 24
                    Layout.preferredWidth: capabilityChipText.implicitWidth + Theme.spacingMd * 2
                    radius: 12 // pill (height/2); the one sanctioned non-token radius

                    color: Theme.chipBgWarning
                    border.color: Theme.chipBorderWarning
                    border.width: 1

                    Text {
                        id: capabilityChipText
                        anchors.centerIn: parent
                        text: qsTr("Limited on this desktop")
                        color: Theme.chipFgWarning
                        font.pixelSize: Theme.fontXs
                        font.weight: Font.DemiBold
                    }

                    ToolTip.visible: capabilityChipMouseArea.containsMouse
                    ToolTip.text: waylandCapabilityWarningChip.warningText

                    MouseArea {
                        id: capabilityChipMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                background: Rectangle {
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusMd
                }

                ColumnLayout {
                    id: appProfilesColumn
                    anchors.fill: parent
                    spacing: Theme.spacingMd

                    // Backing models, re-queried on profilesChanged.
                    property var mappings: ProfileController.appProfileMappings()
                    property var profiles: ProfileController.profilesForDevice("")

                    function refresh() {
                        appProfilesColumn.mappings = ProfileController.appProfileMappings();
                        appProfilesColumn.profiles = ProfileController.profilesForDevice("");
                        // Keep the selector index valid after a refresh.
                        if (appProfileProfileSelector.currentIndex >= appProfilesColumn.profiles.length)
                            appProfileProfileSelector.currentIndex = 0;
                    }

                    Connections {
                        target: ProfileController
                        function onProfilesChanged() { appProfilesColumn.refresh(); }
                        function onProfileChanged() { appProfilesColumn.refresh(); }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Automatically switch the active profile when you focus an application. "
                            + "Match an application name to a profile below.")
                        color: Theme.fgMuted
                        font.pixelSize: Theme.typeBodySmall.pixelSize
                        font.weight: Theme.typeBodySmall.weight
                        font.letterSpacing: Theme.typeBodySmall.letterSpacing
                        wrapMode: Text.WordWrap
                    }

                    // Error state: nothing to map to.
                    Label {
                        objectName: "appProfileNoProfilesLabel"
                        Layout.fillWidth: true
                        visible: appProfilesColumn.profiles.length === 0
                        text: qsTr("No profiles available. Create a profile first, then return here "
                            + "to map it to an application.")
                        color: Theme.errorAccent
                        font.pixelSize: Theme.typeBodySmall.pixelSize
                        wrapMode: Text.WordWrap
                    }

                    // Empty state: profiles exist but no mappings yet.
                    EmptyState {
                        Layout.fillWidth: true
                        visible: appProfilesColumn.profiles.length > 0
                            && appProfilesColumn.mappings.length === 0
                        title: qsTr("No app mappings yet")
                        body: qsTr("Add a mapping to switch profiles automatically when an application "
                            + "takes focus. Focusing an unmapped application leaves your current "
                            + "profile active.")
                    }

                    // Existing mapping rows.
                    Repeater {
                        model: appProfilesColumn.mappings
                        delegate: RowLayout {
                            id: mappingRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            Label {
                                text: mappingRow.modelData.appName
                                color: Theme.fgPrimary
                                font.pixelSize: Theme.typeTitleSmall.pixelSize
                                font.weight: Theme.typeTitleSmall.weight
                                Layout.preferredWidth: 160
                                elide: Text.ElideRight
                            }
                            Label {
                                text: qsTr("-> %1").arg(mappingRow.modelData.profileName)
                                color: Theme.fgMuted
                                font.pixelSize: Theme.typeBodyMedium.pixelSize
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            SecondaryButton {
                                objectName: "removeAppProfileMappingButton"
                                text: qsTr("Remove")
                                // Destructive affordance -- styled error red.
                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.errorAccent
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Remove mapping for %1").arg(mappingRow.modelData.appName)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Remove this app mapping? Focusing this application "
                                    + "will no longer switch profiles; your current profile stays active.")
                                onClicked: {
                                    ProfileController.removeAppProfileMapping(
                                        mappingRow.modelData.profileId, mappingRow.modelData.appName);
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderSubtle
                        visible: appProfilesColumn.profiles.length > 0
                    }

                    // Add-mapping row.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: appProfilesColumn.profiles.length > 0
                        spacing: Theme.spacingMd

                        TextField {
                            id: appProfileAppNameField
                            objectName: "appProfileAppNameField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.minTouchTarget
                            placeholderText: qsTr("Application name")
                            maximumLength: 256 // mirrors ProfileController kMaxAppNameLength (V5)
                            color: Theme.fgPrimary
                            Material.accent: Theme.accent
                            Accessible.role: Accessible.EditableText
                            Accessible.name: qsTr("Application name to map")
                        }

                        ComboBox {
                            id: appProfileProfileSelector
                            objectName: "appProfileProfileSelector"
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: Theme.minTouchTarget
                            model: appProfilesColumn.profiles
                            textRole: "name"
                            valueRole: "id"
                            Material.accent: Theme.accent
                            Accessible.role: Accessible.ComboBox
                            Accessible.name: qsTr("Profile to map to")
                        }

                        PrimaryButton {
                            objectName: "addAppProfileMappingButton"
                            text: qsTr("Add mapping")
                            enabled: appProfileAppNameField.text.trim() !== ""
                                && appProfileProfileSelector.currentValue !== undefined
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            onClicked: {
                                var ok = ProfileController.addAppProfileMapping(
                                    appProfileProfileSelector.currentValue,
                                    appProfileAppNameField.text);
                                if (ok)
                                    appProfileAppNameField.clear();
                            }
                        }
                    }
                }
            }

            // --------------------------------------------------------------
            // Time sync (Phase 5 Plan 05-06 + 05-07)
            //
            // Auto-sync toggles whether arriving devices that advertise
            // Capability::Clock get their RTC pushed to the host system
            // time. As of v1.1 no AJAZZ device firmware exposes a host-
            // settable RTC over HID — the toggle is honestly scaffolded so
            // the day a wire format lands, only the backend stub changes.
            // The hint text below makes the NotImplemented status visible
            // (Pitfall 12 / Pitfall 13 honest UX).
            // --------------------------------------------------------------
            Label {
                text: qsTr("Time sync")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleMedium.pixelSize
                font.weight: Theme.typeTitleMedium.weight
                font.letterSpacing: Theme.typeTitleMedium.letterSpacing
                Accessible.role: Accessible.Heading
            }

            Frame {
                Layout.fillWidth: true
                background: Rectangle {
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusMd
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingSm

                    SwitchDelegate {
                        Layout.fillWidth: true
                        text: qsTr("Auto-sync time on device connect")
                        checked: TimeSyncService.autoSync
                        onToggled: TimeSyncService.autoSync = checked
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.description: qsTr("When a capable device connects, set its clock to the system time after a short debounce.")
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingMd
                        Layout.rightMargin: Theme.spacingMd
                        Layout.bottomMargin: Theme.spacingSm
                        text: qsTr("Note: Currently no AJAZZ device firmware supports host clock writes — the toggle is scaffolded so the wire format can drop in without UI churn.")
                        color: Theme.fgMuted
                        font.pixelSize: Theme.typeBodySmall.pixelSize
                        font.weight: Theme.typeBodySmall.weight
                        font.letterSpacing: Theme.typeBodySmall.letterSpacing
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // --------------------------------------------------------------
            // About / Updates (2026-05-18, docs/architecture/APP-AUTO-UPDATE.md)
            //
            // Notify-only GitHub-Releases-driven update checker. The toggles
            // bind to AppUpdate (QML singleton, AppUpdateService). The "Check
            // now" button is disabled while a request is in flight so the
            // user can't queue duplicates. The Label below the controls is
            // ASCII-only and surfaces the version + platform label so the
            // user can spot a "Source build" / "Flatpak" / "Windows MSI"
            // mismatch at a glance.
            // --------------------------------------------------------------
            Label {
                text: qsTr("About and updates")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleMedium.pixelSize
                font.weight: Theme.typeTitleMedium.weight
                font.letterSpacing: Theme.typeTitleMedium.letterSpacing
                Accessible.role: Accessible.Heading
            }

            Frame {
                Layout.fillWidth: true
                background: Rectangle {
                    color: Theme.tile
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radiusMd
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    SwitchDelegate {
                        Layout.fillWidth: true
                        text: qsTr("Check for updates on launch")
                        checked: AppUpdate.autoCheckEnabled
                        onToggled: AppUpdate.autoCheckEnabled = checked
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderSubtle
                    }

                    SwitchDelegate {
                        Layout.fillWidth: true
                        text: qsTr("Include pre-release / nightly builds")
                        checked: AppUpdate.includeNightly
                        onToggled: AppUpdate.includeNightly = checked
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderSubtle
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingMd
                        Layout.rightMargin: Theme.spacingMd
                        Layout.topMargin: Theme.spacingSm
                        Layout.bottomMargin: Theme.spacingSm
                        spacing: Theme.spacingMd

                        Button {
                            text: qsTr("Check now")
                            enabled: AppUpdate.status !== AppUpdate.Checking
                                  && AppUpdate.status !== AppUpdate.Disabled
                            onClicked: AppUpdate.checkNow()
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingMd
                        Layout.rightMargin: Theme.spacingMd
                        Layout.bottomMargin: Theme.spacingSm
                        text: qsTr("Version %1 - %2").arg(AppUpdate.currentVersion)
                                                     .arg(AppUpdate.platformLabel)
                        color: Theme.fgMuted
                        font.pixelSize: Theme.typeBodySmall.pixelSize
                        font.weight: Theme.typeBodySmall.weight
                        font.letterSpacing: Theme.typeBodySmall.letterSpacing
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // --------------------------------------------------------------
            // About / version footer (read-only)
            // --------------------------------------------------------------
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingLg
                text: Branding.productName
                color: Theme.fgMuted
                font.pixelSize: Theme.typeBodySmall.pixelSize
                font.weight: Theme.typeBodySmall.weight
                font.letterSpacing: Theme.typeBodySmall.letterSpacing
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
