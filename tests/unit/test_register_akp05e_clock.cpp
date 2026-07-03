// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_register_akp05e_clock.cpp
 * @brief Descriptor honesty test for DEVICES-11 / ARCH-05.
 *
 * Asserts that the Stream Dock SKUs driven by the mirajazz sidecar advertise
 * hasClock==false so the runtime Sync-button affordance is never shown for the
 * Stream Dock family (which has no firmware RTC per ARCH-05).
 *
 * The AKP03/AKP05/AKP153 C++ wire backends (which carried an IClockCapable
 * NotImplemented stub) were removed in favour of the sidecar
 * (experiment/mirajazz Slice D); their descriptors now come from
 * streamDockSidecarDescriptors(), which sets hasClock=false uniformly because
 * the sidecar backend exposes no IClockCapable at all. This supersedes the
 * earlier "akp05e false but mirabox_n4 still true" deferred-flip guard — the
 * whole family is now honestly clock-less.
 *
 * Tags: [stream-dock-control][register]
 */
#include "ajazz/streamdeck/streamdeck.hpp"

#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("akp05e_clock: sidecar Stream Dock descriptors advertise hasClock==false (DEVICES-11)",
          "[stream-dock-control][register]") {
    auto const descriptors = ajazz::streamdeck::streamDockSidecarDescriptors();

    auto findByCodename = [&descriptors](std::string const& codename) {
        return std::find_if(descriptors.begin(), descriptors.end(), [&codename](auto const& d) {
            return d.codename == codename;
        });
    };

    SECTION("akp05e entry exists and has hasClock==false") {
        auto const it = findByCodename("akp05e");
        REQUIRE(it != descriptors.end());
        CHECK(!it->hasClock);
    }

    SECTION("mirabox_n4 entry exists and has hasClock==false (whole family is clock-less)") {
        auto const it = findByCodename("mirabox_n4");
        REQUIRE(it != descriptors.end());
        CHECK(!it->hasClock);
    }
}
