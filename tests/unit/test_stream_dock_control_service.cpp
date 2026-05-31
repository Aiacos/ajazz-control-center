// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_control_service.cpp
 * @brief MockTransport wire-level assertions for StreamDockControlService.
 *
 * Covers DISPLAY-06/07/08, DOCK-01/02 requirements using MockTransport and
 * makeAkp05WithTransport — no real HID hardware required (Phase 14 gating proof;
 * live power-cycle smoke deferred to Phase 25).
 *
 * Test tag: [stream-dock-control]
 *
 * Byte conventions (from akp05_protocol.hpp + akp_common_protocol.hpp):
 *   Packet layout: bytes[0..2] = "CRT", bytes[3..4] = 0x00,
 *                  bytes[5..7] = command, bytes[8+] = payload.
 *   LIG  (brightness): byte[5] == 'L' (0x4C), brightness at byte[10].
 *   BAT  (key image):  bytes[5..7] == 'B','A','T'.
 *   ULEND:             bytes[5..9] == 'U','L','E','N','D'.
 *
 * Key-index convention: AKP05 backend is 1-based (1..10). Profile keys are
 * stored as std::uint16_t; the service adds 1 to map 0-based profile indices
 * to 1-based device indices (confirmed by reading profile.hpp keys map and
 * the setKeyImage 1-based contract in akp05.cpp).
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

// ---------------------------------------------------------------------------
// Helper: build a minimal AKP05E DeviceDescriptor (enough for makeAkp05WithTransport).
// ---------------------------------------------------------------------------
core::DeviceDescriptor makeAkp05eDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (test)";
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
    id.serial = "TEST-14-02";
    return id;
}

// ---------------------------------------------------------------------------
// Fixture: a MockTransport-backed Akp05Device with a VER feature response
// pre-queued so open() can probe firmware without blocking.
// ---------------------------------------------------------------------------
struct Akp05Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport; ///< Non-owning observer ptr.
};

/// Build the fixture.  The VER response is a 20-byte frame:
///   byte[0] = 0x01 (report id), then the ASCII string "V3.AKP05E.01.007".
Akp05Fixture makeFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* obs = owned.get();
    // Seed the GET_FEATURE response that probeFirmwareVersion() will consume.
    std::vector<std::uint8_t> verResponse(20, 0);
    verResponse[0] = 0x01; // report id
    std::string const vstr = "V3.AKP05E.01.007";
    for (std::size_t i = 0; i < vstr.size() && i + 1 < verResponse.size(); ++i) {
        verResponse[i + 1] = static_cast<std::uint8_t>(vstr[i]);
    }
    obs->enqueueReadFeature(std::move(verResponse));
    auto dev = streamdeck::makeAkp05WithTransport(
        makeAkp05eDescriptor(), makeAkp05eId(), std::move(owned));
    return Akp05Fixture{std::move(dev), obs};
}

/// Drain the QTimer-based write queue.
void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents(); // second pass handles any timers fired on first pass
}

/// Find the first write whose byte[5] == expected_cmd0 (i.e. first byte of cmd word).
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

} // namespace

// ===========================================================================
// DISPLAY-06: setActiveDevice holds one handle and issues a LIG brightness at open.
// ===========================================================================

TEST_CASE("StreamDockControlService: setActiveDevice issues LIG brightness (DISPLAY-06)",
          "[stream-dock-control][DISPLAY-06]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device; // keep device alive in lookup
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    auto const& writes = obs->writes();

    // There must be at least one LIG write.
    auto const ligIdx = findWriteByCmd0(writes, 0x4c); // 'L' of LIG
    REQUIRE(ligIdx < writes.size());
    auto const& ligPkt = writes[ligIdx];
    REQUIRE(ligPkt.size() >= 11);
    CHECK(ligPkt[5] == 0x4c); // 'L'
    CHECK(ligPkt[6] == 0x49); // 'I'
    CHECK(ligPkt[7] == 0x47); // 'G'
    // Brightness byte is at position 10; must be > 0 (panel must light up).
    CHECK(ligPkt[10] > 0);
}

