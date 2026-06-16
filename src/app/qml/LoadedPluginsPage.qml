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
//   isSigned (bool), publisher (QString), trustLevel (QString),
//   platformStatus (QString — WINPLG chip: "native"/"wine"/"unsupported"/"").
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

    // #83: snapshot of installed plugins that can't run on this platform — native
    // Windows/macOS binaries with no Linux build (e.g. the official com.elgato.*
    // set). They install but surface no actions, so without this they'd vanish
    // silently; listed below with a clear status. Refreshed on load and whenever
    // the installed set changes so they appear/disappear without a restart.
    property var _unsupported: []
    function _refreshUnsupported() {
        root._unsupported = (typeof PluginCatalog !== "undefined" && PluginCatalog)
            ? PluginCatalog.installedUnsupportedPlugins() : [];
    }
    Component.onCompleted: root._refreshUnsupported()
    Connections {
        target: (typeof PluginCatalog !== "undefined") ? PluginCatalog : null
        function onInstalledCountChanged() { root._refreshUnsupported(); }
    }

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

        // NOTE: the .sdPlugin trust controls (the "Allow unsigned plugins"
        // toggle + per-plugin unsigned consent) live in PluginStore.qml, NOT
        // here. This page (LoadedPluginsPage) is driven by LoadedPluginsModel,
        // which (since the Phase-35 CR-01 fix, application.cpp:1120-1122) is fed
        // by the MERGED UnifiedPluginHost inventory — both .sdPlugin AND Python
        // plugins; .sdPlugin rows now carry the WINPLG platformStatus chip.
        // Phase 27 originally placed the .sdPlugin trust UX here by mistake
        // (PluginCatalog.allowPlugin no-ops on a Python plugin id); moved to
        // PluginStore where .sdPlugin plugins are actually installed.

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
                        // VERIF-01: objectName + readable label so scripts/ajazz-debug
                        // qml.get reaches this pre-existing chip headlessly (it had
                        // none before — grep -c objectName was 0).
                        objectName: "trustChip"
                        property string trustLabel: chipText.text
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
                            ? qsTr("This plugin has no signature (developer sideload).")
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

                    // ----- WINPLG-03 platform-status chip (chip-only) ---------
                    // Surfaces the Windows-plugin classification derived from
                    //   LoadedPluginsModel.platformStatus (mirror of
                    //   PluginInfo.winClass, Plan 01). Display-only: it reports the
                    //   verdict but grants no capability — a misclassified plugin
                    //   still cannot run a vendor-DLL (supportsCurrentPlatform gates
                    //   that in Plan 01; there is no in-process PE loader).
                    //   native      → "Runs natively" (positive/neutral)
                    //   wine         → "Requires Wine" (amber; launch DEFERRED)
                    //   unsupported  → "Unsupported on this OS" (error)
                    //   ""           → chip hidden (not a Windows-only plugin)
                    Rectangle {
                        id: platformStatusChip
                        objectName: "platformStatusChip"
                        // Readable label (VERIF-01): a headless qml.get on statusLabel
                        // returns the locked copy without hover.
                        property string statusLabel: row.platformStatus === "native"
                            ? qsTr("Runs natively")
                            : row.platformStatus === "wine"
                                ? qsTr("Requires Wine")
                                : row.platformStatus === "unsupported"
                                    ? qsTr("Unsupported on this OS")
                                    : ""
                        visible: row.platformStatus !== ""
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: platformChipText.implicitWidth + Theme.spacingMd * 2
                        radius: 12

                        // unsupported → error (red); wine → amber warning;
                        // native → neutral/positive (warning family kept subtle).
                        color: row.platformStatus === "unsupported"
                            ? Theme.chipBgError
                            : Theme.chipBgWarning
                        border.color: row.platformStatus === "unsupported"
                            ? Theme.chipBorderError
                            : Theme.chipBorderWarning
                        border.width: 1

                        Text {
                            id: platformChipText
                            anchors.centerIn: parent
                            text: platformStatusChip.statusLabel
                            color: row.platformStatus === "unsupported"
                                ? Theme.chipFgError
                                : Theme.chipFgWarning
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.DemiBold
                        }

                        ToolTip.visible: platformChipMouseArea.containsMouse
                        ToolTip.text: row.platformStatus === "native"
                            ? qsTr("This plugin uses the WS-only-IPC backend and "
                                + "runs natively on this OS.")
                            : row.platformStatus === "wine"
                                ? qsTr("This plugin ships a Windows vendor DLL; "
                                    + "running it requires Wine (launch support is "
                                    + "not yet implemented).")
                                : qsTr("This plugin ships a Windows vendor DLL and "
                                    + "cannot run on this OS (no Wine launcher).")

                        MouseArea {
                            id: platformChipMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }

                    // ----- Unsigned-consent chip (amber, display-only) --------
                    // Informational mirror of the unsigned trust state. Consent is
                    // granted through the existing PluginStore consent flow +
                    // QSettings (Plan 02), NOT through this chip — the QML harness
                    // gap means an interactive Switch's toggled() never fires, so
                    // this is a plain display chip (no Switch/CheckBox).
                    Rectangle {
                        id: unsignedConsentChip
                        objectName: "unsignedConsentChip"
                        property string consentLabel: qsTr("Unsigned — requires consent")
                        visible: row.trustLevel === "unsigned"
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: consentChipText.implicitWidth + Theme.spacingMd * 2
                        radius: 12

                        color: Theme.chipBgWarning
                        border.color: Theme.chipBorderWarning
                        border.width: 1

                        Text {
                            id: consentChipText
                            anchors.centerIn: parent
                            text: unsignedConsentChip.consentLabel
                            color: Theme.chipFgWarning
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.DemiBold
                        }

                        ToolTip.visible: consentChipMouseArea.containsMouse
                        ToolTip.text: qsTr("This plugin is unsigned. It loads only "
                            + "after you grant consent in the Plugin Store; consent "
                            + "is remembered across restarts.")

                        MouseArea {
                            id: consentChipMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }

                    // No per-plugin "Allow" action here: this row is a Python
                    // OOP-host plugin (SEC-003), not a .sdPlugin. The .sdPlugin
                    // unsigned-consent UX lives in PluginStore.qml (the global
                    // "Allow unsigned plugins" toggle + the install-from-file
                    // unsigned confirm dialog). Wiring PluginCatalog.allowPlugin
                    // to a Python plugin id was a no-op (see note at top).
                }
            }
        }

        // #83: installed-but-unrunnable plugins (e.g. the official Elgato
        // com.elgato.* native Windows/macOS binaries). Surfaced with a clear
        // status chip instead of being hidden entirely from the user.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            visible: root._unsupported.length > 0

            Text {
                text: qsTr("Installed — not runnable on this platform")
                color: Theme.fgMuted
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
            }

            Repeater {
                model: root._unsupported
                delegate: Rectangle {
                    id: uRow
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 64
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
                            Text {
                                text: (uRow.modelData.name && uRow.modelData.name !== "")
                                    ? uRow.modelData.name : uRow.modelData.id
                                color: Theme.fgPrimary
                                font.pixelSize: Theme.fontMd
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: {
                                    let bits = [];
                                    if (uRow.modelData.author)
                                        bits.push(uRow.modelData.author);
                                    bits.push(uRow.modelData.detail);
                                    return bits.join(" · ");
                                }
                                color: Theme.fgMuted
                                font.pixelSize: Theme.fontSm
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // Clear "can't run here" status chip (#83).
                        Rectangle {
                            objectName: "unsupportedStatusChip"
                            Layout.preferredHeight: 24
                            Layout.preferredWidth: uChipText.implicitWidth + Theme.spacingMd * 2
                            radius: 12
                            color: Theme.chipBgError
                            border.color: Theme.chipBorderError
                            border.width: 1
                            Text {
                                id: uChipText
                                anchors.centerIn: parent
                                text: uRow.modelData.reason === "noCodePath"
                                    ? ((uRow.modelData.platforms && uRow.modelData.platforms !== "")
                                        ? qsTr("%1 only").arg(uRow.modelData.platforms)
                                        : qsTr("Other OS only"))
                                    : qsTr("Unsupported here")
                                color: Theme.chipFgError
                                font.pixelSize: Theme.fontXs
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
        }
    }
}
