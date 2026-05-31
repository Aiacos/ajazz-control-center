# Phase 28: AKP05 Plugin Action Completeness + Drag-to-Bind on Keys & Dials - Pattern Map

**Mapped:** 2026-05-31
**Files analyzed:** 11 (8 modified, 3 new)
**Analogs found:** 10 / 11

______________________________________________________________________

## File Classification

| New/Modified File                               | Role          | Data Flow               | Closest Analog                                                                                                                                           | Match Quality                                                                                         |
| ----------------------------------------------- | ------------- | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `src/app/qml/components/EncoderDial.qml`        | component     | request-response (drop) | `src/app/qml/components/KeyCell.qml` + `DeviceView.qml:316-330`                                                                                          | exact (same DropArea shape, same onDropped pattern)                                                   |
| `src/app/qml/components/TouchStripLane.qml`     | component     | request-response (drop) | `src/app/qml/components/KeyCell.qml` + `DeviceView.qml:316-330`                                                                                          | exact (same DropArea shape, same onDropped pattern)                                                   |
| `src/app/qml/components/KeyCell.qml`            | component     | request-response (drop) | self — add library affordance gating to `onEntered` mirroring the existing binding gating at `:185-198`                                                  | role-match (extend existing)                                                                          |
| `src/app/src/plugin_manifest.hpp`               | model         | transform               | `src/app/src/plugin_manifest.hpp:28-47` (existing `PluginAction`/`PluginActionState`)                                                                    | exact (add fields to existing structs)                                                                |
| `src/app/src/plugin_manifest.cpp`               | parser        | transform               | `src/app/src/plugin_manifest.cpp:36-78` (`parseState`, `parseAction`)                                                                                    | exact (same JSON key extraction pattern)                                                              |
| `src/app/src/plugin_catalog_model.cpp`          | model/service | CRUD                    | `src/app/src/plugin_catalog_model.cpp:348-432` (`installedActions()`)                                                                                    | exact (same `QVariantMap` builder + skip-continue gates)                                              |
| `src/app/qml/ActionLibraryPane.qml`             | component     | CRUD                    | `src/app/qml/ActionLibraryPane.qml:160-201` (`LibraryTile` + `Drag.mimeData`)                                                                            | exact (extend existing MIME payload + `_rebuild()` append)                                            |
| `src/app/src/application.cpp`                   | wiring        | event-driven            | `src/app/src/application.cpp:452-483` (`profileChanged` → repaint connections)                                                                           | exact (add one more `QObject::connect` to the `#ifdef AJAZZ_HAVE_WEBSOCKETS` block)                   |
| `src/app/src/debug_control_facade.cpp`          | utility       | request-response (RPC)  | `src/app/src/debug_control_facade.cpp:446-510` (`plugin.list` / `plugin.installFromFile`)                                                                | exact (same `server.registerMethod` lambda shape)                                                     |
| `tests/unit/test_catalog_offline.cpp`           | test          | CRUD                    | `tests/unit/test_catalog_offline.cpp:225-310`                                                                                                            | exact (same fixture-layout + `model.installedActions()` assertion)                                    |
| `tests/unit/test_plugin_manifest.cpp`           | test          | transform               | `tests/unit/test_plugin_manifest.cpp:68-159`                                                                                                             | exact (same fixture-load + struct-field assertion)                                                    |
| `src/app/src/plugin_manifest.hpp` (new free fn) | utility       | transform               | `src/app/src/plugin_manifest.hpp:118,152` (`parsePluginManifest`, `currentPlatformString` declarations) + `src/app/src/plugin_device_bridge.hpp:243,257` | role-match (same `[[nodiscard]]` free-fn-in-namespace pattern, declared in `.hpp`, defined in `.cpp`) |

______________________________________________________________________

## Pattern Assignments

### 1. `src/app/qml/components/EncoderDial.qml` + `TouchStripLane.qml` (MODIFY — fix 5-arg drops + add affordance gating)

**Primary analog:** `src/app/qml/DeviceView.qml:316-330` for the 6-arg commit call; `src/app/qml/components/KeyCell.qml:181-244` for the DropArea / `onEntered` gating shape.

**Working 6-arg key commit pattern** (`DeviceView.qml:316-330`):

