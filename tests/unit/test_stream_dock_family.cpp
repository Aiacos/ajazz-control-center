// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_family.cpp
 * @brief MockTransport byte tests proving the capability-generic
 *        StreamDockControlService + StreamDockInputService drive the AKP815.
 *
 * Originally this covered AKP03 (6 LCD keys + 3 encoders), AKP153 (15 keys) and
 * AKP815 (15 keys). The AKP03/AKP05/AKP153 C++ wire backends were removed in
 * favour of the out-of-process mirajazz sidecar (experiment/mirajazz Slice D);
 * their byte-level coverage moved to the sidecar's cargo tests. AKP815 is NOT a
 * mirajazz device (800x480 strip) and keeps its custom C++ backend, so the
 * remaining wire coverage here is AKP815-only.
 *
 * What this file proves for the AKP815:
 *  - assign-image emits the v1-API image header (CmdBat "BAT" at bytes 5-7)
 *    followed by the "ULEND" commit terminator.
 *  - Descriptor-driven geometry: assigning the max key index (15) produces a
 *    write burst -- proves no AKP05 10-key clamp.
 *  - Input: a key press fires the onPress chain; the encoderCount=0 path is
 *    inert (no crash).
 *
 * Live hardware confirmation is Phase 25; this test proves correctness
 * hardware-free via MockTransport. The AKP815 may not be physically connected.
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

// Protocol header: READ-ONLY to pin exact assertion bytes.
#include "akp815_protocol.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QString>

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

/// Find the first write whose byte[5] == cmd0 (first byte of command word).
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

/// AKP815 descriptor: 15 keys (5x3 grid), no encoders.
/// Source: register.cpp -- AKP815 carve-out row.
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

/// AKP815 v1-API key event frame (byte 9 = 1-based key index 1..15).
/// Source: akp815::parseInputReport reads keyIndex from frame[9].
std::vector<std::uint8_t> makeV1ApiKeyFrame(std::uint8_t keyIndex) {
    std::vector<std::uint8_t> f(16, 0);
    f[9] = keyIndex; // 1-based, 1..15
    return f;
}

} // namespace

// =============================================================================
// CONTROL SERVICE: assign-image emits the BAT image header + ULEND terminator
// =============================================================================

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

    // AKP815 emits the v1-API CmdBat opcode (akp815::buildImageHeader, akp815_wire.hpp).
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
// INPUT SERVICE: key press routing
// =============================================================================

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

    // AKP815 v1-API wire format: key press byte[9] = keyIndex (1-based, 1..15).
    obs->enqueueRead(makeV1ApiKeyFrame(10));
    svc.pump();
    REQUIRE(pressCount == 1);
}
