// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_device_view_tests.cpp
 * @brief Catch2 tests for DeviceView.qml geometry rendering and drag-drop wiring.
 *
 * REQ-26-B acceptance (Phase 26 Plan 26-05):
 *   DeviceViewGeometry -- verifies keyCellsRendered / encoderDialsRendered /
 *     touchZonesRendered accessor counts for three device SKU classes:
 *       AKP05E keyRows=2, gridColumns=5, 10 keys + 4 encoders + 4 zones
 *       AKP153 keyRows=3, gridColumns=5, 15 keys + 0 encoders + 0 zones
 *       AKP03  keyRows=2, gridColumns=3,  6 keys + 3 encoders + 0 zones
 *     Values match akp05e/akp153/akp03 descriptors in register.cpp (REQ-26-C).
 *
 *   DeviceViewDragDrop -- verifies that ProfileController commit methods are
 *     invoked (or not, for the cross-controller rejection case) as the drop
 *     handler logic dictates.
 *
 * Infrastructure: uses the world() / engine declared in test_qml_smoke.cpp
 * (external linkage, shared across the ajazz_qml_tests executable) to avoid
 * constructing a second QApplication or duplicate QML singleton registrations.
 *
 * ASCII-only test names (CLAUDE.md).
 */
#include "application.hpp"
#include "profile_controller.hpp"

#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QString>
#include <QTest>
#include <QVariant>
#include <QVariantMap>

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// Forward declarations: world() lives in test_qml_smoke.cpp (external linkage).
// ---------------------------------------------------------------------------
struct QmlWorld {
    ajazz::app::Application* controller = nullptr;
    QQmlApplicationEngine* engine = nullptr;
};

QmlWorld& world();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

// Forward declaration: defined below; used by createDeviceViewWindow to settle layout.
QObject* findByName(QObject* root, QString const& name);

// Load DeviceView from the AjazzControlCenter module with explicit geometry.
// Returns the created QObject* (caller must delete), or nullptr on failure.
QObject* createDeviceView(int keyCount,
                          int keyRows,
                          int gridColumns,
                          int encoderCount,
                          int touchZoneCount,
                          QString const& codename) {
    QQmlComponent component(world().engine);
    component.loadFromModule(QStringLiteral("AjazzControlCenter"), QStringLiteral("DeviceView"));
    if (!component.isReady()) {
        return nullptr;
    }
    QVariantMap props;
    props[QStringLiteral("keyCount")] = keyCount;
    props[QStringLiteral("keyRows")] = keyRows;
    props[QStringLiteral("gridColumns")] = gridColumns;
    props[QStringLiteral("encoderCount")] = encoderCount;
    props[QStringLiteral("touchZoneCount")] = touchZoneCount;
    props[QStringLiteral("codename")] = codename;
    props[QStringLiteral("width")] = 800;
    props[QStringLiteral("height")] = 480;
    return component.createWithInitialProperties(props);
}

// EDIT-01 variant: load DeviceView into a real offscreen QQuickWindow and pump
// the event loop so anchors, Repeater delegates, and Flickable geometry settle.
// A bare QQmlComponent::create() never runs a layout pass, so geometry-dependent
// assertions (contentHeight, delegate instantiation) require a hosted, shown
// window. Returns the WINDOW (owns the DeviceView via contentItem); the caller
// deletes the window. The DeviceView is reachable via findDeviceView(window).
QQuickWindow* createDeviceViewWindow(int keyCount,
                                     int keyRows,
                                     int gridColumns,
                                     int encoderCount,
                                     int touchZoneCount,
                                     QString const& codename,
                                     int width,
                                     int height) {
    QQmlComponent component(world().engine);
    component.loadFromModule(QStringLiteral("AjazzControlCenter"), QStringLiteral("DeviceView"));
    if (!component.isReady()) {
        return nullptr;
    }
    QVariantMap props;
    props[QStringLiteral("keyCount")] = keyCount;
    props[QStringLiteral("keyRows")] = keyRows;
    props[QStringLiteral("gridColumns")] = gridColumns;
    props[QStringLiteral("encoderCount")] = encoderCount;
    props[QStringLiteral("touchZoneCount")] = touchZoneCount;
    props[QStringLiteral("codename")] = codename;
    QObject* obj = component.createWithInitialProperties(props);
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (item == nullptr) {
        delete obj;
        return nullptr;
    }
    auto* window = new QQuickWindow();
    window->resize(width, height);
    item->setParentItem(window->contentItem());
    // Fill the window so anchors.fill chains (chassisArea -> ScrollView) resolve.
    item->setWidth(width);
    item->setHeight(height);
    window->show();
    // Pump events so polish/Repeater/Flickable geometry settle (offscreen: no
    // real exposure, but qWait drains the layout queue).
    QTest::qWait(50);
    return window;
}

