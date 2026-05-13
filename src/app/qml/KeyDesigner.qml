// SPDX-License-Identifier: GPL-3.0-or-later
//
// Key designer tab — Stream Dock per-key configurator.
//
// Layout: RowLayout with the key grid on the left and the per-key Inspector
// embedded on the right. Selecting a cell populates the Inspector; edits in
// the Inspector propagate live into the cell preview (Image + overlay
// label).
//
// Properties (set by the parent ProfileEditor):
//   * `keyCount`     — number of cells (default 0).
//   * `gridColumns`  — preferred grid column count (default 5).
//
// Emits:
//   * `keyActivated(int index)` — user clicked / activated a key.
//   * `keySelected(int index)`  — user moved focus to a key.
//
// Session scope: the per-key binding data lives in this component's
// internal `bindings` ListModel. It is **not** persisted yet —
// ProfileController wiring lands in the follow-up slice (see
// .planning/quick/260514-1je-stream-dock-keydesigner/260514-1je-FINDINGS.md
// §4 "What is explicitly deferred"). Selecting a different device or
// restarting the app resets the bindings to empty defaults.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root

    property int keyCount: 0
    property int gridColumns: 5
    property int selectedIndex: -1

    signal keyActivated(int index)
    signal keySelected(int index)

    // -------------------------------------------------------------
    // Session-scoped binding store. One row per key cell, ordered by
    // key index. Roles mirror the subset of ajazz::core::Binding /
    // ajazz::core::KeyState that the current Inspector form surfaces.
    // Multi-action chains, per-state icons and folder pages live in
    // the schema today (profile.hpp) but are intentionally deferred
    // for this slice.
    // -------------------------------------------------------------
    ListModel { id: bindings }

    function _ensureBindings() {
        // Grow / shrink the model to match keyCount. Existing entries
        // are preserved when the count grows (paddingless append); on
        // shrink, trailing rows are dropped.
        while (bindings.count < root.keyCount) {
            bindings.append({
                iconSource: "",
                label: "",
                actionKind: 0,
                actionParams: ""
            });
        }
        while (bindings.count > root.keyCount) {
            bindings.remove(bindings.count - 1);
        }
        if (root.selectedIndex >= bindings.count) {
            root.selectedIndex = -1;
        }
    }

    onKeyCountChanged: _ensureBindings()
    Component.onCompleted: _ensureBindings()

    // Read-only snapshot of the selected row, surfaced as a JS dict so
    // the Inspector can null-check + property-access without poking at
    // the ListModel directly. Recomputed whenever selection changes.
    readonly property var selectedBinding:
        selectedIndex >= 0 && selectedIndex < bindings.count
            ? bindings.get(selectedIndex)
            : null

    // Apply an Inspector edit to the selected row. `field` is one of
    // "iconSource" / "label" / "actionKind" / "actionParams"; `value` is
    // the new value. ListModel.setProperty triggers the Repeater
    // delegate at that index to re-evaluate its model bindings, which
    // updates the cell preview in real time.
    function updateSelectedBinding(field, value) {
        if (root.selectedIndex < 0 || root.selectedIndex >= bindings.count) {
            return;
        }
        bindings.setProperty(root.selectedIndex, field, value);
    }

    // -------------------------------------------------------------
    // Empty state when the device exposes no LCD keys (keyboard,
    // mouse). Keeps the tab non-blank.
    // -------------------------------------------------------------
    EmptyState {
        anchors.centerIn: parent
        visible: root.keyCount === 0
        title: qsTr("No keys")
        body: qsTr("This device does not expose programmable LCD keys.")
    }

    // -------------------------------------------------------------
    // Active layout: grid + inspector side-panel.
    // -------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        visible: root.keyCount > 0
        spacing: Theme.spacingLg

        // Grid pane (flexible width). The grid centres itself so a
        // narrow 5×3 doesn't visually anchor to one side of a wide
        // viewport.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridLayout {
                anchors.centerIn: parent
                columns: Math.max(1, root.gridColumns)
                rowSpacing: Theme.spacingSm
                columnSpacing: Theme.spacingSm

                Repeater {
                    model: bindings
                    delegate: KeyCell {
                        // KeyCell declares index/iconSource/label as
                        // `required` at its type level, so Repeater's
                        // model-role auto-binding wires them directly
                        // from the delegate context (Repeater's row
                        // index + ListModel roles `iconSource`, `label`).
                        selected: root.selectedIndex === index
                        onClicked: {
                            root.selectedIndex = index;
                            root.keySelected(index);
                            root.keyActivated(index);
                        }
                    }
                }
            }
        }

        // Inspector pane (fixed 320 px). The embedded Inspector is
        // live-bound to `selectedBinding` — every form edit fires
        // `bindingFieldChanged`, which routes through
        // updateSelectedBinding() and lights up the cell preview.
        Inspector {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            selectionLabel: root.selectedIndex >= 0
                ? qsTr("Key %1").arg(root.selectedIndex + 1)
                : ""
            binding: root.selectedBinding
            onBindingFieldChanged: function(field, value) {
                root.updateSelectedBinding(field, value);
            }
        }
    }
}
