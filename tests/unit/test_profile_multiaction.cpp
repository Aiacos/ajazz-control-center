// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_profile_multiaction.cpp
 * @brief Unit tests for ProfileController multi-action verbs (PLUGIN-23).
 *
 * Covers:
 *  - appendKeyAction: adds an action WITHOUT clearing existing entries.
 *  - reorderKeyAction: moves an action within onPress; out-of-range is a no-op.
 *  - removeKeyActionAt: erases onPress[pos]; last action leave empty onPress.
 *  - activeKeyBindings: exposes the full onPress list (per-key actionList field).
 *  - Round-trip: 2-action onPress survives save/load cycle.
 *
 * All test titles ASCII-only (CLAUDE.md cross-platform ctest filter rule).
 * Tag: [multi-action][PLUGIN-23]
 */
#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_io.hpp"
#include "profile_controller.hpp"
#include "qt_app_fixture.hpp"

#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

/// Seed a minimal profile (id + name + device) into a controller via a temp file.
void seedProfile(app::ProfileController& ctrl, QTemporaryDir& tmpDir, char const* id) {
    core::Profile base{};
    base.id = id;
    base.name = "Multi-Action Test";
    base.deviceCodename = "akp05e";
    QString const path = tmpDir.filePath(QStringLiteral("profile.json"));
    core::writeProfileToDisk(std::filesystem::path{path.toStdString()}, base);
    ctrl.loadProfile(path);
}

/// Return the onPress vector for keyIndex from the active profile.
std::vector<core::Action> const& onPressFor(app::ProfileController const& ctrl, uint16_t idx) {
    return ctrl.activeProfile().keys.at(idx).onPress;
}

/// Pull the "actionList" from activeKeyBindings() for the given key index.
QVariantList actionListForKey(app::ProfileController const& ctrl, int keyIndex) {
    auto const kb = ctrl.activeKeyBindings();
    for (auto const& v : kb) {
        auto const m = v.toMap();
        if (m.value(QStringLiteral("index")).toInt() == keyIndex) {
            return m.value(QStringLiteral("actionList")).toList();
        }
    }
    return {};
}

} // namespace

// ===========================================================================
// PLUGIN-23: appendKeyAction
// ===========================================================================

TEST_CASE("ProfileController: appendKeyAction keeps existing action and adds second",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-append-01");

    // Commit the first action via the existing commitKeyBinding path.
    ctrl.commitKeyBinding(0,
                          QStringLiteral(""),
                          QStringLiteral("First"),
                          static_cast<int>(core::ActionKind::Plugin),
                          QStringLiteral("{}"),
                          QStringLiteral("com.test.action1"));
    REQUIRE(onPressFor(ctrl, 0).size() == 1);
    CHECK(onPressFor(ctrl, 0)[0].id == "com.test.action1");

    // appendKeyAction MUST NOT overwrite — it appends.
    ctrl.appendKeyAction(0,
                         static_cast<int>(core::ActionKind::Plugin),
                         QStringLiteral("{}"),
                         QStringLiteral("com.test.action2"));

    REQUIRE(onPressFor(ctrl, 0).size() == 2);
    CHECK(onPressFor(ctrl, 0)[0].id == "com.test.action1");
    CHECK(onPressFor(ctrl, 0)[1].id == "com.test.action2");
}

TEST_CASE("ProfileController: appendKeyAction on empty key creates first action",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-append-02");

    ctrl.appendKeyAction(3,
                         static_cast<int>(core::ActionKind::OpenUrl),
                         QStringLiteral("{\"url\":\"https://example.com\"}"),
                         QStringLiteral(""));

    REQUIRE(ctrl.activeProfile().keys.count(3) == 1);
    auto const& op = onPressFor(ctrl, 3);
    REQUIRE(op.size() == 1);
    CHECK(op[0].kind == core::ActionKind::OpenUrl);
    CHECK(op[0].settingsJson == "{\"url\":\"https://example.com\"}");
}

TEST_CASE("ProfileController: appendKeyAction out-of-range keyIndex is a no-op",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-append-oor");

    // Negative index.
    ctrl.appendKeyAction(-1,
                         static_cast<int>(core::ActionKind::Plugin),
                         QStringLiteral("{}"),
                         QStringLiteral("com.test.x"));
    CHECK(ctrl.activeProfile().keys.empty());

    // Too-large index (> uint16_t max - 1 = 65534).
    ctrl.appendKeyAction(65535,
                         static_cast<int>(core::ActionKind::Plugin),
                         QStringLiteral("{}"),
                         QStringLiteral("com.test.x"));
    CHECK(ctrl.activeProfile().keys.empty());
}