```qml
onKeyActionDropped: function(index, payload) {
    if (index < 0 || index >= bindings.count) return;
    var icon = payload.iconUrl ? payload.iconUrl : "";
    var lbl  = payload.label   ? payload.label   : "";
    var aid  = payload.actionId ? payload.actionId : "";
    bindings.set(index, { iconSource: icon, label: lbl,
                          actionKind: payload.actionKind, actionParams: "",
                          actionId: aid });
    ProfileController.commitKeyBinding(index, icon, lbl,
                                       payload.actionKind, "", aid);  // 6 args
    // ...
}
```

**The identical shape to copy into `EncoderDial.qml:176-183`** — replace the broken 5-arg call at `:179`:

```qml
// BROKEN (line 179):
ProfileController.commitEncoderBinding(root.index, "", ap.label,
                                       ap.actionKind, "");
// FIX — add ap.actionId as 6th arg (match DeviceView.qml:324 pattern):
ProfileController.commitEncoderBinding(root.index, "", ap.label,
                                       ap.actionKind, "",
                                       ap.actionId || "");
```

**The identical shape to copy into `TouchStripLane.qml:269-273`** — replace the broken 5-arg call at `:271`:

```qml
// BROKEN (line 271):
ProfileController.commitTouchZoneBinding(zoneCell.zoneIndex, "",
                                        ap.label, ap.actionKind, "");
// FIX — add ap.actionId as 6th arg:
ProfileController.commitTouchZoneBinding(zoneCell.zoneIndex, "",
                                        ap.label, ap.actionKind, "",
                                        ap.actionId || "");
```

**Affordance gating in `onEntered` — copy from `KeyCell.qml:181-204`** (the binding-drag gate):

```qml
// KeyCell.qml:181-204 — EXISTING pattern for binding-type drags:
onEntered: function(drag) {
    if (drag.hasFormat("application/x-ajazz-binding")) {
        var raw = drag.getDataAsString("application/x-ajazz-binding");
        var ok = false;
        try {
            var payload = JSON.parse(raw);
            ok = (payload.controller === "Keypad");  // gate: source must be Keypad
        } catch (e) { ok = false; }
        if (!ok) {
            dragRejected = true;
            drag.accepted = false;   // visually signal reject
            return;
        }
    }
    dragRejected = false;
    cellScale.xScale = 1.05;
    cellScale.yScale = 1.05;
    drag.accepted = true;
}
```

For the **library-action gating** (new, absent from all three components), add a parallel branch for `"application/x-ajazz-action"` inside `onEntered` of `EncoderDial`, `TouchStripLane`, and `KeyCell`. Use the same `dragRejected = true; drag.accepted = false; return;` idiom. The gate condition reads `payload.affordanceMask` (the new int field added to the MIME payload — see §5 below).

**Reject visual pattern** (`KeyCell.qml:87-93`):

```qml
border.width: dropArea.dragRejected
    ? Theme.focusRingWidth
    : (root.activeFocus || root.selected ? Theme.focusRingWidth : 1)
border.color: dropArea.dragRejected
    ? Theme.errorAccent
    : (root.activeFocus || root.selected ? Theme.accent : Theme.borderSubtle)
```

Copy this to `EncoderDial.qml` and `TouchStripLane.qml` backgrounds — both already have a `dragRejected` property set up at `:143` and `:235` respectively; the border conditional just needs to be wired the same way.

______________________________________________________________________

### 2. `src/app/src/plugin_manifest.hpp` + `plugin_manifest.cpp` (MODIFY — full action model)

**Analog:** existing `PluginActionState` / `PluginAction` structs (`plugin_manifest.hpp:28-47`) and `parseState()` / `parseAction()` functions (`plugin_manifest.cpp:36-78`).

**Existing struct pattern to extend** (`plugin_manifest.hpp:28-47`):

