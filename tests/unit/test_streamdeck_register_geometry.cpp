// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_streamdeck_register_geometry.cpp
 * @brief REQ-26-D regression test: every non-deferred LCD-key StreamDeck descriptor
 *        must carry explicit geometry fields (keyRows > 0) after registerAll().
 *
 * Iterates the registry, filters to StreamDeck family with LCD keys, skips the
 * kDeferredLcdSkus allow-list (Phase 26 D-13: AKP815 deferred sentinel), and
 * asserts that each remaining descriptor has keyRows populated. Also asserts the
 * aux-surface invariant: any descriptor with hasTouchStrip=true must declare either
 * touchZoneCount > 0 (zone-model like AKP05's 4 encoder-aligned zones) OR non-zero
 * mainScreenWidthPx + mainScreenHeightPx (wide-rect model like AKP815's 800x480 when
 * reactivated). AKP153 / AKP03 satisfy trivially (hasTouchStrip=false). This catches
 * the real regression class -- a contributor flipping hasTouchStrip=true without
 * populating zone count or wide-rect dimensions silently fails the geometry-driven
 * renderer introduced in Phase 26.
 *
 * AKP815: stays at keyRows=0 (deferred sentinel per D-13). Removing "akp815" from
 * kDeferredLcdSkus is an explicit code change visible in PR review, and requires
 * populating mainScreenWidthPx + mainScreenHeightPx (800x480 strip) on that row.
 */
#include "ajazz/core/device.hpp"
#include "ajazz/core/device_registry.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <array>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

// REQ-26-D: allow-list for LCD-key StreamDeck SKUs intentionally deferred
// (keyRows = 0 sentinel per D-13). Removing a codename from this list is an
// explicit code change visible in PR review; the removed codename must then have
// its descriptor row populated with correct geometry.
// D-13: AKP815 deferred -- its 800x480 strip is one wide rect, not 4 discrete
// zones. Reactivate when mainScreenWidthPx + mainScreenHeightPx are filled in.
static constexpr std::array kDeferredLcdSkus = {
    std::string_view{"akp815"},
};

TEST_CASE("streamdeck LCD-key descriptors have geometry fields", "[registry][geometry]") {
    ajazz::core::DeviceRegistry registry;
    ajazz::streamdeck::registerAll(registry);

    for (auto const& d : registry.enumerate()) {
        // Only check StreamDeck family LCD-key devices.
        if (d.family != ajazz::core::DeviceFamily::StreamDeck) {
            continue;
        }
        if (d.keyCount == 0) {
            continue; // Not an LCD-key SKU.
        }

        // Skip SKUs in the deferred allow-list (D-13 sentinel).
        bool deferred = false;
        for (auto const sv : kDeferredLcdSkus) {
            if (d.codename == sv) {
                deferred = true;
                break;
            }
        }
        if (deferred) {
            continue;
        }

        // REQ-26-D: every non-deferred LCD-key descriptor must declare explicit rows.
        REQUIRE(d.keyRows > 0);

        // Aux-surface invariant (T-26-04 mitigation): any descriptor with
        // hasTouchStrip=true must declare geometry for that strip -- either N
        // discrete touch zones (zone-model, like AKP05's 4 zones per encoder) OR
        // a single wide-rect strip (wide-rect model, like AKP815's 800x480 when
        // reactivated). AKP153 / AKP03 have hasTouchStrip=false, so they satisfy
        // trivially. Physical rotary encoders without an LCD strip (AKP03) are NOT
        // touch-surface geometry -- only hasTouchStrip signals that a strip exists.
        if (d.hasTouchStrip) {
            bool has_zone_model = d.touchZoneCount > 0;
            bool has_rect_model = (d.mainScreenWidthPx > 0 && d.mainScreenHeightPx > 0);
            REQUIRE((has_zone_model || has_rect_model));
        }
    }
}