// ===========================================================================
// DOCK-01: firmwareVersionFor returns the VER string seeded via enqueueReadFeature.
// ===========================================================================

TEST_CASE("StreamDockControlService: firmwareVersionFor returns cached VER string (DOCK-01)",
          "[stream-dock-control][DOCK-01]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));

    // firmwareVersionFor must return the seeded version string, not "unknown".
    QString const ver = svc.firmwareVersionFor(QStringLiteral("akp05e"));
    CHECK(ver == QStringLiteral("V3.AKP05E.01.007"));
}

// ===========================================================================
// DISPLAY-07 + DOCK-02: assignKeyImage produces BAT header -> chunks -> ULEND
//                       with no explicit flush call.
// ===========================================================================

TEST_CASE("StreamDockControlService: assignKeyImage emits BAT then ULEND (DISPLAY-07+DOCK-02)",
          "[stream-dock-control][DISPLAY-07][DOCK-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Assign a 85x85 solid-red RGBA image to key 1 (1-based).
    // The service must arm the timer; we then drain it.
    QImage img(85, 85, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 0, 0, 255));
    svc.assignKeyImage(1, img); // 1-based key index
    drainQueue();

    auto const& writes = obs->writes();
    // Must have emitted at least 3 more writes: BAT header + >=1 chunk + ULEND.
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // Find the BAT header (first write after open with byte[5]=='B').
    auto const batIdx = findWriteByCmd0(writes, 0x42, writeCountAfterOpen); // 'B' of BAT
    REQUIRE(batIdx < writes.size());
    auto const& batPkt = writes[batIdx];
    REQUIRE(batPkt.size() >= 8);
    CHECK(batPkt[5] == 0x42); // 'B'
    CHECK(batPkt[6] == 0x41); // 'A'
    CHECK(batPkt[7] == 0x54); // 'T'

    // The ULEND must be the last write in the burst.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ===========================================================================
// DISPLAY-08: repaintFromProfile enqueues BAT+ULEND for each bound key.
// ===========================================================================

TEST_CASE("StreamDockControlService: repaintFromProfile repaints all bound keys (DISPLAY-08)",
          "[stream-dock-control][DISPLAY-08]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    // Build a fake profile with 2 bound keys (0-based indices in the map).
    core::Profile fakeProfile;
    fakeProfile.id = "test-profile";
    fakeProfile.name = "Test Profile";
    fakeProfile.deviceCodename = "akp05e";

    // Key 0 -> background fill (solid red).
    {
        core::Binding b;
        b.state.background = core::Rgb{255, 0, 0};
        fakeProfile.keys[0] = std::move(b);
    }
    // Key 1 -> background fill (solid blue).
    {
        core::Binding b;
        b.state.background = core::Rgb{0, 0, 255};
        fakeProfile.keys[1] = std::move(b);
    }

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&fakeProfile]() -> core::Profile const& { return fakeProfile; },
        nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.repaintFromProfile();
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    // Count the number of BAT headers emitted (one per key).
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
    // 2 keys -> 2 BAT headers and >= 2 ULEND commits.
    CHECK(batCount >= 2);
    CHECK(ulendCount >= 2);
}

// ===========================================================================
// DISPLAY-10 MAI: assignMainImage emits MAI header + ULEND through held handle.
// ===========================================================================

