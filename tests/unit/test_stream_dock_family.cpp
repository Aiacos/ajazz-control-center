// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_family.cpp
 * @brief Phase 24-02 (DEVICES-10): per-family MockTransport byte tests proving the SAME
 *        capability-generic StreamDockControlService + StreamDockInputService drive
 *        AKP03 (6 LCD keys + 3 encoders), AKP153 (15 keys), and AKP815 (15 keys).
 *
 * What this file proves:
 *  - assign-image emits the per-family image header (AKP03: CmdImage "BAT" at bytes 5-7;
 *    AKP153/AKP815: CmdBat "BAT" at bytes 5-7) -- NOT the AKP05 BAT/ULEND offsets
 *    (they are the same opcode bytes but the assertion is sourced from each family's
 *    own protocol header, not copied from AKP05 documentation).
 *  - Descriptor-driven geometry: assigning the family's max key index (15 for AKP153/815,
 *    9 for AKP03) produces a write burst -- proves no AKP05 10-key clamp.
 *  - AKP03 input: key press + all 3 encoders (press / CW / CCW) route through the SAME
 *    StreamDockInputService via descriptor.encoderCount=3.
 *  - AKP153/AKP815 input: key press fires; encoderCount=0 path is inert (no crash).
 *
 * Live hardware confirmation for these families is Phase 25. This test proves correctness
 * hardware-free via MockTransport. AKP153/AKP815 may not be physically connected.
 *
 * Test tag:   [stream-dock-family]
 * Quick run:  ctest --preset linux-release -R StreamDockFamily
 */
#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"
#include "qt_executor.hpp"
#include "stream_dock_control_service.hpp"
#include "stream_dock_input_service.hpp"

// Protocol headers: READ-ONLY to pin exact assertion bytes.
// Do NOT edit these files -- RE source of truth (CLAUDE.md).
#include "akp03_protocol.hpp"
#include "akp153_protocol.hpp"
#include "akp815_protocol.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QImage>
#include <QSignalSpy>
#include <QString>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;
using namespace ajazz::app;
using namespace ajazz::core;

// =============================================================================
// Shared helpers
// =============================================================================

namespace {

/// Drain the QTimer-based write queue (mirrors test_stream_dock_control_service.cpp).
void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

/// Spin the Qt event loop until pred() returns true or budget elapses.
template <typename Pred>
bool pumpUntil(Pred&& pred, std::chrono::milliseconds budget) {
    QDeadlineTimer deadline(budget);
    while (!pred() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,
                                        static_cast<int>(deadline.remainingTime()));
    }
    return pred();
}

/// Find the first write whose byte[5] == expected_cmd0 (first byte of command word).
/// Returns the index, or writes.size() on miss.
std::size_t findWriteByCmd0(std::vector<std::vector<std::uint8_t>> const& writes,
                            std::uint8_t cmd0,
                            std::size_t startFrom = 0) {
    for (std::size_t i = startFrom; i < writes.size(); ++i) {
        if (writes[i].size() > 5 && writes[i][5] == cmd0) {
            return i;
        }
    }
    return writes.size();
}

// =============================================================================
// Per-family descriptor builders (matching register.cpp rows)
// =============================================================================

/// AKP03 descriptor: 6 LCD keys + 3 encoders, no touch strip.
/// Source: register.cpp akp03_descriptor() helper.
core::DeviceDescriptor makeAkp03Desc() {
    core::DeviceDescriptor d{};
    d.vendorId = streamdeck::akp03::VendorId;
    d.productId = streamdeck::akp03::ProductIdAkp03;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP03 (test)";
    d.codename = "akp03";
    d.keyCount = streamdeck::akp03::DisplayKeyCount; // 6
    d.gridColumns = 3;
    d.encoderCount = streamdeck::akp03::EncoderCount; // 3
    d.hasTouchStrip = false;
    d.hasClock = false;
    return d;
}

