// Unit tests for SidecarStreamDockDevice — the app-side proxy for the mirajazz Rust sidecar.
//
// Before this file the class had NO dedicated unit test (only the offscreen QML smoke target
// *compiled* it). These cover the process-free surface: construction from a real sidecar
// DeviceDescriptor, the capability-interface wiring, descriptor-derived geometry, and that the
// pre-open lifecycle (incl. the keep-alive guard) is crash-safe without a spawned sidecar.
//
// The keep-alive WIRE command (buildKeepAlive) is unit-tested in test_sidecar_protocol.cpp;
// keepAlive() short-circuits while the device is closed (no process), so here we assert it is a
// safe no-op in that state rather than re-asserting the codec.

#include "sidecar_stream_dock_device.hpp"

#include <vector>

#include <ajazz/core/capabilities.hpp>
#include <ajazz/core/device.hpp>
#include <ajazz/streamdeck/streamdeck.hpp>
#include <catch2/catch_test_macros.hpp>

using ajazz::app::SidecarStreamDockDevice;
using ajazz::core::DeviceDescriptor;
using ajazz::core::DeviceId;

namespace {

/// Pick a sidecar descriptor that has encoders + a touch strip (the AKP05/N4 family) so the
/// encoder/touch capability assertions are meaningful.
DeviceDescriptor pickEncoderDescriptor() {
    auto const descs = ajazz::streamdeck::streamDockSidecarDescriptors();
    REQUIRE_FALSE(descs.empty());
    for (auto const& d : descs) {
        if (d.encoderCount > 0) {
            return d;
        }
    }
    return descs.front();
}

} // namespace

TEST_CASE("SidecarStreamDockDevice constructs closed and exposes its descriptor",
          "[sidecar][device]") {
    auto const desc = pickEncoderDescriptor();
    SidecarStreamDockDevice dev(desc, DeviceId{desc.vendorId, desc.productId, "TEST-SERIAL"});

    REQUIRE_FALSE(dev.isOpen()); // not open until open() spawns the sidecar
    REQUIRE(dev.descriptor().codename == desc.codename);
    REQUIRE(dev.id().vendorId == desc.vendorId);
    REQUIRE(dev.id().productId == desc.productId);
    REQUIRE(dev.firmwareVersion() == "unknown"); // placeholder until a `connected` event arrives
}

TEST_CASE("SidecarStreamDockDevice implements the display/encoder/touch-strip capabilities",
          "[sidecar][device]") {
    auto const desc = pickEncoderDescriptor();
    SidecarStreamDockDevice dev(desc, DeviceId{desc.vendorId, desc.productId, "TEST-SERIAL"});

    // The capability interfaces are reachable (the registry/services dynamic_cast to these).
    REQUIRE(dynamic_cast<ajazz::core::IDisplayCapable*>(&dev) != nullptr);
    REQUIRE(dynamic_cast<ajazz::core::IEncoderCapable*>(&dev) != nullptr);
    REQUIRE(dynamic_cast<ajazz::core::ITouchStripDisplayCapable*>(&dev) != nullptr);

    // Encoder count is descriptor-driven, not hardcoded.
    REQUIRE(dev.encoderInfo().count == static_cast<std::uint8_t>(desc.encoderCount));
}

TEST_CASE("SidecarStreamDockDevice output calls are safe no-ops while closed",
          "[sidecar][device]") {
    auto const desc = pickEncoderDescriptor();
    SidecarStreamDockDevice dev(desc, DeviceId{desc.vendorId, desc.productId, "TEST-SERIAL"});

    // None of these may crash or write when there is no spawned sidecar (isOpen()==false).
    std::vector<std::uint8_t> rgba(4, 0);
    REQUIRE_NOTHROW(dev.setBrightness(40));
    REQUIRE_NOTHROW(dev.keepAlive()); // WR-05: the formerly-no-op guard must stay crash-safe closed
    REQUIRE_NOTHROW(dev.setKeyImage(0, rgba, 1, 1));
    REQUIRE_NOTHROW(dev.flush());
    REQUIRE_FALSE(dev.isOpen());
}
