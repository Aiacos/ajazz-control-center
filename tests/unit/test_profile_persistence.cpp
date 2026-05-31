// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_profile_persistence.cpp
 * @brief Round-trip persistence tests for ProfileController (PROFILE-01).
 *
 * Covers:
 *  - defaultProfilePath() path resolution + traversal sanitization
 *  - commitKeyBinding() mutating m_profile.keys + emitting profileChanged
 *  - commit -> saveProfile -> fresh-controller loadProfile round-trip equality
 *    for keys, encoders, touch/key KeyState, and pages
 *  - load -> repaintFromProfile proving BAT+ULEND per bound key (PROFILE-01 repaint)
 *
 * All test titles are ASCII-only (CLAUDE.md cross-platform ctest filter rule).
 * Tag: [profile-persistence]
 */
#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_io.hpp"
#include "fixtures/mock_transport.hpp"
#include "profile_controller.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

// For streamdeck make factory
#include "ajazz/streamdeck/streamdeck.hpp"

using namespace ajazz;

namespace {

// ---------------------------------------------------------------------------
// Helper: fresh ProfileController that is NOT registered as the QML singleton
// (we just need its persistence logic).
// ---------------------------------------------------------------------------
struct ProfileCtrlFixture {
    std::unique_ptr<app::ProfileController> ctrl;

    ProfileCtrlFixture() { ctrl = std::make_unique<app::ProfileController>(nullptr); }
};

// ---------------------------------------------------------------------------
// Akp05E mock device helper (mirrors test_stream_dock_control_service.cpp).
// ---------------------------------------------------------------------------
core::DeviceDescriptor makeAkp05eDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (persistence-test)";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.hasClock = false;
    return d;
}

core::DeviceId makeAkp05eId() {
    core::DeviceId id{};
    id.vendorId = 0x0300;
    id.productId = 0x3004;
    id.serial = "TEST-16-02";
    return id;
}

struct Akp05Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

Akp05Fixture makeAkp05Fixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* obs = owned.get();
    std::vector<std::uint8_t> verResponse(20, 0);
    verResponse[0] = 0x01;
    std::string const vstr = "V3.AKP05E.01.007";
    for (std::size_t i = 0; i < vstr.size() && i + 1 < verResponse.size(); ++i) {
        verResponse[i + 1] = static_cast<std::uint8_t>(vstr[i]);
    }
    obs->enqueueReadFeature(std::move(verResponse));
    auto dev = streamdeck::makeAkp05WithTransport(
        makeAkp05eDescriptor(), makeAkp05eId(), std::move(owned));
    return Akp05Fixture{std::move(dev), obs};
}

void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

} // namespace

// ===========================================================================
// PROFILE-01: defaultProfilePath() resolution
// ===========================================================================

TEST_CASE("ProfileController: defaultProfilePath resolves under AppDataLocation/profiles/",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    app::ProfileController ctrl(nullptr);
    QString const path = ctrl.defaultProfilePath(QStringLiteral("my-profile-id"));

    // Must end in ".json" and contain "/profiles/my-profile-id.json".
    CHECK(path.endsWith(QStringLiteral(".json")));
    CHECK(path.contains(QStringLiteral("/profiles/")));
    CHECK(path.endsWith(QStringLiteral("my-profile-id.json")));

    // Must be rooted under AppDataLocation.
    QString const appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    REQUIRE_FALSE(appData.isEmpty());
    CHECK(path.startsWith(appData));
}

TEST_CASE("ProfileController: defaultProfilePath sanitizes traversal attempts",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    app::ProfileController ctrl(nullptr);

    // A malicious id with path traversal must not escape the profiles dir.
    QString const path = ctrl.defaultProfilePath(QStringLiteral("../etc/passwd"));

    // The result must NOT contain "../".
    CHECK_FALSE(path.contains(QStringLiteral("..")));
    // Must still end in ".json".
    CHECK(path.endsWith(QStringLiteral(".json")));

    // Must remain inside AppDataLocation.
    QString const appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        CHECK(path.startsWith(appData));
    }
}