// ===========================================================================
// PLUGIN-23: reorderKeyAction
// ===========================================================================

TEST_CASE("ProfileController: reorderKeyAction moves action from pos 0 to pos 1",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-reorder-01");

    ctrl.commitKeyBinding(1, {}, QStringLiteral("A"), 0, {}, QStringLiteral("com.a"));
    ctrl.appendKeyAction(1, 0, {}, QStringLiteral("com.b"));
    ctrl.appendKeyAction(1, 0, {}, QStringLiteral("com.c"));
    REQUIRE(onPressFor(ctrl, 1).size() == 3);

    // Move index 0 -> index 2.
    ctrl.reorderKeyAction(1, 0, 2);

    auto const& op = onPressFor(ctrl, 1);
    REQUIRE(op.size() == 3);
    CHECK(op[0].id == "com.b");
    CHECK(op[1].id == "com.c");
    CHECK(op[2].id == "com.a");
}

TEST_CASE("ProfileController: reorderKeyAction out-of-range pos is a no-op",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-reorder-oor");

    ctrl.commitKeyBinding(0, {}, QStringLiteral("X"), 0, {}, QStringLiteral("com.x"));
    REQUIRE(onPressFor(ctrl, 0).size() == 1);

    // Both out-of-range: fromPos -1.
    ctrl.reorderKeyAction(0, -1, 0);
    REQUIRE(onPressFor(ctrl, 0).size() == 1);
    CHECK(onPressFor(ctrl, 0)[0].id == "com.x");

    // toPos out-of-range (== size).
    ctrl.reorderKeyAction(0, 0, 1);
    REQUIRE(onPressFor(ctrl, 0).size() == 1);
    CHECK(onPressFor(ctrl, 0)[0].id == "com.x");
}

// ===========================================================================
// PLUGIN-23: removeKeyActionAt
// ===========================================================================

TEST_CASE("ProfileController: removeKeyActionAt removes correct action",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-remove-01");

    ctrl.commitKeyBinding(2, {}, {}, 0, {}, QStringLiteral("com.a"));
    ctrl.appendKeyAction(2, 0, {}, QStringLiteral("com.b"));
    ctrl.appendKeyAction(2, 0, {}, QStringLiteral("com.c"));
    REQUIRE(onPressFor(ctrl, 2).size() == 3);

    // Remove middle action.
    ctrl.removeKeyActionAt(2, 1);
    auto const& op = onPressFor(ctrl, 2);
    REQUIRE(op.size() == 2);
    CHECK(op[0].id == "com.a");
    CHECK(op[1].id == "com.c");
}

TEST_CASE("ProfileController: removeKeyActionAt last action leaves empty onPress",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-remove-last");

    ctrl.commitKeyBinding(0, {}, QStringLiteral("Solo"), 0, {}, QStringLiteral("com.solo"));
    REQUIRE(onPressFor(ctrl, 0).size() == 1);

    ctrl.removeKeyActionAt(0, 0);
    REQUIRE(onPressFor(ctrl, 0).empty());
}

TEST_CASE("ProfileController: removeKeyActionAt out-of-range pos is a no-op",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-remove-oor");

    ctrl.commitKeyBinding(0, {}, {}, 0, {}, QStringLiteral("com.x"));
    REQUIRE(onPressFor(ctrl, 0).size() == 1);

    ctrl.removeKeyActionAt(0, -1); // negative
    REQUIRE(onPressFor(ctrl, 0).size() == 1);

    ctrl.removeKeyActionAt(0, 1); // == size -> out of range
    REQUIRE(onPressFor(ctrl, 0).size() == 1);
    CHECK(onPressFor(ctrl, 0)[0].id == "com.x");
}

// ===========================================================================
// PLUGIN-23: activeKeyBindings exposes full onPress list (actionList)
// ===========================================================================