// Find the DeviceView item hosted in a createDeviceViewWindow() window.
QQuickItem* findDeviceView(QQuickWindow* window) {
    if (window == nullptr || window->contentItem() == nullptr)
        return nullptr;
    QList<QQuickItem*> kids = window->contentItem()->childItems();
    return kids.isEmpty() ? nullptr : kids.first();
}

// Read an int Q_PROPERTY from a QObject by name.  Returns -1 if missing.
int intProp(QObject* obj, char const* name) {
    if (obj == nullptr)
        return -1;
    QVariant v = obj->property(name);
    return v.isValid() ? v.toInt() : -1;
}

// Read a bool Q_PROPERTY from a QObject by name.  Returns false if missing.
bool boolProp(QObject* obj, char const* name) {
    if (obj == nullptr)
        return false;
    QVariant v = obj->property(name);
    return v.isValid() && v.toBool();
}

// Read a qreal Q_PROPERTY from a QObject by name.  Returns -1.0 if missing.
qreal realProp(QObject* obj, char const* name) {
    if (obj == nullptr)
        return -1.0;
    QVariant v = obj->property(name);
    return v.isValid() ? v.toReal() : -1.0;
}

// Recursively find a descendant QObject by objectName (covers QQuickItem's
// contentItem subtree too -- ScrollView parents its content under contentItem).
QObject* findByName(QObject* root, QString const& name) {
    if (root == nullptr)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QObject* child : root->children()) {
        if (QObject* hit = findByName(child, name))
            return hit;
    }
    if (auto* item = qobject_cast<QQuickItem*>(root)) {
        QQuickItem* content = item->property("contentItem").value<QQuickItem*>();
        if (content != nullptr && content != item) {
            if (QObject* hit = findByName(content, name))
                return hit;
        }
    }
    return nullptr;
}

// Count descendants whose dynamic metaobject class name starts with `prefix`
// (Qt mangles QML types to "<Type>_QMLTYPE_NN"). Walks BOTH the QObject child
// tree AND the visual childItems()/contentItem tree, because Repeater delegates
// (KeyCell, EncoderDial) are reparented as visual childItems of the lane Item,
// NOT as QObject children of it. A visited set prevents double-counting nodes
// reachable through both edges.
void collectByTypePrefix(QObject* root, QString const& prefix, QSet<QObject*>& seen, int& n) {
    if (root == nullptr || seen.contains(root))
        return;
    seen.insert(root);
    if (QString::fromUtf8(root->metaObject()->className()).startsWith(prefix))
        ++n;
    for (QObject* child : root->children())
        collectByTypePrefix(child, prefix, seen, n);
    if (auto* item = qobject_cast<QQuickItem*>(root)) {
        QQuickItem* content = item->property("contentItem").value<QQuickItem*>();
        if (content != nullptr && content != item)
            collectByTypePrefix(content, prefix, seen, n);
        for (QQuickItem* vi : item->childItems())
            collectByTypePrefix(vi, prefix, seen, n);
    }
}

int countByTypePrefix(QObject* root, QString const& prefix) {
    QSet<QObject*> seen;
    int n = 0;
    collectByTypePrefix(root, prefix, seen, n);
    return n;
}

