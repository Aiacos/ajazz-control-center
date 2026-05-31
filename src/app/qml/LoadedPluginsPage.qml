// SPDX-License-Identifier: GPL-3.0-or-later
//
// LoadedPluginsPage.qml — runtime view of plugins the host has imported
// in this session. Sibling of PluginStore.qml (which lists plugins
// available to install); this page is about the LIVE inventory.
//
// SEC-003 #51 surface contract — the trust chip (Plan 27-04 extended):
//
//   * `trusted`     → no chip (clean look; absence of warning is the
//                     positive signal — see U2 design discussion).
//   * `self-signed` → amber "self-signed" chip; no allow action.
//   * `unsigned`    → red "unsigned" chip + "Allow this plugin" action.
//   * `tampered`    → danger "tampered / invalid signature" chip;
//                     NO allow action (CR-01: never consentable from UI).
//
// Roles consumed (from LoadedPluginsModel):
//   pluginId, name, version, authors, permissions (QStringList),
//   isSigned (bool), publisher (QString), trustLevel (QString).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Page {
    id: root
    title: qsTr("Loaded plugins")

    // Theme has no `materialTheme` property — that lives on Main.qml's root.
    // Material attached props don't cross the Drawer(Popup)->child boundary
    // (CLAUDE.md gotcha), so this Page must re-assert Material.theme itself.
    // Mirror Main.qml's `materialTheme` expression so the drawer chrome stays
    // dark with ThemeService (avoids the invisible black-text-on-dark bug).
    Material.theme: ThemeService.effectiveMode === "light" ? Material.Light : Material.Dark
    Material.accent: Theme.accent

    // Frame the page with the standard column layout used by SettingsPage
    // and PluginStore.qml so the visual rhythm is consistent.
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // Header: title + secondary line that summarises trust state.
        // The secondary line uses LoadedPlugins.untrustedCount so the
        // user sees at a glance whether anything needs review.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            Text {
                text: root.title
                color: Theme.fgPrimary
                font.pixelSize: Theme.fontXl
                font.weight: Font.DemiBold
            }
            Text {
                text: LoadedPlugins.count === 0
                    ? qsTr("No plugins loaded.")
                    : LoadedPlugins.untrustedCount === 0
                        ? qsTr("%1 loaded — all trusted.").arg(LoadedPlugins.count)
                        : qsTr("%1 loaded · %2 need review.").arg(LoadedPlugins.count)
                                                              .arg(LoadedPlugins.untrustedCount)
                color: Theme.fgMuted
                font.pixelSize: Theme.fontMd
            }
        }

        // Allow unsigned plugins toggle (Plan 27-04 / PLUGIN-16).
        // Bound two-way to PluginCatalog.allowUnsignedPlugins.
        // Mirrors the Online catalog toggle pattern in PluginStore.qml.
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
            ToolTip.text: qsTr("When enabled, unsigned (developer sideload) plugins may be "
                + "installed without per-plugin confirmation. Tampered plugins are "
                + "always blocked regardless of this setting.")
            ToolTip.visible: hovered
            ToolTip.delay: 400
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Allow unsigned plugins")
        }

        // Empty state — same affordance as PluginStore when its catalogue
        // is empty; keeps the drawer non-blank if the host hasn't been
        // wired up yet (LoadedPluginsModel ships empty by default).
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: LoadedPlugins.count === 0
            title: qsTr("No plugins loaded yet")
            body: qsTr("Plugins will appear here once the host has imported them.")
        }

        // Live list. Each row is a card with name + version on top and a
        // metadata line below (authors · publisher chip when not trusted).
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: LoadedPlugins.count > 0
            model: LoadedPlugins
            spacing: Theme.spacingSm
            clip: true

            delegate: Rectangle {
                id: row

                // Required role declarations — qmllint resolves them
                // statically to LoadedPluginsModel::Roles. Only the roles
                // the delegate body actually reads are declared `required`
                // so qmllint catches a stale role rename and dead bindings
                // don't accumulate. `isSigned` and `publisher` are still
                // exposed by the model for future delegates (e.g. an
                // expanded "details" panel) but the chip needs only
                // `trustLevel` since the model already collapses the
                // (signed_, publisher) pair into the enum string.
                required property string pluginId
                required property string name
                required property string version
                required property string authors
                required property string trustLevel

                width: ListView.view.width
                height: 64
                radius: Theme.radiusMd
                color: Theme.bgRow
                border.color: Theme.borderSubtle
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingMd

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            spacing: Theme.spacingSm
                            Text {
                                text: row.name
                                color: Theme.fgPrimary
                                font.pixelSize: Theme.fontMd
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                text: row.version
                                color: Theme.fgMuted
                                font.pixelSize: Theme.fontSm
                            }
                        }
                        Text {
                            text: row.authors === ""
                                ? row.pluginId
                                : qsTr("%1 · %2").arg(row.authors).arg(row.pluginId)
                            color: Theme.fgMuted
                            font.pixelSize: Theme.fontSm
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    // ----- U2 trust chip (Plan 27-04 extended) -----------
                    // Handles four states:
                    //   trusted     → chip invisible (no warning = positive signal)
                    //   self-signed → amber chip, no action
                    //   unsigned    → red chip + "Allow this plugin" action
                    //   tampered    → danger chip, NO allow action (CR-01)
                    Rectangle {
                        id: trustChip
                        visible: row.trustLevel !== "trusted"
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: chipText.implicitWidth + Theme.spacingMd * 2
                        radius: 12

                        // Color by trust level: tampered and unsigned both
                        // use error (red) to signal danger; self-signed uses
                        // warning (amber) — a distinct but less severe state.
                        color: (row.trustLevel === "unsigned" || row.trustLevel === "tampered")
                            ? Theme.chipBgError
                            : Theme.chipBgWarning
                        border.color: (row.trustLevel === "unsigned" || row.trustLevel === "tampered")
                            ? Theme.chipBorderError
                            : Theme.chipBorderWarning
                        border.width: 1

                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            // Distinct labels: tampered is a separate security state,
                            // not just another "unsigned" variant.
                            text: row.trustLevel === "unsigned"
                                ? qsTr("unsigned")
                                : row.trustLevel === "tampered"
                                    ? qsTr("tampered")
                                    : qsTr("self-signed")
                            color: (row.trustLevel === "unsigned" || row.trustLevel === "tampered")
                                ? Theme.chipFgError
                                : Theme.chipFgWarning
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.DemiBold
                        }

                        // Distinct tooltip per trust level. The old tooltip conflated
                        // "unsigned or tampered" — now each state is described clearly.
                        ToolTip.visible: chipMouseArea.containsMouse
                        ToolTip.text: row.trustLevel === "unsigned"
                            ? qsTr("This plugin has no signature (developer sideload). "
                                + "Use 'Allow this plugin' to consent to running it.")
                            : row.trustLevel === "tampered"
                                ? qsTr("This plugin's signature is present but "
                                    + "cryptographically invalid — it may be tampered. "
                                    + "It cannot be allowed from the UI.")
                                : qsTr("This plugin's manifest is signed but the publisher "
                                    + "key is not in the bundled trust roots.")

                        MouseArea {
                            id: chipMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }

                    // ----- "Allow this plugin" action (unsigned only) ----
                    // Present ONLY on unsigned rows (trustLevel === "unsigned").
                    // ABSENT on tampered rows — CR-01: no UI consent path for
                    // Ed25519-invalid packages. Do NOT change to "disabled";
                    // the action must be absent, not merely greyed out.
                    Button {
                        id: allowPluginButton
                        objectName: "allowPluginButton"
                        visible: row.trustLevel === "unsigned"
                        text: qsTr("Allow")
                        Layout.preferredHeight: 28
                        font.pixelSize: Theme.fontXs
                        ToolTip.text: qsTr("Record consent to run this unsigned plugin.")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        onClicked: {
                            if (PluginCatalog) {
                                PluginCatalog.allowPlugin(row.pluginId);
                            }
                        }
                    }
                }
            }
        }
    }
}
