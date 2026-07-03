// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_streamdeck_sidecar_geometry_contract.cpp
 * @brief Phase 4 (opendeck-ui) cross-check: every app DeviceDescriptor produced by
 *        streamDockSidecarDescriptors() must agree with the mirajazz sidecar's
 *        authoritative wire model in streamdock-host/src/kind.rs.
 *
 * Per CLAUDE.md "Stream Dock backend = mirajazz sidecar", the sidecar's kind.rs
 * (`params_for(vid,pid)` -> {family, key_count, encoder_count}) is the source of
 * truth for the WIRE. The app's DeviceDescriptor restates the same hardware as a
 * RENDERABLE user-facing grid. device.hpp itself flags these duplicated geometry
 * fields as a "DRIFT WARNING ... no compile-time or test enforcement that the two
 * agree". This test is that enforcement for the Stream Dock SKUs the sidecar drives.
 *
 * Two layers, two counts -- reconciled here:
 *   * encoderCount MUST match the sidecar exactly (4 / 3 / 0 for AKP05 / AKP03 / AKP153).
 *   * the sidecar wire-slot count (kind.rs key_count) MUST equal the app's renderable
 *     keyCount + touchZoneCount + the family's documented NON-RENDER wire slots:
 *       - AKP05  : +1  (BAT wire slot 5 is a dead/no-surface slot; kind.rs says
 *                       "10 keys + 4 enc; 15 mirajazz surfaces" -> 10 + 4 + 1 = 15)
 *       - AKP03  : +3  (3 physical side buttons; kind.rs says "9 buttons + 3 enc"
 *                       -> 6 LCD keys + 3 side buttons = 9, no touch zones)
 *       - AKP153 : +0  (15 keys, no encoders, no strip -> 15 + 0 + 0 = 15)
 *
 * If kind.rs and register.cpp ever drift (a new SKU added to one but not the other,
 * a key_count/encoder_count edit), this test fails and forces a deliberate
 * reconciliation in PR review -- the same guardrail the existing
 * test_streamdeck_register_geometry.cpp gives the app side in isolation, now spanning
 * the app<->sidecar boundary.
 *
 * Cross-check captured 2026-06-22 against streamdock-host/src/kind.rs::params_for
 * and src/main.rs::KNOWN_VID_PIDS (16 SKUs, 1:1 with register.cpp).
 */
#include "ajazz/core/device.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

/// One row of the sidecar's authoritative wire contract (kind.rs params_for).
struct SidecarContract {
    std::uint16_t vid;
    std::uint16_t pid;
    int wireKeyCount;       ///< kind.rs key_count (wire-protocol slot count).
    int encoderCount;       ///< kind.rs encoder_count.
    int nonRenderWireSlots; ///< wire slots that are NOT renderable keys/zones (see file header).
};

/// Mirror of streamdock-host/src/kind.rs::params_for, restricted to the fields the
/// app descriptor must agree with. Edit IN LOCKSTEP with kind.rs.
constexpr std::array<SidecarContract, 19> kSidecarContract = {{
    // --- AKP05 / N4 (pv3): 10 keys + 4 zones + 1 dead = 15 wire slots, 4 encoders.
    {0x0300, 0x3004, 15, 4, 1}, // Ajazz AKP05E (hardware-confirmed)
    {0x0300, 0x5001, 15, 4, 1}, // Ajazz AKP05 (provisional)
    {0x6603, 0x1007, 15, 4, 1}, // Mirabox N4
    {0x0300, 0x3013, 15, 4, 1}, // Ajazz AKP05E Pro (provisional, issue #85)
    {0x0300, 0x3014, 15, 4, 1}, // Ajazz AKP05CN Pro (provisional)
    {0x0300, 0x3006, 15, 4, 1}, // Ajazz AKP05 retail (provisional)
    // --- AKP03 / N3 (pv2): 6 LCD keys + 3 side buttons = 9 wire slots, 3 encoders.
    {0x0300, 0x3001, 9, 3, 3}, // Ajazz AKP03 (legacy)
    {0x0300, 0x3002, 9, 3, 3}, // Ajazz AKP03E
    {0x0300, 0x1003, 9, 3, 3}, // Ajazz AKP03R
    {0x0300, 0x3003, 9, 3, 3}, // Ajazz AKP03R (rev.2)
    {0x6602, 0x1002, 9, 3, 3}, // Mirabox N3
    {0x6602, 0x1003, 9, 3, 3}, // Mirabox N3E
    {0x6603, 0x1002, 9, 3, 3}, // Mirabox N3 (rev.3)
    {0x6603, 0x1003, 9, 3, 3}, // Mirabox N3EN
    // --- AKP153 / HSV293S (pv1): 15 keys, no encoders, no strip.
    {0x0300, 0x1001, 15, 0, 0}, // Ajazz AKP153
    {0x0300, 0x1002, 15, 0, 0}, // Ajazz AKP153E
    {0x5548, 0x6674, 15, 0, 0}, // Ajazz AKP153 (Mirabox V1)
    {0x0300, 0x1010, 15, 0, 0}, // Ajazz AKP153E (V2)
    {0x0300, 0x1020, 15, 0, 0}, // Ajazz AKP153R
}};

} // namespace

TEST_CASE("sidecar descriptors agree with the mirajazz wire model (kind.rs)",
          "[registry][geometry][sidecar]") {
    auto const descriptors = ajazz::streamdeck::streamDockSidecarDescriptors();

    // Completeness, both directions: the app must register exactly the SKUs the
    // sidecar drives. A mismatch means a SKU was added to one table but not the
    // other (the silent-divergence class device.hpp's DRIFT WARNING describes).
    REQUIRE(descriptors.size() == kSidecarContract.size());

    for (auto const& c : kSidecarContract) {
        CAPTURE(c.vid, c.pid);

        // Find the matching app descriptor for this VID:PID.
        ajazz::core::DeviceDescriptor const* match = nullptr;
        for (auto const& d : descriptors) {
            if (d.vendorId == c.vid && d.productId == c.pid) {
                match = &d;
                break;
            }
        }
        REQUIRE(match != nullptr); // every sidecar SKU has an app descriptor

        CAPTURE(match->codename);

        // (1) encoderCount must match the wire model EXACTLY.
        REQUIRE(static_cast<int>(match->encoderCount) == c.encoderCount);

        // (2) wire slots = renderable keys + touch zones + documented non-render slots.
        int const renderablePlusReserved = static_cast<int>(match->keyCount) +
                                           static_cast<int>(match->touchZoneCount) +
                                           c.nonRenderWireSlots;
        REQUIRE(renderablePlusReserved == c.wireKeyCount);
    }
}