TEST_CASE("ProfileController: activeKeyBindings includes actionList with full onPress",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-list-01");

    ctrl.commitKeyBinding(0,
                          QStringLiteral("/tmp/icon.png"),
                          QStringLiteral("First"),
                          static_cast<int>(core::ActionKind::Plugin),
                          {},
                          QStringLiteral("com.a"));
    ctrl.appendKeyAction(0,
                         static_cast<int>(core::ActionKind::OpenUrl),
                         QStringLiteral("{\"url\":\"https://b.com\"}"),
                         QStringLiteral(""));

    QVariantList const list = actionListForKey(ctrl, 0);
    REQUIRE(list.size() == 2);

    auto const a0 = list[0].toMap();
    CHECK(a0.value(QStringLiteral("actionId")).toString() == QStringLiteral("com.a"));
    CHECK(a0.value(QStringLiteral("actionKind")).toInt() ==
          static_cast<int>(core::ActionKind::Plugin));

    auto const a1 = list[1].toMap();
    CHECK(a1.value(QStringLiteral("actionKind")).toInt() ==
          static_cast<int>(core::ActionKind::OpenUrl));
}

TEST_CASE("ProfileController: activeKeyBindings back-compat - top-level fields unchanged",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-ma-compat-01");

    ctrl.commitKeyBinding(5,
                          QStringLiteral("/img.png"),
                          QStringLiteral("Lbl"),
                          static_cast<int>(core::ActionKind::RunCommand),
                          QStringLiteral("{\"cmd\":\"ls\"}"),
                          QStringLiteral("com.cmd.action"));

    auto const kb = ctrl.activeKeyBindings();
    bool found = false;
    for (auto const& v : kb) {
        auto const m = v.toMap();
        if (m.value(QStringLiteral("index")).toInt() == 5) {
            found = true;
            // Back-compat top-level fields must still be present.
            CHECK(m.value(QStringLiteral("iconSource")).toString() == QStringLiteral("/img.png"));
            CHECK(m.value(QStringLiteral("label")).toString() == QStringLiteral("Lbl"));
            CHECK(m.value(QStringLiteral("actionKind")).toInt() ==
                  static_cast<int>(core::ActionKind::RunCommand));
            CHECK(m.value(QStringLiteral("actionId")).toString() ==
                  QStringLiteral("com.cmd.action"));
        }
    }
    CHECK(found);
}

// ===========================================================================
// PLUGIN-23: 2-action onPress round-trips through save/load
// ===========================================================================

TEST_CASE("ProfileController: 2-action onPress survives save and load",
          "[multi-action][PLUGIN-23]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    QString const path = tmpDir.filePath(QStringLiteral("ma_roundtrip.json"));

    // --- Originator ---
    {
        core::Profile base{};
        base.id = "test-ma-rt-01";
        base.name = "Multi-Action RT";
        base.deviceCodename = "akp05e";
        core::writeProfileToDisk(std::filesystem::path{path.toStdString()}, base);

        app::ProfileController orig(nullptr);
        orig.loadProfile(path);

        orig.commitKeyBinding(7,
                              QStringLiteral("/a.png"),
                              QStringLiteral("A"),
                              static_cast<int>(core::ActionKind::Plugin),
                              QStringLiteral("{\"x\":1}"),
                              QStringLiteral("com.plugin.a"));
        orig.appendKeyAction(7,
                             static_cast<int>(core::ActionKind::OpenUrl),
                             QStringLiteral("{\"url\":\"https://b.com\"}"),
                             QStringLiteral(""));
        REQUIRE(onPressFor(orig, 7).size() == 2);

        orig.saveProfile(path);
    }

    // --- Fresh controller ---
    app::ProfileController fresh(nullptr);
    bool loaded = false;
    QObject::connect(
        &fresh, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    fresh.loadProfile(path);
    REQUIRE(loaded);

    auto const& p = fresh.activeProfile();
    auto const it = p.keys.find(7);
    REQUIRE(it != p.keys.end());
    auto const& op = it->second.onPress;
    REQUIRE(op.size() == 2);
    CHECK(op[0].id == "com.plugin.a");
    CHECK(op[0].kind == core::ActionKind::Plugin);
    CHECK(op[0].settingsJson == "{\"x\":1}");
    CHECK(op[1].kind == core::ActionKind::OpenUrl);
    CHECK(op[1].settingsJson == "{\"url\":\"https://b.com\"}");
}

// ===========================================================================
// Move-to-another-button: swapKeyBindings moves the WHOLE Binding
// ===========================================================================

TEST_CASE("ProfileController: swapKeyBindings moves a multi-action chain intact to an empty key",
          "[move][swap-key]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-swapkey-move");

    // Build a 2-action chain on key 0 (the exact case the old QML two-commit
    // workaround collapsed to a single action on a move).
    ctrl.commitKeyBinding(0,
                          QStringLiteral("/tmp/a.png"),
                          QStringLiteral("Alpha"),
                          static_cast<int>(core::ActionKind::Plugin),
                          QStringLiteral("{\"k\":1}"),
                          QStringLiteral("com.test.a"));
    ctrl.appendKeyAction(0,
                         static_cast<int>(core::ActionKind::OpenUrl),
                         QStringLiteral("{\"url\":\"https://b\"}"),
                         QStringLiteral(""));
    REQUIRE(onPressFor(ctrl, 0).size() == 2);

    // Move key 0 -> empty key 7.
    ctrl.swapKeyBindings(0, 7);

    // Source key is now empty (moved away); destination has the FULL chain +
    // the visual state (icon/label), proving whole-Binding relocation.
    CHECK(onPressFor(ctrl, 0).empty());
    REQUIRE(onPressFor(ctrl, 7).size() == 2);
    CHECK(onPressFor(ctrl, 7)[0].id == "com.test.a");
    CHECK(onPressFor(ctrl, 7)[0].settingsJson == "{\"k\":1}");
    CHECK(onPressFor(ctrl, 7)[1].kind == core::ActionKind::OpenUrl);
    REQUIRE(ctrl.activeProfile().keys.at(7).state.imagePath.has_value());
    CHECK(*ctrl.activeProfile().keys.at(7).state.imagePath == "/tmp/a.png");
    REQUIRE(ctrl.activeProfile().keys.at(7).state.text.has_value());
    CHECK(*ctrl.activeProfile().keys.at(7).state.text == "Alpha");
}

