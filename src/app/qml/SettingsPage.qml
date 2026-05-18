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