```cpp
struct PluginActionState {
    QString image;          ///< States[i].Image
    QString fontSize;       ///< States[i].FontSize (or FSize Mirabox synonym)
    QString fontFamily;     ///< States[i].FontFamily (or FFamily Mirabox synonym)
    QString fontStyle;      ///< States[i].FontStyle
    QString titleColor;     ///< States[i].TitleColor
    QString titleAlignment; ///< States[i].TitleAlignment
};

struct PluginAction {
    QString uuid;                  ///< UUID (reverse-DNS)
    QString name;                  ///< Name (display label, may be CJK)
    QString icon;                  ///< Icon path
    QString tooltip;               ///< Tooltip (optional)
    QString propertyInspectorPath; ///< PropertyInspectorPath (optional)
    QStringList controllers;       ///< Controllers
    std::vector<PluginActionState> states; ///< States array
    bool isK1Pro{false};                   ///< IsK1Pro (AJAZZ extension)
};
```

New fields to add follow exactly the same doc-comment style (`///< JSON key (default value)`). New `PluginEncoderBlock` struct goes between `PluginActionState` and `PluginAction` (same placement logic: depended-on type first).

**Existing field-parse pattern to copy in `parseAction()`** (`plugin_manifest.cpp:56-78`):

```cpp
PluginAction parseAction(QJsonObject const& obj) {
    PluginAction a;
    a.uuid    = obj.value(QStringLiteral("UUID")).toString();
    a.name    = obj.value(QStringLiteral("Name")).toString();
    a.isK1Pro = obj.value(QStringLiteral("IsK1Pro")).toBool(false);

    // Controllers: no rejection on unknown values -- just store.
    QJsonArray const controllers = obj.value(QStringLiteral("Controllers")).toArray();
    for (QJsonValue const& cv : controllers)
        a.controllers.append(cv.toString());

    // States[]: empty/missing array is valid.
    QJsonArray const states = obj.value(QStringLiteral("States")).toArray();
    a.states.reserve(static_cast<std::size_t>(states.size()));
    for (QJsonValue const& sv : states)
        a.states.push_back(parseState(sv.toObject()));

    return a;
}
```

New fields follow the same `.toBool(default)` / `.toString()` pattern. Nested-object parse follows the existing `Nodejs` / `Software` pattern from `plugin_manifest.cpp:154-164`:

```cpp
QJsonValue const nodejsVal = root.value(QStringLiteral("Nodejs"));
if (nodejsVal.isObject()) {
    m.nodejsVersion = nodejsVal.toObject().value(QStringLiteral("Version")).toString();
}
```

Mirror this for the `Encoder` block and `TriggerDescription` sub-object:

```cpp
QJsonValue const encVal = obj.value(QStringLiteral("Encoder"));
if (encVal.isObject()) {
    QJsonObject const encObj = encVal.toObject();
    a.encoderBlock.layout = encObj.value(QStringLiteral("layout")).toString();
    // ... TriggerDescription sub-object follows the same if (tdVal.isObject()) pattern
}
```

**State name/title additions follow `parseState()` pattern** (`plugin_manifest.cpp:36-53`):

```cpp
PluginActionState parseState(QJsonObject const& obj) {
    PluginActionState s;
    s.image = obj.value(QStringLiteral("Image")).toString();
    // FontSize: prefer Elgato standard; fall back to Mirabox synonym.
    QString const fontSize = obj.value(QStringLiteral("FontSize")).toString();
    s.fontSize = fontSize.isEmpty() ? obj.value(QStringLiteral("FSize")).toString() : fontSize;
    // ... same pattern for FontFamily/FFamily
    return s;
}
```

New `name`, `title`, `showTitle` fields go in `parseState()` with the same `.toString()` / `.toBool(true)` calls.

______________________________________________________________________

### 3. `src/app/src/plugin_catalog_model.cpp` (MODIFY — filter + diagnostic + new fields)

**Analog:** `src/app/src/plugin_catalog_model.cpp:348-432` (the `installedActions()` method body — read in full above).

**Skip-continue sites to add `VisibleInActionsList` gate** (`plugin_catalog_model.cpp:381-384`):

```cpp
for (PluginAction const& action : parsed->actions) {
    if (action.uuid.isEmpty() || action.name.isEmpty()) {
        continue; // an action with no id cannot be bound or routed   ← line 382
    }
    // ... icon resolution + QVariantMap build
}
```

Add the visibility filter BEFORE the UUID/Name check (so hidden actions are counted separately, not as errors):