TEST_CASE("ProfileController: swapKeyBindings exchanges two occupied keys", "[move][swap-key]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-swapkey-swap");

    ctrl.commitKeyBinding(
        1, {}, {}, static_cast<int>(core::ActionKind::Plugin), {}, QStringLiteral("com.one"));
    ctrl.commitKeyBinding(
        4, {}, {}, static_cast<int>(core::ActionKind::Plugin), {}, QStringLiteral("com.four"));

    ctrl.swapKeyBindings(1, 4);

    REQUIRE(onPressFor(ctrl, 1).size() == 1);
    REQUIRE(onPressFor(ctrl, 4).size() == 1);
    CHECK(onPressFor(ctrl, 1)[0].id == "com.four");
    CHECK(onPressFor(ctrl, 4)[0].id == "com.one");
}

TEST_CASE("ProfileController: swapKeyBindings out-of-range / self-swap are no-ops",
          "[move][swap-key]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-swapkey-oor");

    ctrl.commitKeyBinding(
        2, {}, {}, static_cast<int>(core::ActionKind::Plugin), {}, QStringLiteral("com.keep"));

    ctrl.swapKeyBindings(2, 2);     // self-swap
    ctrl.swapKeyBindings(2, -1);    // negative dst
    ctrl.swapKeyBindings(70000, 2); // > uint16 range

    REQUIRE(onPressFor(ctrl, 2).size() == 1);
    CHECK(onPressFor(ctrl, 2)[0].id == "com.keep");
}

// ===========================================================================
// Dial PI: activeEncoderBindings resolves the dial's bound action
// ===========================================================================

TEST_CASE("ProfileController: activeEncoderBindings exposes the bound dial action",
          "[dial][encoder-bindings]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-enc-bindings");

    ctrl.commitEncoderBinding(2,
                              QStringLiteral("/tmp/dial.png"),
                              QStringLiteral("Volume"),
                              static_cast<int>(core::ActionKind::Plugin),
                              QStringLiteral("{}"),
                              QStringLiteral("com.test.dial"));

    auto const eb = ctrl.activeEncoderBindings();
    bool found = false;
    for (auto const& v : eb) {
        auto const m = v.toMap();
        if (m.value(QStringLiteral("index")).toInt() == 2) {
            found = true;
            CHECK(m.value(QStringLiteral("actionId")).toString() ==
                  QStringLiteral("com.test.dial"));
            CHECK(m.value(QStringLiteral("label")).toString() == QStringLiteral("Volume"));
            CHECK(m.value(QStringLiteral("actionKind")).toInt() ==
                  static_cast<int>(core::ActionKind::Plugin));
        }
    }
    CHECK(found);
}

