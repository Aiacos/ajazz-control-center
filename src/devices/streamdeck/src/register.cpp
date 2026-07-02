// SPDX-License-Identifier: GPL-3.0-or-later
/** @file register.cpp
 *  @brief Device registration for all AJAZZ Stream Dock variants.
 *
 *  Calls @ref core::DeviceRegistry::registerDevice() for every known AKP
 *  device family so that the registry can match USB enumeration results to
 *  the correct factory function at runtime.
 *
 *  The USB identifier matrix below cross-references three independent
 *  reverse-engineering catalogues:
 *
 *  - `[ajazz-sdk]` — `mishamyrt/ajazz-sdk` (AJAZZ-branded SKUs)
 *  - `[opendeck-akp03]` — `4ndv/opendeck-akp03` (Mirabox + rebadge SKUs)
 *  - `[opendeck-akp05]` — `naerschhersch/opendeck-akp05` (Mirabox N4)
 *
 *  Tag definitions live in
 *  `docs/protocols/streamdeck/_research-sources.md`. The per-device
 *  details (geometry, features, edge cases) are documented in
 *  `docs/protocols/streamdeck/{akp153,akp03,akp05,akp815}.md`.
 *
 *  @note Pre-2026-05-14 the registry contained two USB pairs (`0x0300:0x1001`
 *        for AKP153 and `0x0300:0x3001` for AKP03) that conflict with the
 *        canonical mapping in `[ajazz-sdk]` (`0x5548:0x6674` for AKP153,
 *        `0x0300:0x1001` for AKP03). To avoid breaking deployments that
 *        already work against the legacy pairs we keep them registered
 *        **and** register the canonical ones in parallel; runtime
 *        enumeration picks the first match.
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp815_protocol.hpp"

namespace ajazz::streamdeck {

namespace {

// Mirabox V1 vendor ID per `[ajazz-sdk]/protocol/codes.rs::VENDOR_ID_MIRABOX_V1`.
inline constexpr std::uint16_t MiraboxVendorV1 = 0x5548;

// AKP815 PID canonicalised by `[ajazz-sdk]/protocol/codes.rs`.
inline constexpr std::uint16_t Akp815V1Pid = 0x6672;

} // namespace

std::vector<core::DeviceDescriptor> streamDockSidecarDescriptors() {
    // Descriptors for every Stream Dock SKU the mirajazz sidecar drives (AKP05/
    // N4, AKP03/N3, AKP153/HSV293S). Mirrors registerAll's matrix exactly but
    // uses LITERAL geometry — no akp0X:: constants — so it survives the removal
    // of the C++ wire headers (experiment/mirajazz Slice D). hasClock is false:
    // the sidecar backend has no IClockCapable, and the Stream Dock family has
    // no firmware RTC (DEVICES-11 / ARCH-05). The app registers these against
    // makeSidecarStreamDock; AKP815 stays on its C++ backend (not a mirajazz
    // device) and is intentionally absent here.
    using core::DeviceDescriptor;
    using core::DeviceFamily;
    std::vector<DeviceDescriptor> out;

    auto akp153 = [](std::uint16_t vid, std::uint16_t pid, char const* model, char const* code) {
        return DeviceDescriptor{.vendorId = vid,
                                .productId = pid,
                                .family = DeviceFamily::StreamDeck,
                                .model = model,
                                .codename = code,
                                .keyCount = 15,
                                .gridColumns = 5,
                                .encoderCount = 0,
                                .keyRows = 3};
    };
    auto akp03 = [](std::uint16_t vid, std::uint16_t pid, char const* model, char const* code) {
        return DeviceDescriptor{.vendorId = vid,
                                .productId = pid,
                                .family = DeviceFamily::StreamDeck,
                                .model = model,
                                .codename = code,
                                .keyCount = 6,
                                .gridColumns = 3,
                                .encoderCount = 3,
                                .keyRows = 2};
    };
    auto akp05 = [](std::uint16_t vid, std::uint16_t pid, char const* model, char const* code) {
        return DeviceDescriptor{.vendorId = vid,
                                .productId = pid,
                                .family = DeviceFamily::StreamDeck,
                                .model = model,
                                .codename = code,
                                .keyCount = 10,
                                .gridColumns = 5,
                                .encoderCount = 4,
                                .hasTouchStrip = true,
                                .keyRows = 2,
                                .touchZoneCount = 4};
    };

    // AKP153 family (15 keys, no encoders). 0x0300:0x1001 wins over AKP03.
    out.push_back(akp153(0x0300, 0x1001, "AJAZZ AKP153 / Mirabox HSV293S", "akp153"));
    out.push_back(akp153(0x0300, 0x1002, "AJAZZ AKP153E", "akp153e"));
    out.push_back(akp153(0x5548, 0x6674, "AJAZZ AKP153 (Mirabox V1)", "akp153_v1"));
    out.push_back(akp153(0x0300, 0x1010, "AJAZZ AKP153E (Mirabox V2)", "akp153e_v2"));
    out.push_back(akp153(0x0300, 0x1020, "AJAZZ AKP153R", "akp153r"));
    // AKP03 / N3 family (6 LCD keys + 3 side buttons + 3 encoders).
    out.push_back(akp03(0x0300, 0x3001, "AJAZZ AKP03 (legacy firmware)", "akp03_legacy"));
    out.push_back(akp03(0x0300, 0x3002, "AJAZZ AKP03E", "akp03e"));
    out.push_back(akp03(0x0300, 0x1003, "AJAZZ AKP03R", "akp03r"));
    out.push_back(akp03(0x0300, 0x3003, "AJAZZ AKP03R rev. 2", "akp03r_rev2"));
    out.push_back(akp03(0x6602, 0x1002, "Mirabox N3 (rev. 1)", "mirabox_n3"));
    out.push_back(akp03(0x6602, 0x1003, "Mirabox N3E (rev. 1)", "mirabox_n3e"));
    out.push_back(akp03(0x6603, 0x1002, "Mirabox N3 (rev. 3)", "mirabox_n3_rev3"));
    out.push_back(akp03(0x6603, 0x1003, "Mirabox N3EN", "mirabox_n3en"));
    // AKP05 / N4 family (10 keys + 4 encoders + 4 touch zones).
    out.push_back(akp05(0x0300, 0x5001, "AJAZZ AKP05 (provisional)", "akp05"));
    out.push_back(akp05(0x6603, 0x1007, "Mirabox N4 / AJAZZ AKP05 family", "mirabox_n4"));
    out.push_back(akp05(0x0300, 0x3004, "AJAZZ AKP05E (Stream Dock Plus)", "akp05e"));

    return out;
}

/** @brief Register all known Stream Dock device descriptors with the
 *         caller-owned @ref core::DeviceRegistry.
 *
 *  Must be called once during application initialisation (before USB
 *  enumeration begins).  Subsequent calls are safe but redundant — the
 *  registry silently replaces duplicate VID/PID entries.
 *
 *  @param registry Registry to populate (audit finding A1 replaced the
 *         implicit singleton lookup with constructor injection).
 *
 *  @see core::DeviceRegistry::registerDevice()
 *  @see makeAkp815(), streamDockSidecarDescriptors()
 */
