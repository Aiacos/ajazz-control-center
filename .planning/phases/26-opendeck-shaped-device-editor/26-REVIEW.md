---
phase: 26-opendeck-shaped-device-editor
reviewed: 2026-05-28T18:00:00Z
depth: standard
files_reviewed: 23
files_reviewed_list:
  - src/app/CMakeLists.txt
  - src/app/qml/ActionLibraryPane.qml
  - src/app/qml/DeviceView.qml
  - src/app/qml/Main.qml
  - src/app/qml/ProfileEditor.qml
  - src/app/qml/components/EncoderDial.qml
  - src/app/qml/components/KeyCell.qml
  - src/app/qml/components/TouchStripLane.qml
  - src/app/src/device_model.cpp
  - src/app/src/profile_controller.cpp
  - src/app/src/profile_controller.hpp
  - src/app/src/stream_dock_control_service.hpp
  - src/core/include/ajazz/core/device.hpp
  - src/core/include/ajazz/core/profile.hpp
  - src/core/src/profile.cpp
  - src/devices/streamdeck/src/register.cpp
  - tests/qml/CMakeLists.txt
  - tests/qml/test_device_view_drag_drop.qml
  - tests/qml/test_device_view_geometry.qml
  - tests/qml/test_device_view_tests.cpp
  - tests/qml/test_qml_smoke.cpp
  - tests/unit/CMakeLists.txt
  - tests/unit/test_profile_serialization.cpp
  - tests/unit/test_streamdeck_register_geometry.cpp
findings:
  critical: 4
  warning: 6
  info: 4
  total: 14
status: issues_found
---

# Phase 26: Code Review Report

**Reviewed:** 2026-05-28T18:00:00Z
**Depth:** standard
**Files Reviewed:** 23
**Status:** issues_found

## Summary

Phase 26 delivers the OpenDeck-shaped device editor (DeviceView.qml replacing
KeyDesigner.qml), the setActiveDevice wiring (REQ-26-A), DeviceDescriptor geometry
fields (REQ-26-C), profile schema v2 + touchZones (D-11/D-12), and the geometry
regression test (REQ-26-D). The COD-031 invariant (no nlohmann in core/include) is
clean. The QML_SINGLETON pattern follows the documented non-default-constructible
static_assert on all three singleton classes touched (ProfileController,
StreamDockControlService). KeyDesigner.qml is gone; ProfileEditor routes to
DeviceView correctly.

Four blockers are present: the profile JSON reader has an ordering dependency that
silently discards touchZones when the key precedes \_schemaVersion in third-party
JSON; `resetActiveProfile()` does not clear touchZones despite clearing all other
maps; `selectedZoneIndex` is declared and passed to the Inspector label but is never
set positive (touch-zone tap can never propagate selection state to the Inspector);
and `commitEncoderBinding` is called from EncoderDial.qml's drop handler without
existing on ProfileController, producing a silent QML no-op with no runtime guard
or user feedback.

______________________________________________________________________

## Critical Issues

### CR-01: JSON reader silently discards touchZones when key precedes \_schemaVersion

**File:** `src/core/src/profile.cpp:849-895`

**Issue:** `profileFromJson` initialises `schemaVersion = 1` and dispatches key
parsing in a single pass. The branch at line 870 (`if (schemaVersion >= 2)`) guards
the touchZones read. If an external tool (or a future refactor of the writer) emits
the `"touchZones"` key **before** `"_schemaVersion"` in the JSON object, the reader
will skip it silently: `schemaVersion` is still 1 at that point, so the
`else { r.skipValue(); }` branch is taken and all touch-zone data is permanently
lost for that load. The writer always emits `_schemaVersion` as the second key (so
self-written files are safe), but JSON object key order is not specified by RFC 8259
and any compliant serialiser or hand-edited file may reorder keys.

**Fix:** Hoist the schema version to a pre-pass or treat a present `touchZones` key
as implicitly v2 regardless of schema version order:

```cpp
// Option A: two-pass (safest). Parse the whole object once to get
// _schemaVersion, then reset the reader and parse again.

// Option B (minimal change): when "touchZones" is encountered
// and schemaVersion is still 1 (not yet seen), treat it as v2:
} else if (key == "touchZones") {
    // Always attempt to parse if the key is present; an absent
    // _schemaVersion in an externally-generated file that carries
    // touchZones should be read, not discarded.
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const idxStr = r.readString();
            std::uint8_t idx = 0;
            try {
                idx = static_cast<std::uint8_t>(std::stoul(idxStr) & 0xFFu);
            } catch (std::exception const&) { /* throw */ }
            r.expect(':');
            profile.touchZones.emplace(idx, readTouchZoneBinding(r));
            if (r.tryConsume(',')) continue;
            r.expect('}');
            break;
        }
    }
```

Remove the `schemaVersion >= 2` guard on the `touchZones` branch. The schema
version flag is already safe for **missing** touchZones on genuine v1 files (they
have no `"touchZones"` key at all, so the branch is never reached).

______________________________________________________________________

### CR-02: resetActiveProfile() silently omits touchZones from clear

**File:** `src/app/src/profile_controller.cpp:263-277`

**Issue:** `resetActiveProfile()` clears `keys`, `encoders`, `mouseButtons`, and
per-page keys, but does **not** clear `m_profile.touchZones`. After the user clicks
"Restore defaults", every previously configured touch-zone binding survives in
memory and will be written back to disk on the next `saveActiveProfile()`, giving
the user a false "reset" for touch-strip-capable devices.

```cpp
void ProfileController::resetActiveProfile() {
    m_profile.keys.clear();
    m_profile.encoders.clear();
    m_profile.mouseButtons.clear();     // touchZones is NOT cleared
    for (auto& [id, page] : m_profile.pages) {
        page.keys.clear();
    }
    emit profileChanged();
    saveActiveProfile();
}
```

**Fix:**

```cpp
void ProfileController::resetActiveProfile() {
    m_profile.keys.clear();
    m_profile.encoders.clear();
    m_profile.mouseButtons.clear();
    m_profile.touchZones.clear();   // ADD: Phase 26 D-11 map
    for (auto& [id, page] : m_profile.pages) {
        page.keys.clear();
    }
    emit profileChanged();
    saveActiveProfile();
}
```

______________________________________________________________________

### CR-03: selectedZoneIndex is never set positive; touch-zone Inspector selection is permanently broken

**File:** `src/app/qml/DeviceView.qml:58,63,375-376`

**Issue:** `DeviceView.qml` declares `selectedZoneIndex` (line 58), exposes a
`zoneSelected(int idx)` signal (line 63), and uses the index in the `selectionLabel`
binding passed to the Inspector (lines 375-376). However, `selectedZoneIndex` is
**only ever assigned -1** — in the key-click handler (line 214) and encoder-click
handler (line 261) — and is never set to a valid zone index.

`TouchStripLane.qml` has no click handler on `ItemDelegate` (only drag and drop),
and `DeviceView.qml` does not connect any `zoneSelected` emission from the lane.
As a result: tapping a touch-zone cell in the editor **does not** update the
Inspector's `selectionLabel` or the `binding` property with zone-specific data;
the Inspector always shows the empty-selection state for zones regardless of which
zone the user interacts with.

**Fix:** Add a `clicked` signal to `TouchZoneCell`/`ItemDelegate` in
`TouchStripLane.qml` and connect it in `DeviceView.qml`:

```qml
// In TouchStripLane.qml, TouchZoneCell's ItemDelegate:
ItemDelegate {
    id: zoneDelegate
    anchors.fill: parent
    background: Item {}
    property bool selected: false
    // ADD: forward clicks so DeviceView can update selectedZoneIndex
    onClicked: zoneCell.zoneSwapRequested(-1, zoneCell.zoneIndex)  // re-use signal or add new one
}
```

The cleanest fix is to add a `signal zoneTapped(int idx)` to `TouchZoneCell` and
`TouchStripLane`, forward the signal in `DeviceView`:

```qml
// In DeviceView.qml, TouchStripLane delegate:
TouchStripLane {
    visible: root.touchZoneCount > 0
    touchZoneCount: root.touchZoneCount
    zoneIconSources: []
    zoneLabels: []
    onZoneTapped: function(idx) {           // ADD
        root.selectedKeyIndex = -1;
        root.selectedEncoderIndex = -1;
        root.selectedZoneIndex = idx;
        root.zoneSelected(idx);
    }
    onZoneSwapRequested: function(src, dst) { ... }
}
```

Until this is fixed, REQ-26-B acceptance criterion "each cell is a distinct drop
target" holds, but operator workflow (tap a zone → inspect binding → change it) is
broken for touch zones.

______________________________________________________________________

### CR-04: commitEncoderBinding called from QML without existing on ProfileController; no fallback or user feedback

**File:** `src/app/qml/components/EncoderDial.qml:149`

**Issue:** `EncoderDial.qml` line 149 calls
`ProfileController.commitEncoderBinding(root.index, "", ap.label, ap.actionKind, "")`.
`ProfileController` exposes no `commitEncoderBinding` Q_INVOKABLE (confirmed by
reading `profile_controller.hpp`). In Qt 6 QML, calling a non-existent Q_INVOKABLE
on a singleton produces a runtime `qWarning` ("commitEncoderBinding is not a
function") but is otherwise a silent no-op. The user receives zero feedback that
their drop was ignored; the binding appears to succeed visually because
`EncoderDial.qml` calls `drop.acceptProposedAction()` unconditionally at line 151.

This is documented as a "known stub" in the plan summary, but no comment in
`EncoderDial.qml` tells the maintainer that the drop call is a silent no-op,
and no `drop.accepted = false` is set on the missing-method path to make the UI
honest. The encoder drop therefore silently lies: the drag-drop gesture says
"accepted" but nothing is persisted.

**Fix (minimal v1):** Reject the drop explicitly when the method is absent, so
the user gets visual rejection feedback rather than false acceptance:

```qml
// EncoderDial.qml DropArea onDropped, "application/x-ajazz-action" branch:
if (drop.hasFormat("application/x-ajazz-action")) {
    var ap = JSON.parse(drop.getDataAsString("application/x-ajazz-action"));
    // Phase 26 v1: commitEncoderBinding not yet in ProfileController.
    // Reject the drop honestly rather than silently accepting a no-op.
    drop.accepted = false;
    // TODO: replace with ProfileController.commitEncoderBinding(...)
    //       when the Q_INVOKABLE lands (follow-up plan).
    return;
}
```

**Fix (proper):** Add `commitEncoderBinding` Q_INVOKABLE to `ProfileController`,
following the same pattern as `commitKeyBinding` / `commitTouchZoneBinding`.

______________________________________________________________________

## Warnings

### WR-01: Photo background is loaded but never rendered; \_hasPhoto is a dead property

**File:** `src/app/qml/DeviceView.qml:134-141`

**Issue:** `loadLayout(codename)` is called on every codename change (line 137),
parsing the JSON via a synchronous XMLHttpRequest. `_hasPhoto` is computed (line
140-141). The device-layouts directory is now populated (17 SKU JSONs confirmed in
`resources/device-layouts/`). However, `_hasPhoto` is **never referenced** after
line 141 — no `Image { source: ... }` element or conditional in the chassis renders
the photo. The outline-frame Rectangle is always `visible: true` (line 183) with
no branch on `_hasPhoto`. The layout JSON is parsed on every device change for no
effect. The comment at lines 23-25 says "outline-frame always active" as a v1
note, but the loaded layout data (key positions, encoder positions, touch-zone
positions from the JSON) is also never consumed for positioned-cell rendering.

This is a dead-property warning, not a crash. The overhead is a synchronous
`XMLHttpRequest` on every codename change, which is harmless for qrc resources
but leaves the v1 implementation in a state where the loaded JSON data has no
consumer. When photo rendering is activated, a reviewer should verify the
synchronous XHR does not block on slower targets.

**Fix:** Either remove the `loadLayout` call + `_hasPhoto` property entirely until
the photo path is actually wired, OR add a TODO comment pointing to the follow-up
plan that activates the photo path. Do not leave dead computed properties in
production code without explanation.

______________________________________________________________________

### WR-02: anyDragActive is declared but never set; trash-zone opacity is always 0.3

**File:** `src/app/qml/DeviceView.qml:112,299`

**Issue:** `DeviceView.qml` declares `property bool anyDragActive: false` (line
112\) and uses it to control trash-zone button opacity (line 299). However, no code
path in DeviceView, KeyCell, EncoderDial, or TouchStripLane ever sets
`root.anyDragActive = true`. The trash button's opacity therefore remains at 0.3
permanently — the D-10 "100% during any cell drag" visual feedback is never applied.

The D-09 / UI-SPEC trash-zone affordance states the button should go fully opaque
during any drag so users discover it as a valid drop target. This feedback is
permanently suppressed.

**Fix:** Set `anyDragActive` from child-component drag state:

```qml
// In KeyCell/EncoderDial/TouchZoneCell DragHandler, add:
Binding {
    target: root           // the DeviceView root
    property: "anyDragActive"
    value: dragHandler.active
    when: dragHandler.active
    restoreMode: Binding.RestoreBindingOrValue
}
```

Or: wire a `Drag.onActiveChanged` connection on each child that sets
`DeviceView.anyDragActive`.

______________________________________________________________________

### WR-03: test_device_view_geometry.qml uses wrong row/column argument order for AKP03 and AKP153

**File:** `tests/qml/test_device_view_geometry.qml:50-87`

**Issue:** The AKP153 DeviceView in the test (lines 50-60) is configured with:

```qml
keyRows:     5
gridColumns: 3
```

This matches the physical AKP153 layout (3 columns × 5 rows = 15 keys), but note
that the C++ geometry in `register.cpp` uses `keyRows = 3` in `akp153_descriptor`
(line 82 of register.cpp). The test passes the values transposed from the
descriptor: the test says rows=5/cols=3 while the descriptor says keyRows=3 and
gridColumns=5. The QML geometry test creates the DeviceView with hard-coded
geometry, not from the descriptor, so the test happens to render correctly (a
3×5=15-cell grid either way), but the test does not validate that DeviceView
correctly uses the descriptor-supplied values.

Similarly, the AKP03 test (lines 71-80) uses `keyRows: 3, gridColumns: 2` — that
is 2 cols × 3 rows = 6 keys, which is correct, but the `akp03_descriptor` in
`register.cpp` sets `keyRows = 2` and `gridColumns = 3`. Again, the test is not
exercising the actual device geometry from the descriptor.

The `test_device_view_tests.cpp` C++ test (line 105) does the same: `keyRows=5, gridColumns=3` for AKP153, and `keyRows=3, gridColumns=2` for AKP03 (line 115).

These tests pass because the key *count* is correct (15 and 6), but the row/column
counts are swapped from what the actual descriptors declare. Any code that reads
`keyRowsResolved` directly (for layout purposes) would get a different value under
test than under production.

**Fix:** Align test geometry with the descriptor:

```qml
// AKP153: keyRows=3, gridColumns=5 (matches akp153_descriptor in register.cpp)
// AKP03:  keyRows=2, gridColumns=3 (matches akp03_descriptor in register.cpp)
```

______________________________________________________________________

### WR-04: DeviceModel roleNames does not expose keyRows or touchZoneCount as model roles

**File:** `src/app/src/device_model.cpp:138-157`

**Issue:** `DeviceModel::capabilitiesFor()` correctly exposes `keyRows`,
`touchZoneCount`, `mainScreenWidthPx`, and `mainScreenHeightPx` in the QVariantMap
returned to QML (lines 318-321). However, `DeviceModel::roleNames()` does not
define roles for these four geometry fields. Any QML that binds directly to the
`DeviceModel` list view (e.g. `model.keyRows` in a delegate) will get `undefined`.

In the current implementation, `ProfileEditor.qml` reads them through
`DeviceModel.capabilitiesFor(codename)`, which works. But the absence of
corresponding roles means `DeviceList.qml` or any future list delegate cannot bind
to geometry fields, and the role-based `dataChanged` mechanism will not fire for
them. This is a latent inconsistency between the data API and the model roles.

**Fix:** Add the four new geometry roles to the `Roles` enum in
`device_model.hpp` and expose them in `roleNames()` and `data()`:

```cpp
// In device_model.hpp Roles enum:
KeyRowsRole,
TouchZoneCountRole,
MainScreenWidthPxRole,
MainScreenHeightPxRole,

// In roleNames():
{KeyRowsRole, "keyRows"},
{TouchZoneCountRole, "touchZoneCount"},
// etc.

// In data() switch:
case KeyRowsRole: return static_cast<int>(d.keyRows);
```

______________________________________________________________________

### WR-05: Synchronous XMLHttpRequest in QML loadLayout triggers Qt deprecation warning on Qt 6.8+

**File:** `src/app/qml/DeviceView.qml:122`

**Issue:** `xhr.open("GET", url, false)` — the third argument `false` requests
synchronous XHR. Qt 6 has deprecated synchronous XMLHttpRequest in QML as of
Qt 6.8; the documented replacement is the `async` mode with an `onreadystatechange`
callback. On Qt 6.8+ this generates a runtime warning:

```
qrc:/qt/qml/AjazzControlCenter/DeviceView.qml:122: Use of synchronous
XMLHttpRequest in a non-main context is deprecated.
```

The test harness in `test_qml_smoke.cpp` is configured to treat QML warnings as
test failures. If CI upgrades Qt beyond 6.7, this warning will trigger test
failures.

Additionally, blocking the QML main thread on I/O — even for a qrc resource — is
architecturally unsound and will break if the layout loader is ever extended to
HTTP URLs.

**Fix:** Switch to async XHR with proper state handling:

```qml
function loadLayout(cn) {
    if (cn === "") return null;
    var url = "qrc:/qt/qml/AjazzControlCenter/device-layouts/" + cn + ".json";
    var xhr = new XMLHttpRequest();
    xhr.open("GET", url, true);   // async
    xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE) {
            if (xhr.status === 200) {
                try {
                    root._layout = JSON.parse(xhr.responseText);
                } catch (e) {
                    root._layout = null;
                }
            } else {
                root._layout = null;
            }
        }
    };
    xhr.send();
    // _layout starts null; binding updates when async completes
}
```

Note: the `Qt.createQmlObject('import QtQml 2.15; XMLHttpRequest {}', root)` form
at line 121 is also unusual (creating an object from an inline string to get the
XMLHttpRequest type). The standard form `new XMLHttpRequest()` is valid QML and
does not need the `Qt.createQmlObject` indirection.

______________________________________________________________________

### WR-06: Profile schema reader ordering dependency is undocumented and fragile

**File:** `src/core/src/profile.cpp:844-895`

**Issue:** The `touchZones` guard (`if (schemaVersion >= 2)` at line 870) creates
an implicit requirement that `"_schemaVersion"` must appear **before**
`"touchZones"` in the JSON document. This ordering guarantee holds for the
in-house writer (which always emits `_schemaVersion` second, after `id`), but:

1. No comment in the reader documents this ordering requirement.
1. A JSON linter, a diff-merge tool, or a third-party writer that emits the
   `"touchZones"` key first will cause all touch-zone bindings to be silently lost.
1. The round-trip unit test (`test_profile_serialization.cpp`) only tests
   `profileToJson` → `profileFromJson` using the in-house writer, so it will not
   catch this ordering sensitivity.

**Fix:** See CR-01 for the recommended code fix. At minimum, add a comment and a
test that verifies touchZones are parsed when they precede `_schemaVersion`:

```cpp
TEST_CASE("v2 profile with touchZones key before _schemaVersion round-trips",
          "[profile][migration][ordering]") {
    // Keys are in reversed order relative to writer output.
    constexpr char const* kReorderedJson =
        R"({"id":"x","touchZones":{"0":{"onTap":[]}},"_schemaVersion":2,
            "name":"Y","device":"akp05e","keys":{},"encoders":{}})";
    auto const p = profileFromJson(kReorderedJson);
    // This SHOULD have 1 touch zone. If it has 0, the ordering bug is live.
    REQUIRE(p.touchZones.size() == 1);
}
```

______________________________________________________________________

## Info

### IN-01: akp03_descriptor helper omits touchZoneCount field; relies on zero default

**File:** `src/devices/streamdeck/src/register.cpp:90-103`

**Issue:** The `akp03_descriptor` helper function produces a struct initialiser that
does not set `touchZoneCount`, relying on the zero default in `DeviceDescriptor`.
AKP03 has no touch strip, so the value should indeed be 0. However, unlike the AKP05
descriptors (which explicitly set `.touchZoneCount = 4` and
`.mainScreenWidthPx = 0` / `.mainScreenHeightPx = 0` with explanatory comments),
the AKP03 and AKP153 descriptor helpers are silent about the Phase 26 fields. A
future contributor reading the helper is left wondering if the fields were forgotten
or deliberately omitted.

**Fix:** Add an explicit zero with a comment:

```cpp
constexpr auto akp03_descriptor(...) {
    return core::DeviceDescriptor{
        ...
        .keyRows = 2,        // 2x3 grid (REQ-26-C)
        .touchZoneCount = 0, // AKP03 has no touch strip (REQ-26-C)
    };
}
```

______________________________________________________________________

### IN-02: LibraryTile component uses `pragma ComponentBehavior: Bound` but captures root via outer scope

**File:** `src/app/qml/ActionLibraryPane.qml:13,87-162`

**Issue:** `ActionLibraryPane.qml` sets `pragma ComponentBehavior: Bound`, which
is correct for the `LibraryTile` inline component. The `LibraryTile` properly
declares all its required properties. However, the `contentItem` layout (line
128-157) refers to `Theme.spacingLg` / `Theme.spacingSm` via the global
singleton, and the `background` Rectangle (line 124-126) reads `Theme.bgRowHover`
/ `Theme.bgBase`. These are singleton accesses, not outer-scope captures, so they
are compatible with `ComponentBehavior: Bound`. No issue in practice, but worth
noting for future contributors.

______________________________________________________________________

### IN-03: Test case name style inconsistency in test_device_view_tests.cpp

**File:** `tests/qml/test_device_view_tests.cpp:93-207`

**Issue:** The Catch2 test names use a `ClassName::method_name` style with
underscores and double-colons, e.g.:
`"DeviceViewGeometry::test_akp05e_renders_5x2_grid_plus_4_dials_plus_4_zones"`.

The CLAUDE.md requirement for ASCII-only names is satisfied. However, the colons
`::` in test names may conflict with CTest's tag-filter syntax (`[tagname]`) on
some shell environments, and the names diverge from the project's existing
Catch2 naming convention (which uses `TEST_CASE("action verb phrase", "[tag]")`
format in other test files). This is not a build failure but creates inconsistency
in `ctest -N` output.

**Fix (optional):** Align with project convention:

```cpp
TEST_CASE("DeviceView renders 5x2 key grid plus 4 dials plus 4 zones for AKP05E",
          "[qml][device_view][geometry]")
```

______________________________________________________________________

### IN-04: loadLayout creates XMLHttpRequest via Qt.createQmlObject rather than standard constructor

**File:** `src/app/qml/DeviceView.qml:121`

**Issue:** Line 121 uses:

```qml
var xhr = Qt.createQmlObject('import QtQml 2.15; XMLHttpRequest {}', root);
```

The standard QML idiom for XMLHttpRequest is the plain constructor:

```qml
var xhr = new XMLHttpRequest();
```

The `Qt.createQmlObject` path is unnecessary overhead (it compiles a new QML
component on each call), creates a persistent QObject child of `root` that is
never deleted (the `xhr` local goes out of scope but the parented QObject
remains alive), and the `QtQml 2.15` import hint is outdated (Qt 6 uses
`QtQml`/`import QtQml` without version numbers in idiomatic code). This is a
memory-leak vector proportional to the number of codename changes.

**Fix:** Use the standard constructor and see WR-05 for the async migration:

```qml
var xhr = new XMLHttpRequest();
xhr.open("GET", url, false);
```

______________________________________________________________________

_Reviewed: 2026-05-28T18:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