TEST_CASE("StreamDockControlService: assignMainImage emits MAI header then ULEND (DISPLAY-10)",
          "[stream-dock-control][DISPLAY-10][aux-surface]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // 800x100 RGBA image for the main LCD strip (any size -- backend resizes).
    QImage img(100, 50, QImage::Format_RGBA8888);
    img.fill(qRgba(0, 128, 255, 255));
    svc.assignMainImage(img);
    drainQueue();

    auto const& writes = obs->writes();
    // Must have emitted at least 3 more writes: MAI header + >=1 chunk + ULEND.
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // First write after open must be a MAI header (bytes[5..7] == M,A,I).
    auto const maiIdx = findWriteByCmd0(writes, 0x4d, writeCountAfterOpen); // 'M' of MAI
    REQUIRE(maiIdx < writes.size());
    auto const& maiPkt = writes[maiIdx];
    REQUIRE(maiPkt.size() >= 8);
    CHECK(maiPkt[5] == 0x4d); // 'M'
    CHECK(maiPkt[6] == 0x41); // 'A'
    CHECK(maiPkt[7] == 0x49); // 'I'

    // ULEND must be the last write in the burst.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ===========================================================================
// assignEncoderImage routes the 4 strip/encoder zones through the BAT opcode at
// wire bytes 1..4 (encoderIndex + 1), NOT the vendor ENC opcode. Hardware-
// confirmed 2026-05-31 on 0x0300:0x3004: ENC stays blank while BAT wire 1..4
// paints the strip zones aligned to the dials (akp_device_matrix §4 — the strip
// zones ARE the encoder displays; there is no separate encoder LCD).
// ===========================================================================

TEST_CASE("StreamDockControlService: assignEncoderImage emits BAT header at wire byte index+1",
          "[stream-dock-control][DISPLAY-10][aux-surface]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Image for encoder 2 (0-based) -> strip-zone wire byte 3.
    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 64, 0, 255));
    svc.assignEncoderImage(2, img); // 0-based encoder index
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // BAT header: bytes[5..7] == B,A,T; byte[12] == 0x03 (wire byte = index + 1).
    auto const encIdx = findWriteByCmd0(writes, 0x42, writeCountAfterOpen); // 'B' of BAT
    REQUIRE(encIdx < writes.size());
    auto const& encPkt = writes[encIdx];
    REQUIRE(encPkt.size() >= 13);
    CHECK(encPkt[5] == 0x42);  // 'B'
    CHECK(encPkt[6] == 0x41);  // 'A'
    CHECK(encPkt[7] == 0x54);  // 'T'
    CHECK(encPkt[12] == 0x03); // wire byte = 0-based encoder index 2 + 1

    // ULEND commit sentinel.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ===========================================================================
// DISPLAY-10 DRA: assignTouchStripZone(1) emits DRA header with location=1
//                 and BE16 x=200 (zone*200) at bytes[17..18].
// ===========================================================================

TEST_CASE("StreamDockControlService: assignTouchStripZone emits DRA header x=zone*200 (DISPLAY-10)",
          "[stream-dock-control][DISPLAY-10][aux-surface]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // Zone 1: location=1, x=1*200=200 (0x00C8).
    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(qRgba(0, 200, 100, 255));
    svc.assignTouchStripZone(1, img);
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen + 2);

    // DRA header: bytes[5..7] == D,R,A; byte[12] == 0x01 (location=zone);
    // BE16 x at bytes[17..18] == 0x00,0xC8 (200 = zone 1 * 200).
    auto const draIdx = findWriteByCmd0(writes, 0x44, writeCountAfterOpen); // 'D' of DRA
    REQUIRE(draIdx < writes.size());
    auto const& draPkt = writes[draIdx];
    REQUIRE(draPkt.size() >= 19);
    CHECK(draPkt[5] == 0x44);  // 'D'
    CHECK(draPkt[6] == 0x52);  // 'R'
    CHECK(draPkt[7] == 0x41);  // 'A'
    CHECK(draPkt[12] == 0x01); // location = zone = 1
    CHECK(draPkt[17] == 0x00); // BE16 x high byte (200 >> 8)
    CHECK(draPkt[18] == 0xc8); // BE16 x low byte (200 & 0xFF)

    // ULEND commit sentinel.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

// ===========================================================================
// Range refusal (T-23-02): out-of-range encoder/zone index produces zero writes.
// ===========================================================================