```cpp
for (PluginAction const& action : parsed->actions) {
    if (!action.visibleInActionsList) {
        ++hiddenCount;           // intentionally hidden, not an error
        continue;
    }
    if (action.uuid.isEmpty() || action.name.isEmpty()) {
        ++errorSkipCount;        // malformed — this IS diagnostic-worthy
        continue;
    }
    // ... existing QVariantMap build
}
```

**QVariantMap build to extend** (`plugin_catalog_model.cpp:416-427`):

```cpp
QVariantMap m;
m.insert(QStringLiteral("pluginName"),    parsed->name);
m.insert(QStringLiteral("actionId"),      action.uuid);
m.insert(QStringLiteral("actionName"),    action.name);
m.insert(QStringLiteral("icon"),          iconUrl);
m.insert(QStringLiteral("propertyInspectorPath"),    action.propertyInspectorPath);
m.insert(QStringLiteral("propertyInspectorAbsPath"), piAbs);
m.insert(QStringLiteral("pluginUuid"),    entry);
m.insert(QStringLiteral("controllers"),  action.controllers);
out.append(m);
```

New fields follow exactly this `m.insert(QStringLiteral("key"), value)` line pattern. Add after the existing `controllers` line:

```cpp
m.insert(QStringLiteral("visibleInActionsList"), action.visibleInActionsList);
m.insert(QStringLiteral("stateCount"),           static_cast<int>(action.states.size()));
m.insert(QStringLiteral("disableAutomaticStates"), action.disableAutomaticStates);
m.insert(QStringLiteral("defaultSettings"),      QString::fromStdString(action.defaultSettings));
m.insert(QStringLiteral("affordanceMask"),       affordanceMask(action.controllers));
// Encoder block fields:
m.insert(QStringLiteral("encoderLayout"),        action.encoderBlock.layout);
```

______________________________________________________________________

### 4. Affordance normalizer free function (CREATE: `affordanceMask` in `plugin_manifest.hpp`)

**Analog:** `src/app/src/plugin_manifest.hpp:118,152` and `src/app/src/plugin_device_bridge.hpp:243,257` — the project's established pattern for `[[nodiscard]]` free functions in a named namespace, declared in `.hpp`, defined in `.cpp`.

**Declaration style to copy** (`plugin_manifest.hpp:118` + `plugin_device_bridge.hpp:243`):

```cpp
// plugin_manifest.hpp:118
[[nodiscard]] std::optional<PluginManifest> parsePluginManifest(QByteArray const& json);

// plugin_device_bridge.hpp:243
[[nodiscard]] GridCoord coordsForKeyIndex(std::uint8_t oneBasedKeyIndex,
                                          std::uint8_t keyCols) noexcept;
```

Place the new `affordanceMask` declaration in `plugin_manifest.hpp` inside `namespace ajazz::app` (same namespace as `PluginAction`), after the struct definitions and before `parsePluginManifest`:

```cpp
/// Bitmask of drop-target affordances derived from a Controllers QStringList.
/// Key=1, Dial=2, TouchZone=4.
/// "Knob" and "Encoder" both map to Dial; absent/[] defaults to Key only.
/// "Information" is ignored (not a physical drop surface).
enum class Affordance : int { Key = 1, Dial = 2, TouchZone = 4 };

[[nodiscard]] int affordanceMask(QStringList const& controllers) noexcept;
```

Defined in `plugin_manifest.cpp` (same file as `currentPlatformString`) following the same plain-body style — no class, no Q_OBJECT, pure logic.

______________________________________________________________________

### 5. `src/app/qml/ActionLibraryPane.qml` (MODIFY — add `controllers` + `affordanceMask` to model + MIME)

**Analog:** `src/app/qml/ActionLibraryPane.qml:50-81` (`_rebuild()` body) and `:192-201` (`Drag.mimeData`).

**Existing `_rebuild()` plugin row append** (`ActionLibraryPane.qml:72-80`):

```qml
for (let j = 0; j < actions.length; ++j) {
    const a = actions[j];
    actionModel.append({
        group: pluginGroup, actionLabel: a.actionName, kind: 0,
        iconName: "extension", actionId: a.actionId, pluginName: a.pluginName,
        iconUrl: a.icon || "", propertyInspectorPath: a.propertyInspectorPath || "",
        isPlugin: true, isHint: false
    });
}
```