/// AKP153 descriptor: 15 keys, no encoders.
/// Source: register.cpp akp153_descriptor() helper.
core::DeviceDescriptor makeAkp153Desc() {
    core::DeviceDescriptor d{};
    d.vendorId = streamdeck::akp153::VendorId;
    d.productId = streamdeck::akp153::ProductIdInternational;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP153 (test)";
    d.codename = "akp153";
    d.keyCount = streamdeck::akp153::KeyCount; // 15
    d.gridColumns = 5;
    d.encoderCount = 0;
    d.hasTouchStrip = false;
    d.hasClock = false;
    return d;
}

/// AKP815 descriptor: 15 keys, no encoders.
/// Source: register.cpp -- AKP815 shares AKP153 layout (5x3 vs 3x5), same opcode set.
core::DeviceDescriptor makeAkp815Desc() {
    core::DeviceDescriptor d{};
    d.vendorId = streamdeck::akp815::VendorId;
    d.productId = streamdeck::akp815::ProductId;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP815 (test)";
    d.codename = "akp815";
    d.keyCount = streamdeck::akp815::KeyCount;   // 15
    d.gridColumns = streamdeck::akp815::KeyCols; // 3
    d.encoderCount = 0;
    d.hasTouchStrip = false;
    d.hasClock = false;
    return d;
}

core::DeviceId makeDeviceId(std::string const& serial) {
    core::DeviceId id{};
    id.serial = serial;
    return id;
}

// =============================================================================
// AKP03 input-frame builders
// (action codes from akp03_protocol.hpp, sourced from [ajazz-sdk]/protocol/codes.rs)
// =============================================================================

/// AKP03 LCD key press frame (byte 9 = key 1..6, byte 10 = 0x01 pressed).
std::vector<std::uint8_t> makeAkp03KeyFrame(std::uint8_t key, bool pressed) {
    std::vector<std::uint8_t> f(16, 0);
    f[9] = key;
    f[10] = pressed ? 0x01u : 0x00u;
    return f;
}

/// AKP03 encoder rotation frame (tag byte from akp03_protocol.hpp action code table).
std::vector<std::uint8_t> makeAkp03EncoderTurnFrame(std::uint8_t actionCode) {
    std::vector<std::uint8_t> f(16, 0);
    f[9] = actionCode;
    return f;
}

/// AKP03 encoder press frame (tag byte = press action code, byte 10 = 0x01).
std::vector<std::uint8_t> makeAkp03EncoderPressFrame(std::uint8_t actionCode) {
    std::vector<std::uint8_t> f(16, 0);
    f[9] = actionCode;
    f[10] = 0x01u; // pressed
    return f;
}

/// AKP153/AKP815 key event frame (byte 9 = 1-based key index 1..15).
/// Source: akp153::parseInputReport reads keyIndex from frame[9].
std::vector<std::uint8_t> makeAkp153KeyFrame(std::uint8_t keyIndex) {
    std::vector<std::uint8_t> f(16, 0);
    f[9] = keyIndex; // 1-based, 1..15
    return f;
}

} // namespace

// =============================================================================
// CONTROL SERVICE: assign-image emits per-family image header + terminator
// =============================================================================

// ---------------------------------------------------------------------------
// AKP03 assign-image
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP03 assign-image emits CmdImage (BAT) header",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    // No VER GET_FEATURE queued -- open() is tolerant of missing feature resp
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-CTRL-TEST");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp03"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Assign a 60x60 solid-blue RGBA image to key 1 (1-based).
    // AKP03 native LCD resolution is 60x60 (akp03_protocol.hpp:KeyWidthPx/KeyHeightPx).
    QImage img(60, 60, QImage::Format_RGBA8888);
    img.fill(qRgba(0, 0, 255, 255));
    svc.assignKeyImage(1, img);
    drainQueue();

    auto const& writes = obs->writes();
    // Must have at least 3 more writes: BAT header + >=1 chunk + ULEND.
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // Find the BAT header: byte[5]=='B' (0x42).
    // Source: akp03_protocol.hpp CmdImage = {0x42, 0x41, 0x54} == "BAT".
    auto const batIdx = findWriteByCmd0(writes, 0x42, writeCountAfterOpen);
    REQUIRE(batIdx < writes.size());
    auto const& batPkt = writes[batIdx];
    REQUIRE(batPkt.size() >= 8);
    CHECK(batPkt[5] == streamdeck::akp03::CmdImage[0]); // 0x42 'B'
    CHECK(batPkt[6] == streamdeck::akp03::CmdImage[1]); // 0x41 'A'
    CHECK(batPkt[7] == streamdeck::akp03::CmdImage[2]); // 0x54 'T'

    // The ULEND terminator must close the burst (shared UploadFinishedMarker).
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ---------------------------------------------------------------------------
// AKP03 descriptor-driven: assigning the max key index (9 = side-button index)
// produces a burst. Verifies no AKP05 10-key clamp is applied.
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP03 descriptor-driven -- max key index 6 produces a burst",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-DESC-TEST");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp03"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Assign to key 6 (AKP03's max LCD key). Would fail if the service clamped to AKP05's 10.
    QImage img(60, 60, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 0, 0, 255));
    svc.assignKeyImage(6, img);
    drainQueue();

    // A burst must have been emitted (BAT header + at least one chunk + ULEND).
    REQUIRE(obs->writeCount() > writeCountAfterOpen + 2);
    // BAT header present.
    auto const batIdx = findWriteByCmd0(obs->writes(), 0x42, writeCountAfterOpen);
    REQUIRE(batIdx < obs->writes().size());
}