void registerAll(core::DeviceRegistry& registry) {
    auto& reg = registry;

    // A-03 / D-03: every Stream Dock backend inherits IClockCapable (Plan
    // 05-02), so every descriptor row below sets its hasClock flag inline.
    // The per-row repetition is intentional — it keeps capability
    // advertisement visible at every row so a future contributor adding a new
    // row cannot accidentally regress to a hasClock-false row.

    // NOTE: AKP153 / AKP03 / AKP05 are driven by the out-of-process mirajazz
    // sidecar (see streamDockSidecarDescriptors() + makeSidecarStreamDock in the
    // app bootstrap); their in-tree C++ wire backends were removed in
    // experiment/mirajazz Slice D. AKP815 is NOT a mirajazz device and keeps its
    // C++ backend, registered below.

    // ---- AKP815 (15 LCD keys, 5×3 grid, JPEG 100×100, LCD strip) -------
    //
    // AKP815 uses its own makeAkp815 factory (Akp815Device).
    // Per-revision geometry differences (100×100 key image, 800×480 strip) are
    // handled there; see akp815.cpp.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = MiraboxVendorV1,
            .productId = Akp815V1Pid,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP815",
            .codename = "akp815",
            .keyCount = akp815::KeyCount,
            .gridColumns = akp815::KeyCols,
            .encoderCount = 0,
            .hasClock = true, // A-03 / D-03: AKP815 has a firmware RTC. Note: AKP05E is
                              // hasClock=false per DEVICES-11 / ARCH-05 -- check per-SKU
                              // before assuming all Stream Docks support Clock.
        },
        &makeAkp815);

    // -------------------------------------------------------------------------
    // V25 (2025-revision) + OEM rebadge codenames AWAITING PID DISCOVERY.
    //
    // Stream Dock vendor SDK (SDLibrary1.dll) lists ~96 codenames across 7
    // silicon families (see `docs/protocols/streamdeck/akp_device_matrix.md`).
    // The PIDs below are NOT yet known; entries will be added here as soon as
    // a real-device hot-plug capture surfaces them. Until then, an unknown
    // VID:PID match logs an "unsupported device" warning rather than picking
    // up the right family. The codename strings remain available for future
    // log/UI cross-reference.
    //
    // AKP03 family (6-key + 3-encoder, mirajazz sidecar):
    //   AKP03V25, AKP03EV25, AKP03RV25, SD12N3V25, TS16N3V25, VSDN3,
    //   MBox-N3V25, MBox-N3EV25, MBox-N3 EV25, MSD-TWOV25, OMNIDIALV25
    //
    // AKP05 family (10-key + 4-encoder + strip, mirajazz sidecar):
    //   AKP05V25, AKP05EV25, AKP05RV25, MBox-N4Pro, MBox-N4ProE, MBox-N6,
    //   N4Pro, N4ProE, N4V25, MSDPRO, MSDNEO, SD14N4V25, TS10N4V25, VSDN4,
    //   VSDN4Pro, BRHubN4, BRHubN4Pro
    //
    // AKP815 family (15-key 5x3 + 800×480 strip):
    //   TS183 (rebadge)
    // -------------------------------------------------------------------------
}

} // namespace ajazz::streamdeck