Add `controllers: a.controllers || []` and `affordanceMask: a.affordanceMask || 0` to this `actionModel.append({...})` call. Mirror the `|| []` / `|| 0` defensive fallback already used for `iconUrl`.

The builtin-row append (`ActionLibraryPane.qml:54-58`) and hint-row append (`:66-70`) need `controllers: []` and `affordanceMask: 1` (builtins are key-capable) / `affordanceMask: 0` (hints are non-draggable).

**Existing `Drag.mimeData` to extend** (`ActionLibraryPane.qml:192-201`):

```qml
Drag.mimeData: ({
    "application/x-ajazz-action": JSON.stringify({
        actionKind: tile.kind,
        label: tile.actionLabel,
        iconName: tile.iconName,
        actionId: tile.actionId,
        iconUrl: tile.iconUrl,
        propertyInspectorPath: tile.propertyInspectorPath
    })
})
```

Add `affordanceMask: tile.affordanceMask` to this JSON object. Also add `required property int affordanceMask` to the `LibraryTile` component's required-property block (`:163-174`), following the same `required property int kind` style.

______________________________________________________________________

### 6. `src/app/src/application.cpp` (CREATE wiring — `profileChanged` → bridge)

**Analog:** `src/app/src/application.cpp:452-483` (existing `profileChanged` → repaint connections) and `:496-530` (the `#ifdef AJAZZ_HAVE_WEBSOCKETS` block with bridge wiring).

**Pattern to mirror** — the existing repaint connections at `application.cpp:452-455`:

```cpp
QObject::connect(m_profileController.get(),
                 &ProfileController::profileChanged,
                 m_streamDockControl.get(),
                 &StreamDockControlService::repaintFromProfile);
```

The new connection cannot use the same direct-slot form because `populateContextsForActivePage` requires a `deviceId` argument and must guard on `activeDeviceId().isEmpty()`. Mirror the lambda pattern already used at `application.cpp:501-502` and the `#ifdef AJAZZ_HAVE_WEBSOCKETS` block:

```cpp
// From application.cpp:501-502 (lambda accessor injection):
m_pluginBridge->setProfileAccessor(
    [this]() -> core::Profile const& { return m_profileController->activeProfile(); });
```

New connection goes AFTER the existing three `profileChanged` connections (lines 452-483) and INSIDE `#ifdef AJAZZ_HAVE_WEBSOCKETS` (after line 496), before the `deviceEvent` connection at line 507. This preserves the IN-02 ordering invariant (repaint fires before context registration). Pattern:

```cpp
// Wire profileChanged -> populateContextsForActivePage so a drag-drop
// binding registers an ActionContext in the bridge immediately (PLUGIN-19).
// Guard: no-op when no device is active (activeDeviceId empty at startup).
QObject::connect(m_profileController.get(),
                 &ProfileController::profileChanged,
                 m_pluginBridge.get(),
                 [this]() {
                     if (!m_pluginBridge->activeDeviceId().isEmpty()) {
                         m_pluginBridge->populateContextsForActivePage(
                             m_pluginBridge->activeDeviceId());
                     }
                 });
```

Prerequisite: `activeDeviceId()` must be exposed as a `[[nodiscard]] QString activeDeviceId() const noexcept` public accessor on `PluginDeviceBridge` (currently private `m_activeDeviceId`). Follow the same accessor style as `branding_service.hpp:74`: `[[nodiscard]] QString productName() const noexcept { return productName_; }`.

______________________________________________________________________

### 7. `src/app/src/debug_control_facade.cpp` (CREATE `plugin.installedActions` RPC)

**Analog:** `src/app/src/debug_control_facade.cpp:446-455` (`plugin.list`) and `:479-494` (`plugin.installFromFile`).

**`plugin.list` handler shape** (`debug_control_facade.cpp:446-455`):

```cpp
server.registerMethod("plugin.list", [&app](QJsonObject const&, QString& err) {
    auto* srv = app.pluginServer();
    if (srv == nullptr) {
        err = QStringLiteral("plugin server unavailable");
        return QJsonObject{};
    }
    return QJsonObject{{"listening",      srv->isListening()},
                       {"port",           static_cast<int>(srv->serverPort())},
                       {"connectedCount", srv->connectedPluginCount()}};
});
```