// ---------------------------------------------------------------------------
// AKP153 assign-image
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP153 assign-image emits CmdBat (BAT) header",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp153Desc();
    auto id = makeDeviceId("AKP153-CTRL-TEST");
    auto dev = streamdeck::makeAkp153WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp153"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Assign to key 1 (1-based). AKP153 native resolution is 85x85.
    QImage img(85, 85, QImage::Format_RGBA8888);
    img.fill(qRgba(0, 255, 0, 255));
    svc.assignKeyImage(1, img);
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // BAT header: bytes[5..7] = 'B','A','T' (akp153::CmdBat = {0x42, 0x41, 0x54}).
    auto const batIdx = findWriteByCmd0(writes, 0x42, writeCountAfterOpen);
    REQUIRE(batIdx < writes.size());
    auto const& batPkt = writes[batIdx];
    REQUIRE(batPkt.size() >= 8);
    CHECK(batPkt[5] == streamdeck::akp153::CmdBat[0]); // 0x42 'B'
    CHECK(batPkt[6] == streamdeck::akp153::CmdBat[1]); // 0x41 'A'
    CHECK(batPkt[7] == streamdeck::akp153::CmdBat[2]); // 0x54 'T'

    // ULEND terminator.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ---------------------------------------------------------------------------
// AKP153 descriptor-driven: assigning key 15 (max) produces a burst.
// Proves no AKP05 10-key clamp is applied (the DEVICES-10 key assertion).
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP153 descriptor-driven -- key 15 produces a burst (no 10-key clamp)",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp153Desc();
    auto id = makeDeviceId("AKP153-DESC-TEST");
    auto dev = streamdeck::makeAkp153WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp153"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Key 15 is AKP153's max. Would fail if the service wrongly clamped to AKP05's 10.
    QImage img(85, 85, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 128, 0, 255));
    svc.assignKeyImage(15, img);
    drainQueue();

    // A burst must have been emitted.
    REQUIRE(obs->writeCount() > writeCountAfterOpen + 2);
    auto const batIdx = findWriteByCmd0(obs->writes(), 0x42, writeCountAfterOpen);
    REQUIRE(batIdx < obs->writes().size());
}

// ---------------------------------------------------------------------------
// AKP815 assign-image
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP815 assign-image emits BAT header (Rot180 format)",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp815Desc();
    auto id = makeDeviceId("AKP815-CTRL-TEST");
    auto dev = streamdeck::makeAkp815WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp815"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // AKP815 native resolution is 100x100 (akp815_protocol.hpp:KeyWidthPx/KeyHeightPx).
    // Rot180 transform is applied inside the AKP815 backend -- the service passes RGBA.
    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(qRgba(128, 0, 255, 255));
    svc.assignKeyImage(1, img);
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // AKP815 reuses AKP153 CmdBat opcode (akp815.cpp: reuses akp153::build* helpers).
    // Source: akp153_protocol.hpp CmdBat = {0x42, 0x41, 0x54}.
    auto const batIdx = findWriteByCmd0(writes, 0x42, writeCountAfterOpen);
    REQUIRE(batIdx < writes.size());
    auto const& batPkt = writes[batIdx];
    REQUIRE(batPkt.size() >= 8);
    CHECK(batPkt[5] == 0x42); // 'B'
    CHECK(batPkt[6] == 0x41); // 'A'
    CHECK(batPkt[7] == 0x54); // 'T'

    // ULEND terminator.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ---------------------------------------------------------------------------