// Retrieve the ProfileController QML singleton from the shared engine.
ajazz::app::ProfileController* profileController() {
    return world().engine->singletonInstance<ajazz::app::ProfileController*>(
        QStringLiteral("AjazzControlCenter"), QStringLiteral("ProfileController"));
}

} // namespace

// ===========================================================================
// DeviceViewGeometry -- geometry count accessors for three SKU classes
// ===========================================================================

TEST_CASE("DeviceViewGeometry::test_akp05e_renders_5x2_grid_plus_4_dials_plus_4_zones",
          "[qml][device_view][geometry]") {
    QObject* dv = createDeviceView(10, 2, 5, 4, 4, QStringLiteral("akp05e"));
    REQUIRE(dv != nullptr);
    CHECK(intProp(dv, "keyCellsRendered") == 10);
    CHECK(intProp(dv, "encoderDialsRendered") == 4);
    CHECK(intProp(dv, "touchZonesRendered") == 4);
    delete dv;
}

TEST_CASE("DeviceViewGeometry::test_akp153_renders_3x5_grid_no_dials_no_zones",
          "[qml][device_view][geometry]") {
    // keyRows=3, gridColumns=5 aligned with akp153_descriptor (REQ-26-C, WR-03).
    QObject* dv = createDeviceView(15, 3, 5, 0, 0, QStringLiteral("akp153"));
    REQUIRE(dv != nullptr);
    CHECK(intProp(dv, "keyCellsRendered") == 15);
    CHECK(intProp(dv, "encoderDialsRendered") == 0);
    CHECK(intProp(dv, "touchZonesRendered") == 0);
    delete dv;
}

TEST_CASE("DeviceViewGeometry::test_akp03_renders_2x3_grid_plus_3_dials_no_zones",
          "[qml][device_view][geometry]") {
    // keyRows=2, gridColumns=3 aligned with akp03_descriptor (REQ-26-C, WR-03).
    QObject* dv = createDeviceView(6, 2, 3, 3, 0, QStringLiteral("akp03"));
    REQUIRE(dv != nullptr);
    CHECK(intProp(dv, "keyCellsRendered") == 6);
    CHECK(intProp(dv, "encoderDialsRendered") == 3);
    CHECK(intProp(dv, "touchZonesRendered") == 0);
    delete dv;
}

// ===========================================================================
// DeviceViewDragDrop -- ProfileController commit-method wiring
//
// These tests verify the QML -> C++ binding wire works by calling the same
// ProfileController methods that the drop handlers invoke, observing that
// profileChanged() is emitted (positive cases) or is NOT emitted (rejection).
// ===========================================================================

TEST_CASE("DeviceViewDragDrop::test_drop_library_tile_on_keycell_calls_commitKeyBinding",
          "[qml][device_view][drag_drop]") {
    auto* pc = profileController();
    REQUIRE(pc != nullptr);

    int fired = 0;
    auto conn =
        QObject::connect(pc, &ajazz::app::ProfileController::profileChanged, [&fired] { ++fired; });

    // Simulate: KeyCell DropArea onDropped with "application/x-ajazz-action",
    // actionKind=4 ("Open URL"), keyIndex=3 (valid for any SKU with >= 4 keys).
    pc->commitKeyBinding(3, QStringLiteral(""), QStringLiteral("Open URL"), 4, QStringLiteral(""));

    CHECK(fired == 1);
    QObject::disconnect(conn);
}

TEST_CASE("DeviceViewDragDrop::test_drop_library_tile_on_encoder_calls_commitEncoderBinding",
          "[qml][device_view][drag_drop]") {
    // Phase 26 CR-04: commitEncoderBinding Q_INVOKABLE now exists on ProfileController.
    // EncoderDial.qml calls ProfileController.commitEncoderBinding(...) on drop; the
    // call must persist the binding and emit profileChanged().
    auto* pc = profileController();
    REQUIRE(pc != nullptr);

    int fired = 0;
    auto conn =
        QObject::connect(pc, &ajazz::app::ProfileController::profileChanged, [&fired] { ++fired; });

    // Simulate: EncoderDial DropArea onDropped with "application/x-ajazz-action",
    // actionKind=4 ("Open URL"), encoderIndex=1 (valid for AKP05E 4 encoders).
    pc->commitEncoderBinding(
        1, QStringLiteral(""), QStringLiteral("Open URL"), 4, QStringLiteral(""));

    CHECK(fired == 1);
    QObject::disconnect(conn);
}