// ===========================================================================
// T037 (002): uninstall-while-bound — clearBindingsForPlugin reverts owned
// key + encoder bindings to unbound, leaves others intact, no crash.
// ===========================================================================
TEST_CASE("ProfileController: clearBindingsForPlugin reverts owned key+encoder, keeps others",
          "[multi-action][uninstall]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmp, "clr-uuid");

    // Key 0 -> a child action of the victim plugin; key 1 -> a different plugin.
    ctrl.commitKeyBinding(0, {}, QStringLiteral("V"), 0, {}, QStringLiteral("com.victim.action"));
    ctrl.commitKeyBinding(1, {}, QStringLiteral("K"), 0, {}, QStringLiteral("com.keep.action"));
    // Encoder 2 -> a victim dial action; encoder 3 -> a different plugin.
    ctrl.commitEncoderBinding(
        2, {}, QStringLiteral("VD"), 0, {}, QStringLiteral("com.victim.dial"));
    ctrl.commitEncoderBinding(3, {}, QStringLiteral("KD"), 0, {}, QStringLiteral("com.keep.dial"));

    // Uninstalling "com.victim" clears its owned key + encoder (2 bindings).
    int const cleared = ctrl.clearBindingsForPlugin(QStringLiteral("com.victim"));
    CHECK(cleared == 2);

    // Victim bindings reverted to unbound; the other plugin's bindings survive.
    CHECK(ctrl.activeProfile().keys.count(0) == 0);
    CHECK(ctrl.activeProfile().keys.count(1) == 1);
    CHECK(ctrl.activeProfile().encoders.count(2) == 0);
    CHECK(ctrl.activeProfile().encoders.count(3) == 1);

    // Idempotent: a second clear removes nothing; an empty uuid is a safe no-op.
    CHECK(ctrl.clearBindingsForPlugin(QStringLiteral("com.victim")) == 0);
    CHECK(ctrl.clearBindingsForPlugin(QString{}) == 0);
}

// ===========================================================================
// Volume dial: commitEncoderVolume wires CW=up / CCW=down / press=mute to the
// built-in system.volume action (the Linux-working volume control).
// ===========================================================================
TEST_CASE("ProfileController: commitEncoderVolume wires directional volume chains",
          "[multi-action][volume]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmp, "vol-uuid");

    ctrl.commitEncoderVolume(2);

    REQUIRE(ctrl.activeProfile().encoders.count(2) == 1);
    auto const& eb = ctrl.activeProfile().encoders.at(2);

    constexpr char const* kVol = "com.hotspot.streamdock.system.volume";
    // CW -> volume up.
    REQUIRE(eb.onCw.size() == 1);
    CHECK(eb.onCw.front().kind == core::ActionKind::Plugin);
    CHECK(eb.onCw.front().id == kVol);
    CHECK(eb.onCw.front().settingsJson.find("up") != std::string::npos);
    // CCW -> volume down.
    REQUIRE(eb.onCcw.size() == 1);
    CHECK(eb.onCcw.front().id == kVol);
    CHECK(eb.onCcw.front().settingsJson.find("down") != std::string::npos);
    // Press -> mute.
    REQUIRE(eb.onPress.size() == 1);
    CHECK(eb.onPress.front().id == kVol);
    CHECK(eb.onPress.front().settingsJson.find("Mute") != std::string::npos);
    // Segment label.
    CHECK(eb.state.text.value_or("") == "Volume");

    // Out-of-range index is a safe no-op.
    ctrl.commitEncoderVolume(-1);
    CHECK(ctrl.activeProfile().encoders.count(2) == 1);
}

// ===========================================================================
// Pages / Folders (Elgato-parity Delta A)
// ===========================================================================

TEST_CASE("ProfileController: createFolderOnKey binds OpenFolder + seeds BackToParent",
          "[pages][delta-a]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-folder-create");

    QString const pageId = ctrl.createFolderOnKey(2, QStringLiteral("Lights"));
    REQUIRE_FALSE(pageId.isEmpty());

    // The root key 2 now holds an OpenFolder action targeting the new page.
    REQUIRE(ctrl.activeProfile().keys.count(2) == 1);
    auto const& onPress = onPressFor(ctrl, 2);
    REQUIRE(onPress.size() == 1);
    CHECK(onPress[0].kind == core::ActionKind::OpenFolder);
    CHECK(onPress[0].settingsJson.find(pageId.toStdString()) != std::string::npos);

    // The new page exists and carries a BackToParent key at index 0.
    auto const pit = ctrl.activeProfile().pages.find(pageId.toStdString());
    REQUIRE(pit != ctrl.activeProfile().pages.end());
    REQUIRE(pit->second.keys.count(0) == 1);
    CHECK(pit->second.keys.at(0).onPress.front().kind == core::ActionKind::BackToParent);

    // activeKeyBindings surfaces the folder marker for the canvas.
    auto const kb = ctrl.activeKeyBindings();
    bool sawFolder = false;
    for (auto const& v : kb) {
        auto const m = v.toMap();
        if (m.value("index").toInt() == 2) {
            CHECK(m.value("isFolder").toBool());
            CHECK(m.value("folderTarget").toString() == pageId);
            sawFolder = true;
        }
    }
    CHECK(sawFolder);
}

