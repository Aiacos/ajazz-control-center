// SPDX-License-Identifier: GPL-3.0-or-later
//
// BatteryIndicator.qml — small chip-style battery state-of-charge widget.
//
// Sits in DeviceRow's right-side stack (next to the offline / sync glyphs)
// for any row whose codename belongs to an IBatteryCapable device. The
// stacked rectangle + text emulates a battery glyph in a way that survives
// Qt's icon-name resolution being a no-op on Windows, where the
// theme-icon fallback path would render a blank tile.
//
// Wire-up:
//   * Bind `codename` to the row's codename.
//   * Mount only when `DeviceModel.HasBatteryRole` (role `deviceHasBattery`)
//     is true AND the row is connected — the parent decides via the `visible`
//     binding. Inside, we also self-hide when `state === "unavailable"` so a
//     transient I/O failure (BatteryUnavailable signal) makes the chip
//     vanish instead of showing a stale stale percent.
//
// Polling policy (per task spec): BatteryService's 15-s QTimer is enabled by
// default (see battery_service.cpp) and short-circuits to no-op when no
// IBatteryCapable codename is currently registered — so leaving it always-on
// is the simpler path AND the auto-enable path. The indicator subscribes to
// BatteryService::batteryQueried / batteryUnavailable via a `Connections`
// block, filters by its own codename, and seeds initial state from
// BatteryService.lastKnownPercent(codename) on Component.onCompleted so the
// first 15-s tick isn't visible as a stale "--%" placeholder.
//
// Theme tokens only — no hardcoded colours. The low-charge tint at <=20 %
// uses `Theme.warningAccent` (same token RgbPicker / plugin-store badges
// use for "needs attention but not destructive"), which keeps polarity
// correct in both light and dark themes.
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter

Item {
    id: root

    /// Device codename this indicator tracks. Set by the parent row.
    property string codename: ""

    /// Last known battery charge level in percent (0..100), or -1 when
    /// unknown (no reading yet, or last query returned unavailable).
    /// Internal state — the parent binds visibility, not this property.
    property int percent: -1

    /// Whether we've ever observed an unavailable result on this codename
    /// since this indicator was constructed. Used to collapse the chip
    /// the first time BatteryService reports the device cannot answer
    /// (e.g. backend is not IBatteryCapable, or the HID I/O failed).
    property bool unavailable: false

    // The chip is visually empty when we have no reading and no signal yet —
    // collapse to zero width so it does not take layout space until we know
    // the device can answer. Once we *do* know (either a successful percent
    // OR an explicit unavailable signal), we keep the chip's width stable
    // so the row layout does not jitter.
    visible: percent >= 0 && !unavailable
    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight

    // Accessibility — the percent gets read by the screen reader as
    // "Battery 85 percent on AK980 PRO" via the row's own context. We
    // keep the description compact because the row's own Accessible.name
    // already carries the model name.
    Accessible.role: Accessible.StaticText
    Accessible.name: root.percent >= 0
        ? qsTr("Battery %1 percent").arg(root.percent)
        : qsTr("Battery unknown")

    Rectangle {
        id: pill
        anchors.centerIn: parent
        radius: Theme.radiusSm
        // Match the offline badge / sync glyph visual rhythm: a soft
        // 12% tint of the active colour (accent for healthy, warningAccent
        // for low) on top of bgBase. Border at 30 % alpha for definition.
        readonly property bool lowCharge: root.percent >= 0 && root.percent <= 20
        readonly property color tintColor: lowCharge ? Theme.warningAccent : Theme.accent
        color: Qt.rgba(tintColor.r, tintColor.g, tintColor.b, 0.12)
        border.width: 1
        border.color: Qt.rgba(tintColor.r, tintColor.g, tintColor.b, 0.30)
        implicitWidth: chipRow.implicitWidth + Theme.spacingSm * 2
        implicitHeight: chipRow.implicitHeight + Theme.spacingXs

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: Theme.spacingXs

            // Battery glyph — stacked Rectangle pair (body + tip) sized to
            // the row font so it tracks user font scaling. Avoiding
            // Qt.labs.platform icon-name resolution which is a no-op on
            // Windows. The body fill represents the charge level so the
            // chip is legible even before the percent text resolves
            // (e.g. accessibility users with very large fonts).
            Item {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 18
                implicitHeight: 10

                Rectangle {
                    id: batteryBody
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 2
                    height: parent.height
                    radius: 1
                    color: "transparent"
                    border.width: 1
                    border.color: pill.tintColor

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 1
                        width: Math.max(
                            1,
                            (parent.width - 2) * Math.max(0, Math.min(100, root.percent)) / 100)
                        color: pill.tintColor
                    }
                }

                // Battery tip (the small nub on the +ve terminal).
                Rectangle {
                    anchors.left: batteryBody.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2
                    height: parent.height * 0.5
                    color: pill.tintColor
                }
            }

            Text {
                id: percentLabel
                Layout.alignment: Qt.AlignVCenter
                text: root.percent >= 0 ? qsTr("%1%").arg(root.percent) : qsTr("--%")
                color: pill.tintColor
                font.pixelSize: Theme.fontXs
            }
        }
    }

    // Subscribe to BatteryService updates and filter by our codename.
    // Use Qt 6.7+ function-style signal handlers (NOT the deprecated
    // `onBatteryQueried:` shorthand) so qmllint stays clean.
    Connections {
        target: BatteryService

        function onBatteryQueried(updatedCodename, pct) {
            if (updatedCodename !== root.codename)
                return;
            root.percent = pct;
            root.unavailable = false;
        }

        function onBatteryUnavailable(updatedCodename) {
            if (updatedCodename !== root.codename)
                return;
            // Drop the cached reading: the device can no longer answer.
            // The chip collapses via the `visible` binding above.
            root.percent = -1;
            root.unavailable = true;
        }
    }

    // Seed initial state from the BatteryService cache so a freshly-mounted
    // indicator paints immediately if a previous poll already saw the
    // device, instead of waiting up to 15 s for the next tick.
    Component.onCompleted: {
        if (codename === "")
            return;
        var seed = BatteryService.lastKnownPercent(codename);
        if (seed >= 0) {
            root.percent = seed;
            root.unavailable = false;
        }
    }
}
