// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file register.cpp
 * @brief Populates the caller-owned DeviceRegistry with known AJAZZ mouse VID/PID pairs.
 *
 * All listed models share the AJ-series backend (makeAjSeries()). The table
 * is the authoritative source of VID/PID mappings for mouse devices and
 * mirrors `docs/_data/devices.yaml` — keep the two in sync.
 *
 * VID:PID provenance (2026-04-29 vendor static-analysis pass):
 *   - VID 0x248A / 0x249A — AJ139 / AJ159 / AJ179 family, observed in the
 *     vendor's `AJAZZ Driver (X)` driver `app/config.xml`. PIDs:
 *       wired:  0x5C2E (primary), 0x5D2E (alt mode 2), 0x5E2E (alt mode 3)
 *       dongle: 0x5C2F (under both VID 0x248A and 0x249A)
 *   - VID 0x3554 — AJ199 family, observed in the AJ199 Max `Mouse Drive
 *     Beta` driver `app/Config.ini` (base64-encoded VID/PID strings; full
 *     decode in `docs/research/vendor-protocol-notes.md` Finding 8).
 *     Wired PIDs (M_PID): 0xF500 / 0xF566 / 0xF546.
 *     Dongle PIDs (D_PID): 0xF501 / 0xF564 / 0xF567 / 0xF545 / 0xF547 / 0xF5D5.
 *
 * Pre-2026-04-29 history: the four entries (aj159 / aj199 / aj339 / aj380)
 * were registered under VID 0x3554 with sequential PIDs 0xF51A-D. None of
 * those values exists in any vendor manifest — they were fictional. The
 * AJ339 / AJ380 SKUs are no longer enumerated until a real-device capture
 * surfaces their VID:PID. Tracking entry: TODO.md →
 * "AJ-series VID:PID enumeration drift".
 *
 * @warning The wire format implemented in `aj_series.cpp` is currently
 *          NOT confirmed to match what the vendor driver sends — see
 *          `docs/research/vendor-protocol-notes.md` Finding 11.A. This
 *          file fixes only the enumeration so devices can be detected;
 *          configuration write commands may still no-op silently until
 *          the wire-format reconciliation runs. Tracking entry: TODO.md →
 *          "AJ-series wire format reconciliation".
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/mouse/mouse.hpp"

namespace ajazz::mouse {

void registerAll(core::DeviceRegistry& registry) {
    auto& reg = registry;

    static constexpr struct {
        std::uint16_t vid;
        std::uint16_t pid;
        char const* model;
        char const* codename;
    } kMice[] = {
        // AJ139 / AJ159 / AJ179 family (USB wired modes + 2.4GHz dongle).
        // Each (vid, pid) tuple covers all 8 SKUs in the family at the USB
        // layer; the specific SKU is identified at runtime via a vendor-
        // specific dev_id readback (M129 / M620 / M630 / M179 / M139 / …).
        {0x248A, 0x5C2E, "AJAZZ AJ-series wired (primary)", "aj_series_wired_primary"},
        {0x248A, 0x5D2E, "AJAZZ AJ-series wired (alt mode 5D2E)", "aj_series_wired_alt"},
        {0x248A, 0x5E2E, "AJAZZ AJ-series wired (alt mode 5E2E)", "aj_series_wired_alt2"},
        {0x248A, 0x5C2F, "AJAZZ AJ-series 2.4GHz dongle", "aj_series_dongle"},
        // AJ199 family (separate VID space).
        {0x3554, 0xF500, "AJAZZ AJ199 family (wired)", "aj199_family"},
        {0x3554, 0xF501, "AJAZZ AJ199 family (2.4GHz dongle)", "aj199_family_dongle"},
    };

    for (auto const& m : kMice) {
        reg.registerDevice(
            core::DeviceDescriptor{
                .vendorId = m.vid,
                .productId = m.pid,
                .family = core::DeviceFamily::Mouse,
                .model = m.model,
                .codename = m.codename,
                // AJ-series mice expose 6 DPI stages with per-stage RGB
                // (AJ159 device manifest `report_max="4"` actually limits
                // the polling rate to 4 stages, NOT the DPI count).
                .dpiStageCount = 6,
                .hasRgb = true,
            },
            &makeAjSeries);
    }
}

} // namespace ajazz::mouse