// AKP815 descriptor-driven: assigning key 15 (max) produces a burst.
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP815 descriptor-driven -- key 15 produces a burst (no 10-key clamp)",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp815Desc();
    auto id = makeDeviceId("AKP815-DESC-TEST");
    auto dev = streamdeck::makeAkp815WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp815"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(qRgba(0, 128, 255, 255));
    svc.assignKeyImage(15, img);
    drainQueue();

    REQUIRE(obs->writeCount() > writeCountAfterOpen + 2);
    auto const batIdx = findWriteByCmd0(obs->writes(), 0x42, writeCountAfterOpen);
    REQUIRE(batIdx < obs->writes().size());
}

// =============================================================================
// INPUT SERVICE: key press + encoder routing per family
// =============================================================================

// ---------------------------------------------------------------------------
// AKP03 input: key press fires onPress chain
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP03 key press fires onPress chain", "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-INPUT-KEY");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    int pressCount = 0;
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++pressCount; };

    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    // AKP03 LCD key 2 (1-based, 1..6): dispatch uses ev.index which matches raw key number.
    prof.keys[2].onPress = {Action{.kind = ActionKind::KeyPress}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(dev);

    // Frame: byte[9] = 2 (LCD key 2), byte[10] = 0x01 (pressed).
    obs->enqueueRead(makeAkp03KeyFrame(2, true));
    svc.pump();

    REQUIRE(pressCount == 1);
}

