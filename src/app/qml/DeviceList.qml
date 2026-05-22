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
    /// ListView) so the layout topology survives reflow when rows
    /// appear/disappear (lex-sorted insertion preserved).
    ///
    /// **Connected-only filter (restored 2026-05-15, reverting Phase 4
    /// HOTPLUG-02 mid-session offline-badge UX).** Each delegate sets
    /// `visible: connected`, hiding rows for catalogued-but-not-currently-
    /// connected devices. Rationale: when the catalogue grows to 13+ SKUs
    /// and only 3-4 are physically plugged in, the offline-badged rows
    /// dominate the sidebar visually and read as "the app is showing me
    /// devices I don't have". Matches the original v1.0 spec at commit
    /// d377d80 + e889d28, requested 2026-05-13.
    ///
    /// **Trade-off accepted**: HOTPLUG-02 selection-focus retention on
    /// mid-session unplug no longer applies — when a device is unplugged
    /// while the app runs, the row vanishes immediately (instead of
    /// going to offline-badged state and keeping focus). The user
    /// explicitly chose this in 2026-05-15. If selection retention
    /// becomes needed again, the alternative is a "session-seen" filter
    /// (visible: connected || wasConnectedThisSession) or a UI toggle.
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
                    // 2026-05-18 P3.d: gates the BatteryIndicator chip mounted
                    // inside DeviceRow. Role name (`deviceHasBattery`) mirrors
                    // DeviceModel::roleNames(); consumer property uses the
                    // de-collided name `hasBatteryCapability` to dodge the
                    // QML self-binding trap (same pattern as deviceHasClock /
                    // hasClockCapability above).
                    required property bool   deviceHasBattery

                    Layout.fillWidth: true
                    modelName: model
                    deviceCodename: codename
                    deviceConnected: connected
                    hasClockCapability: deviceHasClock
                    hasBatteryCapability: deviceHasBattery
                    // Phase 8 DEVICES-02: maturity tier surfaced as tooltip.
                    // Bound from DeviceModel.MaturityRole (`maturity` role name).
                    // Same QML self-binding trap naming pattern as
                    // deviceConnected: connected (line 94 above).
                    deviceMaturity: maturity
                    // Per-row glyph state pulled from DeviceList's map (set
                    // by Main.qml on TimeSyncService signals — D-02).
                    syncGlyphState: root.syncGlyphByCodename[codename] || ""

                    // Connected-only sidebar filter (restored 2026-05-15
                    // per user request, matching d377d80 spec). Hides
                    // delegates for catalogued-but-not-currently-connected
                    // devices. `visible: false` in a ColumnLayout child
                    // also removes the item from layout sizing, so the
                    // row collapses to zero height without needing an
                    // explicit `Layout.preferredHeight` override.
                    //
                    // Trade-off vs HOTPLUG-02 mid-session unplug: the row
                    // disappears immediately when a device is unplugged
                    // while the app runs (instead of going offline-badged).
                    // Selection focus is lost on the unplugged codename;
                    // this is the explicit user trade-off captured at
                    // the top-of-file `model` property docstring.
                    visible: connected

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