**`plugin.installFromFile` params-validation shape** (`debug_control_facade.cpp:480-494`):

```cpp
server.registerMethod(
    "plugin.installFromFile", [&app](QJsonObject const& params, QString& err) {
        auto* cat = app.pluginCatalog();
        if (cat == nullptr) {
            err = QStringLiteral("plugin catalog unavailable");
            return QJsonObject{};
        }
        QString const path = params.value("path").toString();
        if (path.isEmpty()) {
            err = QStringLiteral("require 'path'");
            return QJsonObject{};
        }
        // ... call + return QJsonObject{{"installed", result}}
    });
```

New `plugin.installedActions` RPC follows `plugin.list` shape (no params needed). Call `cat->installedActions()`, convert to `QJsonArray`, return:

```cpp
server.registerMethod("plugin.installedActions", [&app](QJsonObject const&, QString& err) {
    auto* cat = app.pluginCatalog();
    if (cat == nullptr) {
        err = QStringLiteral("plugin catalog unavailable");
        return QJsonObject{};
    }
    QVariantList const actions = cat->installedActions();
    QJsonArray arr;
    for (QVariant const& v : actions)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    return QJsonObject{{"count", static_cast<int>(actions.size())}, {"actions", arr}};
});
```

Place immediately after `plugin.list` (line 455), still inside `#ifdef AJAZZ_HAVE_WEBSOCKETS`.

______________________________________________________________________

### 8. `tests/unit/test_catalog_offline.cpp` + `test_plugin_manifest.cpp` (CREATE new TEST_CASEs)

**Analog:** `tests/unit/test_catalog_offline.cpp:225-310` and `tests/unit/test_plugin_manifest.cpp:68-159`.

**Catalog offline test skeleton** (`test_catalog_offline.cpp:225-310` — the fixture-layout + assertion pattern):

```cpp
TEST_CASE("CatalogOffline installedActions flattens manifest actions", "[catalog-offline]") {
    auto& app = qtApp();
    Q_UNUSED(app);

    EnvGuard sdGuard("ACC_STREAMDOCK_CATALOG_URL", "disabled");
    EnvGuard odGuard("ACC_OPENDECK_CATALOG_URL", "disabled");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    PluginCatalogModel::setPluginsDirOverride(pluginsDir);
    QStandardPaths::setTestModeEnabled(true);

    PluginCatalogModel model(nullptr);

    // Plant a .sdPlugin dir with an inline manifest JSON...
    QString const pluginDir = QDir(pluginsDir).filePath(QStringLiteral("com.example.demo.sdPlugin"));
    REQUIRE(QDir().mkpath(pluginDir));
    QByteArray const manifest = R"JSON({ ... })JSON";
    { QFile f(...); REQUIRE(f.open(QIODevice::WriteOnly)); f.write(manifest); }

    QVariantList const actions = model.installedActions();
    QStandardPaths::setTestModeEnabled(false);

    REQUIRE(actions.size() == N);
    auto const a0 = actions.at(0).toMap();
    CHECK(a0.value(QStringLiteral("fieldName")).toString() == expected);
    // ...

    PluginCatalogModel::setPluginsDirOverride(QString{});
}
```

New tests mirror this pattern. TEST_CASE titles are **ASCII-only** (CLAUDE.md Pitfall 6):

- `"CatalogOffline VisibleInActionsList false filters action"` — 3-action manifest, 1 hidden; `actions.size() == 2`
- `"CatalogOffline diagnostic hidden count vs error count"` — separate hiddenCount vs errorSkipCount
- `"CatalogOffline controllers in output map"` — `a0.value("controllers").toStringList()` check
- `"CatalogOffline defaultSettings in output map"` — non-empty JSON string for actions with Settings

**Manifest test skeleton** (`test_plugin_manifest.cpp:95-134` — fixture-file pattern):

```cpp
TEST_CASE("PluginManifestTest accepts elgato v6 plus ajazz extensions", "[plugin-manifest]") {
    SECTION("elgato_v6_keypad parses with Keypad controller") {
        auto const maybeManifest = parseFixture("elgato_v6_keypad.json");
        REQUIRE(maybeManifest.has_value());
        PluginManifest const& m = *maybeManifest;
        CHECK(m.name == "Keypad Test Plugin");
        // struct field assertions...
    }
}
```