TEST_CASE("StreamDockControlService: assignEncoderImage index>=4 produces zero wire writes",
          "[stream-dock-control][DISPLAY-10][range-guard]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // encoderIndex=4 is out-of-range (AKP05E has 4 encoders: 0..3).
    // The backend (setEncoderImage) refuses >=4. Service must NOT bypass the guard.
    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(qRgba(128, 128, 128, 255));
    svc.assignEncoderImage(4, img);
    drainQueue();

    // Zero additional writes: backend range-check honored.
    CHECK(obs->writeCount() == writeCountAfterOpen);
}

TEST_CASE("StreamDockControlService: assignTouchStripZone zone>=4 produces zero wire writes",
          "[stream-dock-control][DISPLAY-10][range-guard]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    // zone=4 is out-of-range (AKP05E has 4 zones: 0..3).
    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(qRgba(64, 64, 64, 255));
    svc.assignTouchStripZone(4, img);
    drainQueue();

    // Zero additional writes: backend range-check honored.
    CHECK(obs->writeCount() == writeCountAfterOpen);
}

// ===========================================================================
// DISPLAY-10 repaint: repaintEncodersFromProfile iterates Profile::encoders
//                     and emits DRA zone bursts for bound encoders.
// ===========================================================================

TEST_CASE(
    "StreamDockControlService: repaintEncodersFromProfile - bound encoder emits DRA zone burst",
    "[stream-dock-control][DISPLAY-10][repaint-encoders]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    // Profile with encoder 1 bound to a background fill (0-based index).
    core::Profile fakeProfile;
    fakeProfile.id = "enc-repaint-test";
    fakeProfile.name = "Encoder Repaint Test";
    fakeProfile.deviceCodename = "akp05e";
    {
        core::EncoderBinding eb;
        eb.state.background = core::Rgb{0, 128, 255};
        fakeProfile.encoders[1] = std::move(eb);
    }

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&fakeProfile]() -> core::Profile const& { return fakeProfile; },
        nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.repaintEncodersFromProfile();
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    // DRA burst for encoder 1: bytes[5..7]==D,R,A; byte[12]==1 (location=zone=1);
    // BE16 x at bytes[17..18]==0x00,0xC8 (1*200=200).
    auto const draIdx = findWriteByCmd0(writes, 0x44, writeCountAfterOpen); // 'D' of DRA
    REQUIRE(draIdx < writes.size());
    auto const& draPkt = writes[draIdx];
    REQUIRE(draPkt.size() >= 19);
    CHECK(draPkt[5] == 0x44);  // 'D'
    CHECK(draPkt[6] == 0x52);  // 'R'
    CHECK(draPkt[7] == 0x41);  // 'A'
    CHECK(draPkt[12] == 0x01); // location = zone = encoderIndex = 1
    CHECK(draPkt[17] == 0x00); // BE16 x high byte (200 >> 8)
    CHECK(draPkt[18] == 0xc8); // BE16 x low byte (200 & 0xFF)

    // ULEND must be the last write in the burst.
    auto const& ulendPkt = writes.back();
    REQUIRE(ulendPkt.size() >= 10);
    CHECK(ulendPkt[5] == 0x55); // 'U'
    CHECK(ulendPkt[6] == 0x4c); // 'L'
    CHECK(ulendPkt[7] == 0x45); // 'E'
    CHECK(ulendPkt[8] == 0x4e); // 'N'
    CHECK(ulendPkt[9] == 0x44); // 'D'
}

