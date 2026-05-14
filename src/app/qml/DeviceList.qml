// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sidebar panel showing connected and previously-seen AJAZZ devices.
// Exposes `model` (alias to the internal ListView model) for the parent to
// supply a device list model.  Emits `deviceSelected(codename)` when the
// user clicks (or activates with Enter / Space) a device row.
//
// Each row is a DeviceRow component (QtQuick.Controls ItemDelegate) so it is
// keyboard-focusable, hover-highlighted and exposes Accessible roles.
//
// `pragma ComponentBehavior: Bound` makes IDs from outer components
// (`root`) statically resolvable inside the delegate, so qmllint can
// type-check `root.deviceSelected(...)` instead of warning unqualified.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Rectangle {
    id: root
    color: Theme.bgSidebar

    /// Emitted when the user activates a device row; carries the device codename.
    signal deviceSelected(string codename)

    /// Emitted when the user clicks the per-row "Sync time" ToolButton.
    /// Main.qml routes this to TimeSyncService.setSystemTimeOn(codename)
    /// (Plan 05-06 + 05-07). Bubbled up from each DeviceRow.
    signal syncTimeRequested(string codename)

    /// Map of `codename → "success" | "not_implemented" | "io_error" | "not_capable" | ""`.
    /// Set by Main.qml on TimeSyncService syncSucceeded / syncFailed and the
    /// autoSync result connection. Per-row glyph reads its own state from
    /// this map via syncGlyphState binding. D-02: auto-sync only writes
    /// here (no Toast); manual sync writes here AND fires a Toast.
    property var syncGlyphByCodename: ({})

    /// The device model. Set by the parent (Main.qml) to DeviceModel.
    /// We feed it to a Repeater inside a ColumnLayout (rather than a
    /// ListView) so the layout topology survives reflow under the
    /// HOTPLUG-02 contract: ALL registered codename rows are present
    /// at all times — disconnected ones surface an "Offline" badge
    /// (see DeviceRow.qml) but stay at their lex-sorted position.
    /// Phase 4 (D-03 + HOTPLUG-02/03/04) reverses the v1.0
    /// connected-only filter so selection focus + scroll position
    /// survive a disconnect/reconnect cycle automatically.
    property var model: null

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        clip: true
        // Hide the entire scroll surface only when there are NO
        // registered codenames at all (e.g. before bootstrap finishes
        // — the sidebar would otherwise look broken). Once any row
        // exists we always show the scroll surface; offline rows are
        // surfaced via the Offline badge instead of being hidden.
        visible: rows.children.length > 1 // Repeater itself counts as 1 child.

        ColumnLayout {
            id: rows
            width: scroll.availableWidth
            spacing: Theme.spacingXs

            Repeater {
                model: root.model

                delegate: DeviceRow {
                    // F-08/COD-019: required model roles instead of parent.parent.model.
                    // Names match DeviceModel::roleNames() exactly. To avoid the QML
                    // self-binding trap (`connected: connected` would self-reference
                    // the DeviceRow's `connected` property and resolve to `false`),
                    // the consumer-facing properties on DeviceRow are namespaced
                    // (deviceCodename / deviceConnected). See the note at the top
                    // of DeviceRow.qml.
                    // F-08/COD-019: required model roles instead of parent.parent.model.
                    // Names match DeviceModel::roleNames() exactly. Phase 5
                    // Plan 05-05 names the new role `deviceHasClock`; the
                    // DeviceRow's consumer property is `hasClockCapability`
                    // (renamed to dodge the self-binding trap — see the
                    // top-of-file note on DeviceRow.qml).
                    required property string model
                    required property string codename
                    required property int    family
                    required property bool   connected
                    required property bool   deviceHasClock

                    Layout.fillWidth: true
                    modelName: model
                    deviceCodename: codename
                    deviceConnected: connected
                    hasClockCapability: deviceHasClock
                    // Phase 8 DEVICES-02: maturity tier surfaced as tooltip.
                    // Bound from DeviceModel.MaturityRole (`maturity` role name).
                    // Same QML self-binding trap naming pattern as
                    // deviceConnected: connected (line 94 above).
                    deviceMaturity: maturity
                    // Per-row glyph state pulled from DeviceList's map (set
                    // by Main.qml on TimeSyncService signals — D-02).
                    syncGlyphState: root.syncGlyphByCodename[codename] || ""

                    // HOTPLUG-02: rows always visible — offline state
                    // surfaces via the Offline badge inside DeviceRow,
                    // not by hiding the row. This is what makes
                    // selection focus + scroll position survive a
                    // disconnect/reconnect cycle (HOTPLUG-03): the
                    // row index does not move.

                    onClicked: root.deviceSelected(codename)
                    onSyncTimeRequested: function(cn) { root.syncTimeRequested(cn); }
                }
            }
        }
    }

    // Onboarding hint shown when no devices are registered and not in
    // the collapsed-icon-only narrow layout (root.width < 200 covers the
    // 64 px collapsed mode that Main.qml falls into below 700 px window
    // width — drawing the title at that width clips badly).
    //
    // Pre-Phase 4 this also fired when devices were registered but none
    // were connected. Post-Phase 4 the offline-badge UX (HOTPLUG-02)
    // means the sidebar stays populated with offline rows, so the
    // empty-state only appears in the genuinely-empty case (no
    // backends registered yet).
    EmptyState {
        anchors.centerIn: parent
        anchors.margins: Theme.spacingLg
        width: parent.width - Theme.spacingLg * 2
        // Mirror the ScrollView visibility condition: hidden scroll
        // means zero registered rows, which is exactly when we want
        // the onboarding hint. Avoids the "scroll visible AND empty
        // state visible at the same time" double-display.
        visible: !scroll.visible && root.width >= 200
        title: qsTr("No devices yet")
        body: qsTr("Plug in an AJAZZ device — keyboard, Stream Dock, or mouse — and it will show up here automatically.")
    }
}
