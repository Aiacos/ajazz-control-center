// SPDX-License-Identifier: GPL-3.0-or-later
//
// DragRelay.qml — app-wide drag-and-drop relay singleton (Phase 29).
//
// WHY THIS EXISTS
// ---------------
// The device editor's drag sources (action-library tiles, key cells, encoder
// dials, touch-strip zones) originally used `Drag.dragType: Drag.Automatic`,
// which spawns a *native* QDrag (OS-level XDND / wl_data_device hand-off).
// niri and several wlroots compositors do not deliver in-process native drags,
// so dragging with a real mouse did nothing at all — the originating
// DropArea never saw an event.
//
// THE FIX (mirrors OpenDeck's two drag "kinds")
// ---------------------------------------------
// OpenDeck distinguishes an "action" drag (copy a new instance from the
// palette) from a "controller" drag (move an existing instance). We keep the
// same two MIME formats but switch to Qt's *internal* drag, which is
// compositor-independent. Internal drag hit-tests DropAreas by the *scene
// geometry* of whichever item carries `Drag.active == true`. Our sources use
// `DragHandler { target: null }`, so the source item never moves — there would
// be nothing for a DropArea to collide with. So we host ONE cursor-following
// "ghost" item in the window overlay (see Main.qml) and route every source
// through this relay:
//
//   * each SOURCE owns only a DragHandler (pointer grab + drag threshold) and,
//     on activation, calls begin()/finish() here and feeds the live cursor
//     position via moveTo();
//   * this SINGLETON carries the active MIME payload + cursor hotspot + a few
//     visual hints for the floating drag image;
//   * the GHOST (Main.qml) binds Drag.active / Drag.mimeData to this relay,
//     follows `hotspot`, and performs the explicit `Drag.drop()` that internal
//     drags require to fire a DropArea's onDropped.
//
// The DropArea side of every cell is UNCHANGED — it already works for internal
// drags (keys filter, hasFormat/getDataAsString, affordance-mask gating,
// reject visuals).
pragma Singleton
import QtQuick

QtObject {
    id: relay

    // True while a drag is in flight. The overlay ghost binds Drag.active here.
    property bool active: false

    // The single active MIME format + its JSON payload string.
    //   "application/x-ajazz-action"  → palette copy   { actionKind, label, ... }
    //   "application/x-ajazz-binding" → existing move  { controller, position }
    property string mimeKey: ""
    property string payload: ""

    // Live cursor position in SCENE coordinates (fed from the source
    // DragHandler's centroid.scenePosition). The ghost positions itself here.
    property point hotspot: Qt.point(0, 0)

    // Visual hints so the floating drag image resembles what is being dragged.
    property url    ghostIconUrl:  ""   // plugin icon when available
    property string ghostIconName: ""   // Material Symbols ligature fallback
    property string ghostLabel:    ""

    // Emitted on pointer release so the overlay ghost can call Drag.drop()
    // while `active` is still true (internal drag requires an explicit drop()).
    signal dropRequested()

    // Start a drag. iconUrl/iconName/label drive the floating ghost; (sx, sy)
    // is the initial cursor position in scene coordinates.
    function begin(key, payloadStr, iconUrl, iconName, label, sx, sy) {
        relay.mimeKey       = key;
        relay.payload       = payloadStr;
        relay.ghostIconUrl  = iconUrl  ? iconUrl  : "";
        relay.ghostIconName = iconName ? iconName : "";
        relay.ghostLabel    = label    ? label    : "";
        relay.hotspot       = Qt.point(sx, sy);
        relay.active        = true;
    }

    // Track the cursor while dragging.
    function moveTo(sx, sy) {
        relay.hotspot = Qt.point(sx, sy);
    }

    // Pointer released: ask the ghost to commit the drop under the cursor, then
    // clear. The ghost's Connections handler runs synchronously inside the
    // dropRequested() emission, so the drop is finished before clear() runs.
    function finish() {
        if (!relay.active)
            return;
        relay.dropRequested();
        relay.clear();
    }

    function clear() {
        relay.active        = false;
        relay.mimeKey       = "";
        relay.payload       = "";
        relay.ghostIconUrl  = "";
        relay.ghostIconName = "";
        relay.ghostLabel    = "";
    }
}