TEST_CASE(
    "StreamDockControlService: repaintEncodersFromProfile - two bound encoders each emit DRA burst",
    "[stream-dock-control][DISPLAY-10][repaint-encoders]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    // Two bound encoders: index 0 and index 2.
    core::Profile fakeProfile;
    fakeProfile.id = "enc-two-repaint-test";
    fakeProfile.name = "Encoder Two Repaint";
    fakeProfile.deviceCodename = "akp05e";
    {
        core::EncoderBinding eb0;
        eb0.state.background = core::Rgb{255, 0, 0};
        fakeProfile.encoders[0] = std::move(eb0);

        core::EncoderBinding eb2;
        eb2.state.background = core::Rgb{0, 255, 0};
        fakeProfile.encoders[2] = std::move(eb2);
    }

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&fakeProfile]() -> core::Profile const& { return fakeProfile; },
        nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.repaintEncodersFromProfile();
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    // Count DRA headers emitted after open.
    // Encoder 0 -> DRA location=0, x=0; encoder 2 -> DRA location=2, x=400 (0x01,0x90).
    std::size_t draCount = 0;
    bool foundZone0 = false;
    bool foundZone2 = false;
    for (std::size_t i = writeCountAfterOpen; i < writes.size(); ++i) {
        auto const& pkt = writes[i];
        if (pkt.size() >= 19 && pkt[5] == 0x44 && pkt[6] == 0x52 && pkt[7] == 0x41) {
            ++draCount;
            if (pkt[12] == 0x00 && pkt[17] == 0x00 && pkt[18] == 0x00) {
                foundZone0 = true; // encoder 0: x=0
            }
            if (pkt[12] == 0x02 && pkt[17] == 0x01 && pkt[18] == 0x90) {
                foundZone2 = true; // encoder 2: x=400=0x0190
            }
        }
    }
    CHECK(draCount >= 2);
    CHECK(foundZone0);
    CHECK(foundZone2);
}

TEST_CASE(
    "StreamDockControlService: repaintEncodersFromProfile - unbound encoder produces no write",
    "[stream-dock-control][DISPLAY-10][repaint-encoders]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    // Profile with an encoder entry that has neither imagePath nor background.
    core::Profile fakeProfile;
    fakeProfile.id = "enc-unbound-test";
    fakeProfile.name = "Encoder Unbound Test";
    fakeProfile.deviceCodename = "akp05e";
    {
        core::EncoderBinding eb;
        // state.imagePath and state.background both absent -> unbound
        fakeProfile.encoders[0] = std::move(eb);
    }

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&fakeProfile]() -> core::Profile const& { return fakeProfile; },
        nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.repaintEncodersFromProfile();
    drainQueue();

    // Zero additional writes: unbound encoder is skipped.
    CHECK(obs->writeCount() == writeCountAfterOpen);
}

// ===========================================================================
// Held handle (Pitfall 2): transport opened only once across two assigns.
// ===========================================================================

TEST_CASE("StreamDockControlService: transport opened only once across multiple assigns",
          "[stream-dock-control][pitfall-2]") {
    ajazz::tests::qtApp();

    // We need a separate fixture for this test since we track open() calls.
    // MockTransport::open() sets m_open = true (idempotent); Akp05Device::open()
    // is also idempotent (returns early if isOpen()). We verify by checking that
    // the VER GET_FEATURE was called exactly once, proving open() fired once.
    auto fx = makeFixture();
    auto devPtr = fx.device;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    // Assign two different keys.
    QImage img1(85, 85, QImage::Format_RGBA8888);
    img1.fill(qRgba(255, 0, 0, 255));
    QImage img2(85, 85, QImage::Format_RGBA8888);
    img2.fill(qRgba(0, 255, 0, 255));

    svc.assignKeyImage(1, img1);
    drainQueue();
    svc.assignKeyImage(2, img2);
    drainQueue();

    // The transport was opened once (at setActiveDevice); there must be exactly
    // 1 GET_FEATURE call (the VER probe in probeFirmwareVersion) — not 2 or 3.
    // writeFeatureCount() is used for writeFeature(); readFeature() is not counted
    // there. We verify that the VER response queue was consumed exactly once
    // (i.e. only 1 readFeature was called) by checking that readFeature doesn't
    // return data a second time. We confirm by the firmware string being set.
    QString const ver = svc.firmwareVersionFor(QStringLiteral("akp05e"));
    // If transport were opened more than once, the second open would have no
    // VER feature response queued and firmwareVersion() would show "unknown".
    CHECK(ver == QStringLiteral("V3.AKP05E.01.007"));
}