// ===========================================================================
// PROFILE-01: commitKeyBinding mutates m_profile.keys and emits profileChanged
// ===========================================================================

TEST_CASE("ProfileController: commitKeyBinding mutates keys and emits profileChanged",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    app::ProfileController ctrl(nullptr);

    bool changed = false;
    QObject::connect(
        &ctrl, &app::ProfileController::profileChanged, [&changed]() { changed = true; });

    // Commit a URL-open binding to key index 1.
    ctrl.commitKeyBinding(1,
                          QStringLiteral("/tmp/icon.png"),
                          QStringLiteral("Hi"),
                          static_cast<int>(core::ActionKind::OpenUrl),
                          QStringLiteral("{\"url\":\"https://x\"}"));

    // Signal must have fired.
    REQUIRE(changed);

    // Inspect the mutated profile.
    auto const& profile = ctrl.activeProfile();
    auto const it = profile.keys.find(1);
    REQUIRE(it != profile.keys.end());

    auto const& binding = it->second;
    CHECK(binding.state.imagePath == std::optional<std::string>{"/tmp/icon.png"});
    CHECK(binding.state.text == std::optional<std::string>{"Hi"});
    REQUIRE(binding.onPress.size() == 1);
    CHECK(binding.onPress[0].kind == core::ActionKind::OpenUrl);
    CHECK(binding.onPress[0].settingsJson == std::string{"{\"url\":\"https://x\"}"});
}

TEST_CASE("ProfileController: commitKeyBinding persists the plugin actionId into Action::id",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    app::ProfileController ctrl(nullptr);

    // Bind a plugin action: the dotted UUID must land in Action::id so the
    // plugin host can route key events to it (Workstream B).
    ctrl.commitKeyBinding(4,
                          QStringLiteral(""),
                          QStringLiteral("Toggle Mute"),
                          static_cast<int>(core::ActionKind::Plugin),
                          QStringLiteral("{}"),
                          QStringLiteral("com.elgato.obs.togglemute"));

    auto const& profile = ctrl.activeProfile();
    auto const it = profile.keys.find(4);
    REQUIRE(it != profile.keys.end());
    REQUIRE(it->second.onPress.size() == 1);
    CHECK(it->second.onPress[0].kind == core::ActionKind::Plugin);
    CHECK(it->second.onPress[0].id == std::string{"com.elgato.obs.togglemute"});

    // The 5-arg overload (no actionId) must still work and leave id empty.
    ctrl.commitKeyBinding(5,
                          QStringLiteral(""),
                          QStringLiteral("URL"),
                          static_cast<int>(core::ActionKind::OpenUrl),
                          QStringLiteral("{\"url\":\"https://x\"}"));
    auto const it5 = ctrl.activeProfile().keys.find(5);
    REQUIRE(it5 != ctrl.activeProfile().keys.end());
    REQUIRE(it5->second.onPress.size() == 1);
    CHECK(it5->second.onPress[0].id.empty());
}

TEST_CASE("ProfileController: commitKeyBinding with empty iconPath/label stores nullopt",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    app::ProfileController ctrl(nullptr);
    ctrl.commitKeyBinding(2,
                          QStringLiteral(""),
                          QStringLiteral(""),
                          static_cast<int>(core::ActionKind::Plugin),
                          QStringLiteral("{}"));

    auto const& profile = ctrl.activeProfile();
    auto const it = profile.keys.find(2);
    REQUIRE(it != profile.keys.end());
    CHECK_FALSE(it->second.state.imagePath.has_value());
    CHECK_FALSE(it->second.state.text.has_value());
}

// ===========================================================================
// PROFILE-01: commit -> save -> fresh-controller load round-trip for keys
// ===========================================================================