TEST_CASE("ProfileController: enterFolder routes edits to the folder page, not root",
          "[pages][delta-a]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-folder-edit");

    // Bind a root key, then make a folder on another key and enter it.
    ctrl.commitKeyBinding(
        0, {}, QStringLiteral("RootKey"), 0, QStringLiteral("{}"), QStringLiteral("com.root"));
    QString const pageId = ctrl.createFolderOnKey(1, QStringLiteral("Sub"));
    REQUIRE(ctrl.activePageId() == QStringLiteral("root"));

    ctrl.enterFolder(pageId);
    CHECK(ctrl.activePageId() == pageId);
    CHECK(ctrl.activePageName() == QStringLiteral("Sub"));
    // Breadcrumb is root -> Sub.
    REQUIRE(ctrl.pageBreadcrumb().size() == 2);
    CHECK(ctrl.pageBreadcrumb().at(0).toMap().value("id").toString() == QStringLiteral("root"));
    CHECK(ctrl.pageBreadcrumb().at(1).toMap().value("id").toString() == pageId);

    // Committing now writes to the FOLDER page, leaving root key 0 intact.
    ctrl.commitKeyBinding(
        3, {}, QStringLiteral("InFolder"), 0, QStringLiteral("{}"), QStringLiteral("com.sub"));
    auto const& pg = ctrl.activeProfile().pages.at(pageId.toStdString());
    REQUIRE(pg.keys.count(3) == 1);
    CHECK(pg.keys.at(3).onPress.front().id == "com.sub");
    // Root key 0 unchanged; root has no key 3.
    CHECK(ctrl.activeProfile().keys.at(0).onPress.front().id == "com.root");
    CHECK(ctrl.activeProfile().keys.count(3) == 0);

    // activeKeyBindings now reflects the FOLDER page: key 3 (com.sub) is present,
    // and the key-0 entry is the folder's seeded BackToParent (kind 6) -- NOT the
    // root's com.root plugin action, proving the view switched pages.
    bool sawFolderKey3 = false, sawFolderKey0Back = false;
    for (auto const& v : ctrl.activeKeyBindings()) {
        auto const m = v.toMap();
        int const idx = m.value("index").toInt();
        if (idx == 3)
            sawFolderKey3 = true;
        if (idx == 0) {
            sawFolderKey0Back =
                m.value("actionKind").toInt() == static_cast<int>(core::ActionKind::BackToParent);
        }
    }
    CHECK(sawFolderKey3);
    CHECK(sawFolderKey0Back);
}

TEST_CASE("ProfileController: goBackPage returns to root", "[pages][delta-a]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-folder-back");

    QString const pageId = ctrl.createFolderOnKey(0, QStringLiteral("F"));
    ctrl.enterFolder(pageId);
    REQUIRE(ctrl.activePageId() == pageId);

    ctrl.goBackPage();
    CHECK(ctrl.activePageId() == QStringLiteral("root"));
    // goBackPage at root is a no-op.
    ctrl.goBackPage();
    CHECK(ctrl.activePageId() == QStringLiteral("root"));
}

TEST_CASE("ProfileController: loading a profile resets page nav to root", "[pages][delta-a]") {
    ajazz::tests::qtApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    app::ProfileController ctrl(nullptr);
    seedProfile(ctrl, tmpDir, "test-folder-reset");

    QString const pageId = ctrl.createFolderOnKey(0, QStringLiteral("F"));
    ctrl.enterFolder(pageId);
    REQUIRE(ctrl.activePageId() == pageId);

    // Reload the profile from disk: nav must snap back to root (loadProfile calls
    // resetPageNav, so a freshly-loaded profile always opens at the top level).
    ctrl.loadProfile(tmpDir.filePath(QStringLiteral("profile.json")));
    CHECK(ctrl.activePageId() == QStringLiteral("root"));
}
