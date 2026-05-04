// SPDX-License-Identifier: GPL-3.0-or-later
//
// LoadedPluginsPage.qml — runtime view of plugins the host has imported
// in this session. Sibling of PluginStore.qml (which lists plugins
// available to install); this page is about the LIVE inventory.
//
// SEC-003 #51 surface contract — the trust chip:
//
//   * `trusted`     → no chip (clean look; absence of warning is the
//                     positive signal — see U2 design discussion).
//   * `self-signed` → amber "self-signed" chip; the manifest verifies
//                     but its key is not in the bundled trust roots.
//   * `unsigned`    → red "unsigned" chip; the manifest is missing
//                     OR the signature failed verification.
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

    Material.theme: Theme.materialTheme
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
                color: Theme.fgSecondary
                font.pixelSize: Theme.fontMd
            }
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
                                color: Theme.fgSecondary
                                font.pixelSize: Theme.fontSm
                            }
                        }
                        Text {
                            text: row.authors === ""
                                ? row.pluginId
                                : qsTr("%1 · %2").arg(row.authors).arg(row.pluginId)
                            color: Theme.fgSecondary
                            font.pixelSize: Theme.fontSm
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    // ----- U2 trust chip ---------------------------------
                    // Visible only when trustLevel != "trusted" — silence
                    // is the positive signal, per the design discussion.
                    Rectangle {
                        id: trustChip
                        visible: row.trustLevel !== "trusted"
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: chipText.implicitWidth + Theme.spacingMd * 2
                        radius: 12

                        // Amber for self-signed (verifies but unknown key),
                        // red for unsigned / tampered (the dangerous case).
                        color: row.trustLevel === "unsigned"
                            ? "#7f1d1d"  // dark red 800
                            : "#78350f"  // amber 800
                        border.color: row.trustLevel === "unsigned"
                            ? "#ef4444"  // red 500
                            : "#f59e0b"  // amber 500
                        border.width: 1

                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: row.trustLevel === "unsigned"
                                ? qsTr("unsigned")
                                : qsTr("self-signed")
                            color: row.trustLevel === "unsigned"
                                ? "#fecaca"  // red 200
                                : "#fde68a"  // amber 200
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.DemiBold
                        }

                        ToolTip.visible: chipMouseArea.containsMouse
                        ToolTip.text: row.trustLevel === "unsigned"
                            ? qsTr("This plugin's manifest is unsigned or tampered. Run only if you trust the source.")
                            : qsTr("This plugin's manifest is signed but the publisher key is not in the bundled trust roots.")

                        MouseArea {
                            id: chipMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                }
            }
        }
    }
}