TEST_CASE("ProfileController: key binding round-trips through save/load",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const savePath = tmpDir.filePath(QStringLiteral("profile.json"));

    // --- Originator ---
    {
        app::ProfileController orig(nullptr);
        // Set a minimal profile id/name so validateProfileJson accepts it.
        // We need to seed the internal m_profile; do this by loading a minimal
        // profile from disk first, OR by directly testing through the controller's
        // save path. Since m_profile starts with empty id/name (which fails
        // validateProfileJson), we must either set them or test only the round-trip
        // contract (load a pre-built profile, commit on top, save).
        //
        // Approach: write a minimal base profile using the core serializer,
        // load it into orig, then commit the binding on top.
        core::Profile base{};
        base.id = "test-rt-key-01";
        base.name = "RT Test";
        base.deviceCodename = "akp05e";
        auto const baseJson = core::profileToJson(base);

        // Write base to disk, load into controller.
        {
            std::filesystem::path const fsPath = savePath.toStdString();
            core::writeProfileToDisk(fsPath, base);
        }
        orig.loadProfile(savePath);

        // Commit a binding.
        orig.commitKeyBinding(1,
                              QStringLiteral("/tmp/icon.png"),
                              QStringLiteral("Hi"),
                              static_cast<int>(core::ActionKind::OpenUrl),
                              QStringLiteral("{\"url\":\"https://x\"}"));

        // Save.
        orig.saveProfile(savePath);
    }

    // --- Fresh controller ---
    app::ProfileController fresh(nullptr);
    bool loaded = false;
    QObject::connect(
        &fresh, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    fresh.loadProfile(savePath);
    REQUIRE(loaded);

    auto const& p = fresh.activeProfile();
    auto const it = p.keys.find(1);
    REQUIRE(it != p.keys.end());
    auto const& b = it->second;
    CHECK(b.state.imagePath == std::optional<std::string>{"/tmp/icon.png"});
    CHECK(b.state.text == std::optional<std::string>{"Hi"});
    REQUIRE(b.onPress.size() == 1);
    CHECK(b.onPress[0].kind == core::ActionKind::OpenUrl);
    CHECK(b.onPress[0].settingsJson == std::string{"{\"url\":\"https://x\"}"});
}

// ===========================================================================
// PROFILE-01: encoder binding round-trip (LOCKED decision 3 - programmatic)
// ===========================================================================

TEST_CASE("ProfileController: encoder binding round-trips through save/load",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const savePath = tmpDir.filePath(QStringLiteral("enc_profile.json"));

    // Build a profile with encoder[0] programmatically.
    core::Profile p{};
    p.id = "test-rt-enc-01";
    p.name = "Encoder RT Test";
    p.deviceCodename = "akp05e";

    core::EncoderBinding eb{};
    eb.onCw.push_back(core::Action{core::ActionKind::OpenUrl, "", "{\"url\":\"https://cw\"}"});
    eb.onCcw.push_back(core::Action{core::ActionKind::OpenUrl, "", "{\"url\":\"https://ccw\"}"});
    eb.onPress.push_back(core::Action{core::ActionKind::Sleep, "", "{}"});
    eb.state.text = "ENC0";
    eb.state.background = core::Rgb{10, 20, 30};
    p.encoders[0] = std::move(eb);

    // Write directly, load into a fresh controller.
    {
        std::filesystem::path const fsPath = savePath.toStdString();
        core::writeProfileToDisk(fsPath, p);
    }

    app::ProfileController ctrl(nullptr);
    bool loaded = false;
    QObject::connect(
        &ctrl, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    ctrl.loadProfile(savePath);
    REQUIRE(loaded);

    auto const& loaded_p = ctrl.activeProfile();
    auto const it = loaded_p.encoders.find(0);
    REQUIRE(it != loaded_p.encoders.end());
    auto const& leb = it->second;
    REQUIRE(leb.onCw.size() == 1);
    CHECK(leb.onCw[0].settingsJson == std::string{"{\"url\":\"https://cw\"}"});
    REQUIRE(leb.onCcw.size() == 1);
    CHECK(leb.onCcw[0].settingsJson == std::string{"{\"url\":\"https://ccw\"}"});
    REQUIRE(leb.onPress.size() == 1);
    CHECK(leb.onPress[0].kind == core::ActionKind::Sleep);
    CHECK(leb.state.text == std::optional<std::string>{"ENC0"});
    REQUIRE(leb.state.background.has_value());
    CHECK(leb.state.background->r == 10);
    CHECK(leb.state.background->g == 20);
    CHECK(leb.state.background->b == 30);
}

// ===========================================================================
// PROFILE-01: pages round-trip
// ===========================================================================

TEST_CASE("ProfileController: pages round-trip through save/load",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const savePath = tmpDir.filePath(QStringLiteral("pages_profile.json"));

    core::Profile p{};
    p.id = "test-rt-pages-01";
    p.name = "Pages RT Test";
    p.deviceCodename = "akp05e";

    core::ProfilePage pg{};
    pg.id = "folderA";
    pg.name = "Folder A";
    core::Binding kb{};
    kb.state.text = "Page key";
    pg.keys[3] = std::move(kb);
    p.pages["folderA"] = std::move(pg);

    {
        std::filesystem::path const fsPath = savePath.toStdString();
        core::writeProfileToDisk(fsPath, p);
    }

    app::ProfileController ctrl(nullptr);
    bool loaded = false;
    QObject::connect(
        &ctrl, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    ctrl.loadProfile(savePath);
    REQUIRE(loaded);

    auto const& lp = ctrl.activeProfile();
    auto const pit = lp.pages.find("folderA");
    REQUIRE(pit != lp.pages.end());
    CHECK(pit->second.name == "Folder A");
    auto const kit = pit->second.keys.find(3);
    REQUIRE(kit != pit->second.keys.end());
    CHECK(kit->second.state.text == std::optional<std::string>{"Page key"});
}

// ===========================================================================
// PROFILE-01: saveActiveProfile uses defaultProfilePath (mkpath creates dir)
// ===========================================================================

TEST_CASE("ProfileController: saveActiveProfile creates parent dir and saves",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    // Use a fresh controller with a profile that has a non-trivial id.
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const seedPath = tmpDir.filePath(QStringLiteral("seed.json"));

    core::Profile base{};
    base.id = "save-active-test-id";
    base.name = "Save Active Test";
    base.deviceCodename = "akp05e";
    {
        std::filesystem::path const fsPath = seedPath.toStdString();
        core::writeProfileToDisk(fsPath, base);
    }

    app::ProfileController ctrl(nullptr);
    ctrl.loadProfile(seedPath);
    ctrl.commitKeyBinding(3,
                          QStringLiteral("/img.png"),
                          QStringLiteral("Save test"),
                          static_cast<int>(core::ActionKind::RunCommand),
                          QStringLiteral("{}"));

    bool savedOk = false;
    QObject::connect(&ctrl, &app::ProfileController::profileSaved, [&savedOk](QString const&) {
        savedOk = true;
    });
    bool saveFailed = false;
    QObject::connect(&ctrl, &app::ProfileController::saveFailed, [&saveFailed](QString const&) {
        saveFailed = true;
    });

    // saveActiveProfile() must resolve the default path (creates the profiles dir).
    ctrl.saveActiveProfile();

    CHECK(savedOk);
    CHECK_FALSE(saveFailed);

    // The saved file must be readable and field-equal.
    QString const defaultPath = ctrl.defaultProfilePath(QStringLiteral("save-active-test-id"));
    REQUIRE_FALSE(defaultPath.isEmpty());

    app::ProfileController fresh(nullptr);
    bool loaded = false;
    QObject::connect(
        &fresh, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    fresh.loadProfile(defaultPath);
    REQUIRE(loaded);

    auto const& lp = fresh.activeProfile();
    auto const it = lp.keys.find(3);
    REQUIRE(it != lp.keys.end());
    CHECK(it->second.state.imagePath == std::optional<std::string>{"/img.png"});
    CHECK(it->second.state.text == std::optional<std::string>{"Save test"});
}

// ===========================================================================
// PROFILE-01: repaint-on-load -- loaded profile with >= 2 bound keys
//             triggers BAT+ULEND per key via StreamDockControlService.
// ===========================================================================

TEST_CASE("ProfileController: loaded profile with 2 bound keys repaints via "
          "StreamDockControlService (PROFILE-01 repaint-on-load)",
          "[profile-persistence][PROFILE-01]") {
    ajazz::tests::qtApp();

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const savePath = tmpDir.filePath(QStringLiteral("repaint_profile.json"));

    // Build a profile with 2 keys, each having a background colour.
    core::Profile p{};
    p.id = "repaint-test-id";
    p.name = "Repaint Test";
    p.deviceCodename = "akp05e";
    {
        core::Binding b1{};
        b1.state.background = core::Rgb{255, 0, 0};
        p.keys[0] = std::move(b1);
    }
    {
        core::Binding b2{};
        b2.state.background = core::Rgb{0, 0, 255};
        p.keys[1] = std::move(b2);
    }
    {
        std::filesystem::path const fsPath = savePath.toStdString();
        core::writeProfileToDisk(fsPath, p);
    }

    // Build a MockTransport-backed Akp05 device.
    auto fx = makeAkp05Fixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    // Build the control service with a profile accessor that returns our loaded profile.
    app::ProfileController ctrl(nullptr);

    // Wire the service: on profileChanged, repaint.
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&ctrl]() -> core::Profile const& { return ctrl.activeProfile(); },
        nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    // Connect profileChanged -> repaintFromProfile (mirrors Application wiring).
    QObject::connect(&ctrl,
                     &app::ProfileController::profileChanged,
                     &svc,
                     &app::StreamDockControlService::repaintFromProfile);

    std::size_t const writeCountAfterOpen = obs->writeCount();

    // Load the saved profile -- this emits profileChanged -> repaintFromProfile.
    ctrl.loadProfile(savePath);
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    // Count BAT headers and ULEND commits for the 2 bound keys.
    std::size_t batCount = 0;
    std::size_t ulendCount = 0;
    for (std::size_t i = writeCountAfterOpen; i < writes.size(); ++i) {
        auto const& pkt = writes[i];
        if (pkt.size() >= 8 && pkt[5] == 0x42 && pkt[6] == 0x41 && pkt[7] == 0x54) {
            ++batCount;
        }
        if (pkt.size() >= 10 && pkt[5] == 0x55 && pkt[6] == 0x4c && pkt[7] == 0x45 &&
            pkt[8] == 0x4e && pkt[9] == 0x44) {
            ++ulendCount;
        }
    }
    // 2 keys -> >= 2 BAT headers and >= 2 ULEND commits.
    CHECK(batCount >= 2);
    CHECK(ulendCount >= 2);
}

// ===========================================================================
// PLUGIN-19: encoder + touch-zone binding round-trip -- actionId persistence
// ===========================================================================

TEST_CASE("ProfilePersistence encoder binding persists plugin actionId",
          "[profile-persistence][PLUGIN-19]") {
    ajazz::tests::qtApp();

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const savePath = tmpDir.filePath(QStringLiteral("enc_actionid.json"));

    // --- Originator: commit via ProfileController with a non-empty actionId ---
    {
        // Seed a base profile so saveProfile() validates id/name.
        core::Profile base{};
        base.id = "test-enc-aid-01";
        base.name = "Encoder ActionId Test";
        base.deviceCodename = "akp05e";
        {
            std::filesystem::path const fsPath = savePath.toStdString();
            core::writeProfileToDisk(fsPath, base);
        }
        app::ProfileController orig(nullptr);
        orig.loadProfile(savePath);

        // Drop a plugin action onto encoder 0 with a known actionId.
        // This is the 6-arg form the fixed EncoderDial.qml now emits.
        orig.commitEncoderBinding(0,
                                  QStringLiteral(""),
                                  QStringLiteral("CPU Usage"),
                                  static_cast<int>(core::ActionKind::Plugin),
                                  QStringLiteral("{}"),
                                  QStringLiteral("com.x.action"));
        orig.saveProfile(savePath);
    }

    // --- Fresh controller: reload and check encoder[0].onPress.id ---
    app::ProfileController fresh(nullptr);
    bool loaded = false;
    QObject::connect(
        &fresh, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    fresh.loadProfile(savePath);
    REQUIRE(loaded);

    auto const& p = fresh.activeProfile();
    auto const it = p.encoders.find(0);
    REQUIRE(it != p.encoders.end());
    REQUIRE(it->second.onPress.size() == 1);
    // The actionId must survive save/load -- this is the PLUGIN-19 routable
    // persistence contract. If the 5-arg regression returns (id empty), this CHECK fails.
    CHECK(it->second.onPress[0].id == std::string{"com.x.action"});
    CHECK(it->second.onPress[0].kind == core::ActionKind::Plugin);
}

TEST_CASE("ProfilePersistence touch zone binding persists plugin actionId",
          "[profile-persistence][PLUGIN-19]") {
    ajazz::tests::qtApp();

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    QString const savePath = tmpDir.filePath(QStringLiteral("zone_actionid.json"));

    // --- Originator ---
    {
        core::Profile base{};
        base.id = "test-zone-aid-01";
        base.name = "TouchZone ActionId Test";
        base.deviceCodename = "akp05e";
        {
            std::filesystem::path const fsPath = savePath.toStdString();
            core::writeProfileToDisk(fsPath, base);
        }
        app::ProfileController orig(nullptr);
        orig.loadProfile(savePath);

        // Drop a plugin action onto touch zone 1 with a known actionId.
        // This is the 6-arg form the fixed TouchStripLane.qml now emits.
        orig.commitTouchZoneBinding(1,
                                    QStringLiteral(""),
                                    QStringLiteral("Zone Label"),
                                    static_cast<int>(core::ActionKind::Plugin),
                                    QStringLiteral("{}"),
                                    QStringLiteral("com.x.zone"));
        orig.saveProfile(savePath);
    }

    // --- Fresh controller ---
    app::ProfileController fresh(nullptr);
    bool loaded = false;
    QObject::connect(
        &fresh, &app::ProfileController::profileChanged, [&loaded]() { loaded = true; });
    fresh.loadProfile(savePath);
    REQUIRE(loaded);

    auto const& p = fresh.activeProfile();
    auto const it = p.touchZones.find(1);
    REQUIRE(it != p.touchZones.end());
    REQUIRE(it->second.onTap.size() == 1);
    // The actionId must survive save/load. If the 5-arg regression returns
    // (id empty), this CHECK fails (PLUGIN-19 round-trip guard).
    CHECK(it->second.onTap[0].id == std::string{"com.x.zone"});
    CHECK(it->second.onTap[0].kind == core::ActionKind::Plugin);
}

// ---------------------------------------------------------------------------
// Multi-profile library (Workstream D)
//
// qt_app_fixture enables QStandardPaths test mode, so the profiles dir is an
// isolated test location. Tests use unique device codenames to avoid colliding
// with each other inside the shared test-mode AppDataLocation.
// ---------------------------------------------------------------------------

TEST_CASE("ProfileController: createProfile indexes a device-scoped profile",
          "[profile-persistence][profile-library]") {
    ajazz::tests::qtApp();
    app::ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-d-create");
    QString const id = ctrl.createProfile(QStringLiteral("My Profile"), dev);

    REQUIRE_FALSE(id.isEmpty());
    CHECK(ctrl.activeProfileId() == id);
    CHECK(ctrl.activeProfileName() == QStringLiteral("My Profile"));
    CHECK(ctrl.knownProfileIds().contains(id));
    CHECK(ctrl.profileNameFor(id) == QStringLiteral("My Profile"));

    // profilesForDevice returns {id, name} for this device.
    QVariantList const list = ctrl.profilesForDevice(dev);
    bool found = false;
    for (auto const& v : list) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id) {
            found = true;
            CHECK(v.toMap().value(QStringLiteral("name")).toString() ==
                  QStringLiteral("My Profile"));
        }
    }
    CHECK(found);

    ctrl.deleteProfile(id); // cleanup
}

TEST_CASE("ProfileController: loadProfileById switches between profiles",
          "[profile-persistence][profile-library]") {
    ajazz::tests::qtApp();
    app::ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-d-switch");
    QString const a = ctrl.createProfile(QStringLiteral("A"), dev);
    // Give profile A a distinctive key binding and persist it.
    ctrl.commitKeyBinding(0,
                          QStringLiteral(""),
                          QStringLiteral("AKey"),
                          static_cast<int>(core::ActionKind::KeyPress),
                          QStringLiteral("{}"));
    ctrl.saveActiveProfile();

    QString const b = ctrl.createProfile(QStringLiteral("B"), dev);
    REQUIRE(a != b);
    CHECK(ctrl.activeProfileId() == b);

    // Switch back to A by id: its binding must come back.
    ctrl.loadProfileById(a);
    CHECK(ctrl.activeProfileId() == a);

    QVariantList const kb = ctrl.activeKeyBindings();
    bool sawAKey = false;
    for (auto const& v : kb) {
        auto const m = v.toMap();
        if (m.value(QStringLiteral("index")).toInt() == 0) {
            sawAKey = (m.value(QStringLiteral("label")).toString() == QStringLiteral("AKey"));
        }
    }
    CHECK(sawAKey);

    ctrl.deleteProfile(a);
    ctrl.deleteProfile(b);
}

TEST_CASE("ProfileController: rename and duplicate update the library",
          "[profile-persistence][profile-library]") {
    ajazz::tests::qtApp();
    app::ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-d-rename");
    QString const id = ctrl.createProfile(QStringLiteral("Orig"), dev);

    ctrl.renameActiveProfile(QStringLiteral("Renamed"));
    CHECK(ctrl.activeProfileName() == QStringLiteral("Renamed"));
    CHECK(ctrl.profileNameFor(id) == QStringLiteral("Renamed"));

    // Duplicate the active profile under a new name -> new id, copy active.
    QString const dupe = ctrl.duplicateProfile(QString{}, QStringLiteral("Copy"));
    REQUIRE_FALSE(dupe.isEmpty());
    CHECK(dupe != id);
    CHECK(ctrl.activeProfileId() == dupe);
    CHECK(ctrl.profileNameFor(dupe) == QStringLiteral("Copy"));

    ctrl.deleteProfile(id);
    ctrl.deleteProfile(dupe);
}

TEST_CASE("ProfileController: deleting the active profile activates a replacement",
          "[profile-persistence][profile-library]") {
    ajazz::tests::qtApp();
    app::ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-d-delete");
    QString const only = ctrl.createProfile(QStringLiteral("Only"), dev);
    CHECK(ctrl.activeProfileId() == only);

    // Deleting the only profile for the device activates a fresh "Default".
    ctrl.deleteProfile(only);
    CHECK_FALSE(ctrl.knownProfileIds().contains(only));
    CHECK(ctrl.activeProfileId() != only);
    CHECK_FALSE(ctrl.activeProfileId().isEmpty());

    ctrl.deleteProfile(ctrl.activeProfileId()); // best-effort cleanup
}