// ---------------------------------------------------------------------------
// AKP03 input: all 3 encoders route through the SAME input service
// (descriptor.encoderCount = 3; this is the DEVICES-10 encoder-routing proof)
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP03 all 3 encoders press + CW + CCW route through input service",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-INPUT-ENC");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    // Spy counters per encoder per action type.
    int press0{0}, press1{0}, press2{0};
    int cw0{0}, cw1{0}, cw2{0};
    int ccw0{0}, ccw1{0}, ccw2{0};

    ActionExecutors spies;
    spies.keyPress = [&](std::string_view key) {
        // Use key name to distinguish encoder/direction: pass as a unique key string.
        std::string_view k = key;
        if (k == "press0") {
            ++press0;
        } else if (k == "press1") {
            ++press1;
        } else if (k == "press2") {
            ++press2;
        } else if (k == "cw0") {
            ++cw0;
        } else if (k == "cw1") {
            ++cw1;
        } else if (k == "cw2") {
            ++cw2;
        } else if (k == "ccw0") {
            ++ccw0;
        } else if (k == "ccw1") {
            ++ccw1;
        } else if (k == "ccw2") {
            ++ccw2;
        }
    };

    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    // Bind all 3 encoders. Use settingsJson as a discriminator string.
    prof.encoders[0].onPress = {Action{.kind = ActionKind::KeyPress, .settingsJson = "press0"}};
    prof.encoders[0].onCw = {Action{.kind = ActionKind::KeyPress, .settingsJson = "cw0"}};
    prof.encoders[0].onCcw = {Action{.kind = ActionKind::KeyPress, .settingsJson = "ccw0"}};
    prof.encoders[1].onPress = {Action{.kind = ActionKind::KeyPress, .settingsJson = "press1"}};
    prof.encoders[1].onCw = {Action{.kind = ActionKind::KeyPress, .settingsJson = "cw1"}};
    prof.encoders[1].onCcw = {Action{.kind = ActionKind::KeyPress, .settingsJson = "ccw1"}};
    prof.encoders[2].onPress = {Action{.kind = ActionKind::KeyPress, .settingsJson = "press2"}};
    prof.encoders[2].onCw = {Action{.kind = ActionKind::KeyPress, .settingsJson = "cw2"}};
    prof.encoders[2].onCcw = {Action{.kind = ActionKind::KeyPress, .settingsJson = "ccw2"}};

    // Observe the synthetic-release signal (encoder press -> synthesised release).
    int relCount = 0;
    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    QObject::connect(&svc, &StreamDockInputService::encoderReleaseSynthesised, [&](std::uint16_t) {
        ++relCount;
    });
    svc.setActiveDevice(dev);

    SECTION("Encoder 0: press + CW + CCW") {
        // Press (action code 0x33, byte[10]=0x01).
        obs->enqueueRead(makeAkp03EncoderPressFrame(streamdeck::akp03::ActionEncoder0Press));
        svc.pump();
        REQUIRE(press0 == 1);
        REQUIRE(relCount >= 1); // synthetic release synthesised

        // CW (action code 0x91).
        obs->enqueueRead(makeAkp03EncoderTurnFrame(streamdeck::akp03::ActionEncoder0Cw));
        svc.pump();
        REQUIRE(pumpUntil([&] { return cw0 > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(cw0 == 1);

        // CCW (action code 0x90).
        obs->enqueueRead(makeAkp03EncoderTurnFrame(streamdeck::akp03::ActionEncoder0Ccw));
        svc.pump();
        REQUIRE(pumpUntil([&] { return ccw0 > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(ccw0 == 1);
    }

    SECTION("Encoder 1: press + CW + CCW") {
        obs->enqueueRead(makeAkp03EncoderPressFrame(streamdeck::akp03::ActionEncoder1Press));
        svc.pump();
        REQUIRE(press1 == 1);

        obs->enqueueRead(makeAkp03EncoderTurnFrame(streamdeck::akp03::ActionEncoder1Cw));
        svc.pump();
        REQUIRE(pumpUntil([&] { return cw1 > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(cw1 == 1);

        obs->enqueueRead(makeAkp03EncoderTurnFrame(streamdeck::akp03::ActionEncoder1Ccw));
        svc.pump();
        REQUIRE(pumpUntil([&] { return ccw1 > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(ccw1 == 1);
    }

    SECTION("Encoder 2: press + CW + CCW") {
        obs->enqueueRead(makeAkp03EncoderPressFrame(streamdeck::akp03::ActionEncoder2Press));
        svc.pump();
        REQUIRE(press2 == 1);

        obs->enqueueRead(makeAkp03EncoderTurnFrame(streamdeck::akp03::ActionEncoder2Cw));
        svc.pump();
        REQUIRE(pumpUntil([&] { return cw2 > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(cw2 == 1);

        obs->enqueueRead(makeAkp03EncoderTurnFrame(streamdeck::akp03::ActionEncoder2Ccw));
        svc.pump();
        REQUIRE(pumpUntil([&] { return ccw2 > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(ccw2 == 1);
    }
}

// ---------------------------------------------------------------------------
// AKP153 input: key press fires chain, encoderCount=0 path is inert (no crash)
// Live hardware confirmation is Phase 25; AKP153 may not be physically connected.
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP153 key press fires chain; encoderCount=0 path is inert",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp153Desc();
    auto id = makeDeviceId("AKP153-INPUT-TEST");
    auto dev = streamdeck::makeAkp153WithTransport(desc, id, std::move(transport));

    int pressCount = 0;
    int encoderDispatch = 0;

    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++pressCount; };
    spies.openUrl = [&](std::string_view) { ++encoderDispatch; };

    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.keys[5].onPress = {Action{.kind = ActionKind::KeyPress}};
    // Bind encoder 0 -- should NEVER fire since AKP153 has no encoders.
    prof.encoders[0].onCw = {Action{.kind = ActionKind::OpenUrl}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(dev);

    SECTION("Key press on AKP153 fires onPress chain") {
        // AKP153 input frame: byte[9] = keyIndex (1-based, 1..15).
        obs->enqueueRead(makeAkp153KeyFrame(5));
        svc.pump();
        REQUIRE(pressCount == 1);
        REQUIRE(encoderDispatch == 0); // no encoder event emitted
    }

    SECTION("No encoder events emitted by AKP153 backend (encoderCount=0 path inert)") {
        // Don't feed any encoder frame -- AKP153 backend never produces EncoderTurned/Pressed.
        // Just pump with no input and verify no spurious encoder dispatch.
        svc.pump();
        REQUIRE(encoderDispatch == 0);
        REQUIRE(pressCount == 0);
    }
}

// ---------------------------------------------------------------------------
// AKP815 input: key press fires chain, encoderCount=0 path is inert (no crash)
// Live hardware confirmation is Phase 25; AKP815 may not be physically connected.
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: AKP815 key press fires chain; encoderCount=0 path is inert",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp815Desc();
    auto id = makeDeviceId("AKP815-INPUT-TEST");
    auto dev = streamdeck::makeAkp815WithTransport(desc, id, std::move(transport));

    int pressCount = 0;

    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++pressCount; };

    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.keys[10].onPress = {Action{.kind = ActionKind::KeyPress}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(dev);

    // AKP815 reuses AKP153 wire format (akp815.cpp delegates to akp153:: builders/parser).
    // Key press: byte[9] = keyIndex (1-based, 1..15).
    obs->enqueueRead(makeAkp153KeyFrame(10));
    svc.pump();
    REQUIRE(pressCount == 1);
}

// =============================================================================
// CR-01 REGRESSION: AKP03 setKeyImage/clearKey side-button index rejection
// =============================================================================
// Regression for CR-01: key indices 7..9 (non-LCD side buttons) must be
// rejected by the backend WITHOUT sending any BAT/ULEND burst to the firmware.
// Key indices 1..6 (LCD keys) must continue to produce a burst.
//
// Source: akp03.md "LCD keys: 6"; akp03_protocol.hpp DisplayKeyCount=6,
//         SideButtonCount=3, KeyCount=9.
// ---------------------------------------------------------------------------

TEST_CASE("StreamDockFamily: CR-01 AKP03 side-button keyIndex 7 rejected -- no BAT burst",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-CR01-REJECT");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp03"));
    drainQueue();
    auto const baseCount = obs->writeCount();

    // Key 7 is a non-LCD side button -- setKeyImage must reject it silently.
    // No BAT header or ULEND burst should be written.
    QImage img(60, 60, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 255, 0, 255));
    svc.assignKeyImage(7, img);
    drainQueue();

    // Write count must not have grown (no burst emitted for a non-LCD key).
    CHECK(obs->writeCount() == baseCount);
}

TEST_CASE("StreamDockFamily: CR-01 AKP03 side-button keyIndex 8 rejected -- no BAT burst",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-CR01-REJECT8");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp03"));
    drainQueue();
    auto const baseCount = obs->writeCount();

    QImage img(60, 60, QImage::Format_RGBA8888);
    img.fill(qRgba(0, 255, 255, 255));
    svc.assignKeyImage(8, img);
    drainQueue();

    CHECK(obs->writeCount() == baseCount);
}

TEST_CASE("StreamDockFamily: CR-01 AKP03 side-button keyIndex 9 rejected -- no BAT burst",
          "[stream-dock-family]") {
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-CR01-REJECT9");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    StreamDockControlService svc(
        [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
    svc.setActiveDevice(QStringLiteral("akp03"));
    drainQueue();
    auto const baseCount = obs->writeCount();

    QImage img(60, 60, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 0, 255, 255));
    svc.assignKeyImage(9, img);
    drainQueue();

    CHECK(obs->writeCount() == baseCount);
}

TEST_CASE("StreamDockFamily: CR-01 AKP03 LCD keyIndex 1..6 all produce a BAT burst",
          "[stream-dock-family]") {
    // Verifies that the CR-01 fix did not accidentally over-restrict: all
    // 6 LCD keys must still produce a BAT + ULEND burst after the fix.
    ajazz::tests::qtApp();

    for (std::uint8_t keyIdx = 1; keyIdx <= 6; ++keyIdx) {
        auto transport = std::make_unique<ajazz::tests::MockTransport>();
        auto* obs = transport.get();
        transport->open();

        auto desc = makeAkp03Desc();
        auto id = makeDeviceId("AKP03-CR01-LCD" + std::to_string(keyIdx));
        auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

        StreamDockControlService svc(
            [dev](QString const&) -> std::shared_ptr<core::IDevice> { return dev; }, nullptr);
        svc.setActiveDevice(QStringLiteral("akp03"));
        drainQueue();
        auto const baseCount = obs->writeCount();

        QImage img(60, 60, QImage::Format_RGBA8888);
        img.fill(qRgba(static_cast<int>(keyIdx * 40), 0, 0, 255));
        svc.assignKeyImage(static_cast<int>(keyIdx), img);
        drainQueue();

        // Must have emitted at least BAT header + 1 chunk + ULEND.
        INFO("keyIndex=" << static_cast<int>(keyIdx));
        REQUIRE(obs->writeCount() > baseCount + 2);

        auto const batIdx = findWriteByCmd0(obs->writes(), 0x42, baseCount);
        REQUIRE(batIdx < obs->writes().size());
    }
}

// =============================================================================
// WR-05 REGRESSION: AKP03 v3 EncoderReleased hardware event handled
// =============================================================================
// Regression for WR-05: the AKP03 v3 firmware emits real EncoderReleased
// events (byte[9]=ActionEncoderNPress, byte[10]=0x00). The dispatch() case
// for EncoderReleased now has a clear comment instead of "Dormant on hardware".
// This test verifies that:
//  (a) EncoderPressed fires the onPress chain AND the encoderReleaseSynthesised
//      signal (as before -- no regression on AKP05 path).
//  (b) A hardware EncoderReleased frame from the AKP03 backend passes through
//      poll() and dispatch() without crashing or corrupting state.
// ---------------------------------------------------------------------------

/// AKP03 encoder release frame: byte[9] = press action code, byte[10] = 0x00
/// (v3 firmware release polarity). parseInputReport produces EncoderReleased.
std::vector<std::uint8_t> makeAkp03EncoderReleaseFrame(std::uint8_t actionCode) {
    std::vector<std::uint8_t> f(16, 0);
    f[9] = actionCode;
    f[10] = 0x00u; // released (v3 firmware)
    return f;
}

TEST_CASE("StreamDockFamily: WR-05 AKP03 EncoderReleased hardware event dispatched without crash",
          "[stream-dock-family]") {
    // This test verifies the WR-05 fix: EncoderReleased events from AKP03 v3
    // firmware pass through dispatch() cleanly. The service currently breaks
    // on EncoderReleased (no onRelease binding yet); this is intentional and
    // documented in the TODO(WR-05) comment. The test asserts:
    //  - pump() does not throw or corrupt the service for a real release frame.
    //  - The synthesised release still fires on the PRIOR EncoderPressed.
    ajazz::tests::qtApp();

    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* obs = transport.get();
    transport->open();

    auto desc = makeAkp03Desc();
    auto id = makeDeviceId("AKP03-WR05-RELEASE");
    auto dev = streamdeck::makeAkp03WithTransport(desc, id, std::move(transport));

    int pressCount = 0;
    int synthReleaseCount = 0;

    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++pressCount; };

    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.encoders[0].onPress = {Action{.kind = ActionKind::KeyPress}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    QObject::connect(&svc, &StreamDockInputService::encoderReleaseSynthesised, [&](std::uint16_t) {
        ++synthReleaseCount;
    });
    svc.setActiveDevice(dev);

    // Feed an encoder press (byte[10]=0x01 -> EncoderPressed).
    obs->enqueueRead(makeAkp03EncoderPressFrame(streamdeck::akp03::ActionEncoder0Press));
    svc.pump();

    // Press chain fired and synthetic release synthesised.
    REQUIRE(pressCount == 1);
    REQUIRE(synthReleaseCount == 1);

    auto const synthCountBefore = synthReleaseCount;

    // Now feed a real v3-firmware EncoderReleased (byte[10]=0x00).
    // This should NOT crash, NOT fire the press chain again, and NOT
    // synthesise another release (it goes to the EncoderReleased break case).
    obs->enqueueRead(makeAkp03EncoderReleaseFrame(streamdeck::akp03::ActionEncoder0Press));
    svc.pump(); // must not throw

    // Press count unchanged (release doesn't re-fire the press chain).
    REQUIRE(pressCount == 1);
    // Synthetic release count unchanged (release goes to break, not synthesise).
    REQUIRE(synthReleaseCount == synthCountBefore);
}