New TEST_CASEs for Phase 28 use the same `parseFixture("filename.json")` helper and `SECTION` structure. Fixtures go in `tests/unit/fixtures/manifests/` matching the existing `kFixtureDir` constant.

New tests (ASCII names per CLAUDE.md):

- `"PluginManifestTest parses VisibleInActionsList false and absent"` `[plugin-manifest]`
- `"PluginManifestTest parses DisableAutomaticStates true and absent"` `[plugin-manifest]`
- `"PluginManifestTest parses Encoder block with TriggerDescription"` `[plugin-manifest]`
- `"PluginManifestTest parses default Settings as JSON string"` `[plugin-manifest]`
- `"PluginManifestTest parses state Name Title ShowTitle"` `[plugin-manifest]`
- `"PluginManifestTest affordanceMask empty controllers defaults to Key"` `[plugin-manifest]`
- `"PluginManifestTest affordanceMask Knob maps to Dial only"` `[plugin-manifest]`
- `"PluginManifestTest affordanceMask Encoder maps to Dial same as Knob"` `[plugin-manifest]`
- `"PluginManifestTest affordanceMask Keypad Knob maps to Key and Dial"` `[plugin-manifest]`
- `"PluginManifestTest affordanceMask SecondaryScreen maps to TouchZone only"` `[plugin-manifest]`
- `"PluginManifestTest affordanceMask Information only maps to zero"` `[plugin-manifest]`

______________________________________________________________________

## Shared Patterns

### QJsonObject nested-object extraction (all parser changes)

**Source:** `src/app/src/plugin_manifest.cpp:154-164`
**Apply to:** all new `Encoder` / `TriggerDescription` block parsing in `parseAction()`

```cpp
QJsonValue const nodejsVal = root.value(QStringLiteral("Nodejs"));
if (nodejsVal.isObject()) {
    m.nodejsVersion = nodejsVal.toObject().value(QStringLiteral("Version")).toString();
}
```

### QVariantMap field insertion (catalog model)

**Source:** `src/app/src/plugin_catalog_model.cpp:416-427`
**Apply to:** all new `m.insert(...)` calls in `installedActions()`

```cpp
m.insert(QStringLiteral("keyName"), value);
```

### `[[nodiscard]]` free-function declaration in namespace

**Source:** `src/app/src/plugin_manifest.hpp:118,152` / `src/app/src/plugin_device_bridge.hpp:243`
**Apply to:** `affordanceMask()` declaration in `plugin_manifest.hpp`

```cpp
[[nodiscard]] int affordanceMask(QStringList const& controllers) noexcept;
```

### `QObject::connect` lambda with guard

**Source:** `src/app/src/application.cpp:501-502` + the existing `profileChanged` connections at `:452-483`
**Apply to:** new `profileChanged` → `populateContextsForActivePage` connection in `application.cpp`

### DropArea `dragRejected` + errorAccent border

**Source:** `src/app/qml/components/KeyCell.qml:87-93,179-204`
**Apply to:** affordance gating in `EncoderDial.qml` `onEntered` and `TouchStripLane.qml` `onEntered`

### Debug RPC `server.registerMethod` lambda shape

**Source:** `src/app/src/debug_control_facade.cpp:446-455`
**Apply to:** new `plugin.installedActions` RPC

______________________________________________________________________

## No Analog Found

| File                                                                                                         | Role         | Data Flow | Reason                                                                                                                                                                                                                                                                      |
| ------------------------------------------------------------------------------------------------------------ | ------------ | --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Wave 0 fixture files (`tests/unit/fixtures/manifests/manifest_visibility.json`, `manifest_affordances.json`) | test fixture | —         | No fixtures for VisibleInActionsList or affordance tokens exist yet; must be authored. Use the in-test inline manifest JSON style from `test_catalog_offline.cpp:249-263` as the format guide, then extract to files per `test_plugin_manifest.cpp:kFixtureDir` convention. |

______________________________________________________________________

## Metadata

**Analog search scope:** `src/app/src/`, `src/app/qml/`, `src/app/qml/components/`, `tests/unit/`
**Files scanned:** 12
**Pattern extraction date:** 2026-05-31