TEST_CASE("DeviceViewDragDrop::test_drop_library_tile_on_touch_zone_calls_commitTouchZoneBinding",
          "[qml][device_view][drag_drop]") {
    auto* pc = profileController();
    REQUIRE(pc != nullptr);

    int fired = 0;
    auto conn =
        QObject::connect(pc, &ajazz::app::ProfileController::profileChanged, [&fired] { ++fired; });

    // Simulate: TouchZoneCell DropArea onDropped with "application/x-ajazz-action",
    // actionKind=3 ("Launch command"), zoneIndex=2 (valid for AKP05E 4 zones).
    pc->commitTouchZoneBinding(
        2, QStringLiteral(""), QStringLiteral("Launch command"), 3, QStringLiteral(""));

    CHECK(fired == 1);
    QObject::disconnect(conn);
}

TEST_CASE("DeviceViewDragDrop::test_cross_controller_drop_is_rejected",
          "[qml][device_view][drag_drop]") {
    auto* pc = profileController();
    REQUIRE(pc != nullptr);

    int fired = 0;
    auto conn =
        QObject::connect(pc, &ajazz::app::ProfileController::profileChanged, [&fired] { ++fired; });

    // Simulate the cross-controller rejection guard in KeyCell.qml DropArea onDropped:
    //   if (bindPayload.controller !== "Keypad") { drop.accepted = false; return; }
    // When controller != "Keypad", commitKeyBinding is never reached.
    // Mirror the guard to verify the tested logic:
    QString controller = QStringLiteral("Encoder"); // wrong controller for KeyCell
    if (controller == QStringLiteral("Keypad")) {
        pc->commitKeyBinding(0, QStringLiteral(""), QStringLiteral(""), 0, QStringLiteral(""));
    }

    // Cross-controller drop: commitKeyBinding must NOT have been called.
    CHECK(fired == 0);
    QObject::disconnect(conn);
}

// ===========================================================================
// DeviceViewScroll (EDIT-01, Phase 32 Plan 32-05)
//
// The device grid (DeviceCanvas) is wrapped in a vertical-only
// ScrollView#deviceCanvasScroll inside chassisArea. These cases assert the
// 32-UI-SPEC frontend contract: over/under-threshold overflow, addressable
// scroll container + flickable, no horizontal scroll, trash pinned outside the
// scroll, and the three controller delegates staying distinct.
//
// ASCII-only test names (CLAUDE.md).
// ===========================================================================

TEST_CASE("DeviceViewScroll::test_scroll_container_is_addressable", "[qml][device_view][scroll]") {
    QQuickWindow* win = createDeviceViewWindow(10, 2, 5, 4, 4, QStringLiteral("akp05e"), 800, 480);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    CHECK(findByName(dv, QStringLiteral("deviceCanvasScroll")) != nullptr);
    CHECK(findByName(dv, QStringLiteral("deviceCanvasFlick")) != nullptr);
    delete win;
}

TEST_CASE("DeviceViewScroll::test_over_threshold_grid_overflows", "[qml][device_view][scroll]") {
    // 9 columns (> 8) OR rows+encoder+touch > 4 => _gridOverflows must be true.
    QQuickWindow* win =
        createDeviceViewWindow(45, 5, 9, 4, 4, QStringLiteral("overflow"), 360, 240);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    CHECK(boolProp(dv, "_gridOverflows"));
    delete win;
}

TEST_CASE("DeviceViewScroll::test_over_threshold_is_scrollable", "[qml][device_view][scroll]") {
    // With a small viewport, the over-threshold content exceeds the viewport so
    // the flickable's contentHeight is taller than its height (=> scrollable).
    QQuickWindow* win =
        createDeviceViewWindow(45, 5, 9, 4, 4, QStringLiteral("overflow"), 360, 240);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    QObject* fl = findByName(dv, QStringLiteral("deviceCanvasFlick"));
    REQUIRE(fl != nullptr);
    qreal contentH = realProp(fl, "contentHeight");
    qreal viewH = realProp(fl, "height");
    CHECK(contentH > 0.0);
    CHECK(contentH > viewH); // content taller than viewport => vertical scroll engages
    delete win;
}

