// SPDX-License-Identifier: GPL-3.0-or-later
/** @file test_fake_stream_dock_device.cpp
 *  @brief Self-test for the in-process FakeStreamDockDevice unit-test fixture.
 *
 *  Confirms the fixture honours the core::IDevice + IDisplayCapable +
 *  IEncoderCapable contracts (open/close lifecycle, call recording, input
 *  injection) so the ~10 app suites can migrate onto it. ASCII-only names.
 */
#include "fixtures/fake_stream_dock_device.hpp"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::tests::FakeStreamDockDevice;
namespace core = ajazz::core;

namespace {

core::DeviceDescriptor akp05eDesc() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "Fake AKP05E";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.gridColumns = 5;
    d.encoderCount = 4;
    d.keyRows = 2;
    return d;
}

} // namespace

TEST_CASE("FakeStreamDockDevice open/close lifecycle", "[fake-device]") {
    FakeStreamDockDevice dev(akp05eDesc(), {0x0300, 0x3004, "FAKE1"});
    REQUIRE_FALSE(dev.isOpen());
    dev.open();
    REQUIRE(dev.isOpen());
    REQUIRE(dev.openCount == 1);
    dev.close();
    REQUIRE_FALSE(dev.isOpen());
    REQUIRE(dev.closeCount == 1);
    REQUIRE(dev.id().serial == "FAKE1");
    REQUIRE(dev.descriptor().codename == "akp05e");
}

TEST_CASE("FakeStreamDockDevice records display calls", "[fake-device]") {
    FakeStreamDockDevice dev(akp05eDesc(), {0x0300, 0x3004, "FAKE2"});
    std::vector<std::uint8_t> const rgba(112 * 112 * 4, 0xAB);

    dev.setKeyImage(3, rgba, 112, 112);
    dev.setBrightness(60);
    dev.clearKey(0xFF);
    dev.setEncoderImage(2, rgba, 128, 128);
    dev.flush();

    REQUIRE(dev.keyImages.size() == 1);
    REQUIRE(dev.keyImages[0].index == 3);
    REQUIRE(dev.keyImages[0].byteCount == rgba.size());
    REQUIRE(dev.brightnessCalls == std::vector<std::uint8_t>{60});
    REQUIRE(dev.clearedKeys == std::vector<std::uint8_t>{0xFF});
    REQUIRE(dev.encoderImages.size() == 1);
    REQUIRE(dev.encoderImages[0].index == 2);
    REQUIRE(dev.flushCount == 1);
}

TEST_CASE("FakeStreamDockDevice injects input through the callback", "[fake-device]") {
    FakeStreamDockDevice dev(akp05eDesc(), {0x0300, 0x3004, "FAKE3"});

    std::vector<core::DeviceEvent> seen;
    dev.onEvent([&seen](core::DeviceEvent const& e) { seen.push_back(e); });

    dev.injectEvent({core::DeviceEvent::Kind::KeyPressed, 4, 1});
    dev.injectEvent({core::DeviceEvent::Kind::EncoderTurned, 1, -1});

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0].kind == core::DeviceEvent::Kind::KeyPressed);
    REQUIRE(seen[0].index == 4);
    REQUIRE(seen[1].kind == core::DeviceEvent::Kind::EncoderTurned);
    REQUIRE(seen[1].value == -1);
}

TEST_CASE("FakeStreamDockDevice exposes geometry from its descriptor", "[fake-device]") {
    FakeStreamDockDevice dev(akp05eDesc(), {0x0300, 0x3004, "FAKE4"});
    REQUIRE(dev.displayInfo().keyCols == 5);
    REQUIRE(dev.displayInfo().keyRows == 2);
    REQUIRE(dev.encoderInfo().count == 4);
    REQUIRE(dev.encoderInfo().hasScreens);
}