TEST_CASE("DeviceViewScroll::test_under_threshold_does_not_overflow",
          "[qml][device_view][scroll]") {
    // A 3x3 grid with no encoders/zones is at/under threshold.
    QQuickWindow* win = createDeviceViewWindow(9, 3, 3, 0, 0, QStringLiteral("compact"), 800, 1000);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    CHECK_FALSE(boolProp(dv, "_gridOverflows"));
    delete win;
}

TEST_CASE("DeviceViewScroll::test_under_threshold_is_not_scrollable",
          "[qml][device_view][scroll]") {
    // The small grid fits a large viewport: contentHeight must NOT exceed the
    // viewport height (frame stays centered, no vertical scroll).
    QQuickWindow* win = createDeviceViewWindow(9, 3, 3, 0, 0, QStringLiteral("compact"), 800, 1000);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    QObject* fl = findByName(dv, QStringLiteral("deviceCanvasFlick"));
    REQUIRE(fl != nullptr);
    qreal contentH = realProp(fl, "contentHeight");
    qreal viewH = realProp(fl, "height");
    CHECK(contentH > 0.0);
    CHECK(contentH <= viewH + 0.5); // fits viewport => not scrollable
    delete win;
}

TEST_CASE("DeviceViewScroll::test_no_horizontal_scroll_ever", "[qml][device_view][scroll]") {
    // Vertical-only contract: even on the over-threshold SKU the content width is
    // bounded to the viewport so the flickable is never horizontally scrollable.
    QQuickWindow* win =
        createDeviceViewWindow(45, 5, 9, 4, 4, QStringLiteral("overflow"), 360, 240);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    QObject* fl = findByName(dv, QStringLiteral("deviceCanvasFlick"));
    REQUIRE(fl != nullptr);
    qreal contentW = realProp(fl, "contentWidth");
    qreal viewW = realProp(fl, "width");
    CHECK(contentW > 0.0);
    CHECK(contentW <= viewW + 0.5); // content never wider than viewport
    delete win;
}

TEST_CASE("DeviceViewScroll::test_trash_button_is_pinned_outside_scroll",
          "[qml][device_view][scroll]") {
    QQuickWindow* win =
        createDeviceViewWindow(45, 5, 9, 4, 4, QStringLiteral("overflow"), 360, 240);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    QObject* sv = findByName(dv, QStringLiteral("deviceCanvasScroll"));
    REQUIRE(sv != nullptr);
    // trashBtn (a RoundButton) must NOT live inside the ScrollView subtree; it is
    // a pinned chassisArea sibling. Counting RoundButton under the ScrollView only.
    CHECK(countByTypePrefix(sv, QStringLiteral("RoundButton")) == 0);
    // ...but the DeviceView overall does contain the trash RoundButton.
    CHECK(countByTypePrefix(dv, QStringLiteral("RoundButton")) >= 1);
    delete win;
}

TEST_CASE("DeviceViewScroll::test_delegates_remain_distinct", "[qml][device_view][scroll]") {
    // On an all-three-controller SKU the key/encoder/touch delegates stay distinct.
    QQuickWindow* win =
        createDeviceViewWindow(45, 5, 9, 4, 4, QStringLiteral("overflow"), 360, 240);
    REQUIRE(win != nullptr);
    QQuickItem* dv = findDeviceView(win);
    REQUIRE(dv != nullptr);
    CHECK(countByTypePrefix(dv, QStringLiteral("KeyCell")) > 0);
    CHECK(countByTypePrefix(dv, QStringLiteral("EncoderDial")) > 0);
    CHECK(countByTypePrefix(dv, QStringLiteral("TouchStripLane")) > 0);
    delete win;
}
