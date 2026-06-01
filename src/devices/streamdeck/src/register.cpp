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
#include "akp03_protocol.hpp"
#include "akp05_protocol.hpp"
#include "akp153_protocol.hpp"
#include "akp815_protocol.hpp"

namespace ajazz::streamdeck {

namespace {

// Mirabox V1 vendor ID per `[ajazz-sdk]/protocol/codes.rs::VENDOR_ID_MIRABOX_V1`.
inline constexpr std::uint16_t MiraboxVendorV1 = 0x5548;

// Mirabox N3 family old vendor ID surfaced by `[opendeck-akp03]`.
inline constexpr std::uint16_t MiraboxN3VendorOld = 0x6602;

// VID 0x6603 is used by both Mirabox N3 (AKP03 protocol) and N4 (AKP05 protocol)
// families per `[opendeck-akp03]` and `[opendeck-akp05]`. PID distinguishes the family:
//   N3 family: 0x1002 (N3 rev.3), 0x1003 (N3EN)
//   N4 family: 0x1007 (canonical Mirabox N4 / AKP05)
inline constexpr std::uint16_t MiraboxVendorV2 = 0x6603;

// Mirabox N4 (AKP05 family) PID per `[opendeck-akp05]/40-opendeck-akp05.rules`.
inline constexpr std::uint16_t MiraboxN4Pid = 0x1007;

// AKP153 / AKP815 PIDs canonicalised by `[ajazz-sdk]/protocol/codes.rs`.
inline constexpr std::uint16_t Akp153V1Pid = 0x6674;
inline constexpr std::uint16_t Akp815V1Pid = 0x6672;
inline constexpr std::uint16_t Akp153EV2Pid = 0x1010;
inline constexpr std::uint16_t Akp153RV2Pid = 0x1020;

// AJAZZ AKP05E "Stream Dock Plus" retail PID. Surfaced via real-device
// hot-plug capture 2026-05-13 as "Ajazz HOTSPOTEKUSB HID DEMO"; the
// white-label DEMO string initially led us to mis-file it as a 6-key AKP03
// sibling. A live `CRT VER` handshake on 2026-05-20 returned the firmware
// string "V3.AKP05E.01.007", confirming it is an AKP05E (protocol_version 3,
// 10 LCD keys + 4 encoders + touch strip). Registered against `makeAkp05`
// in the AKP05 section below.
inline constexpr std::uint16_t Akp05ePid = 0x3004;

// Generic AKP153 grid descriptor used by every 15-key variant.
constexpr auto
akp153_descriptor(std::uint16_t vid, std::uint16_t pid, char const* model, char const* codename) {
    return core::DeviceDescriptor{
        .vendorId = vid,
        .productId = pid,
        .family = core::DeviceFamily::StreamDeck,
        .model = model,
        .codename = codename,
        .keyCount = akp153::KeyCount,
        .gridColumns = 5,
        .encoderCount = 0,
        .keyRows = 3, // 3×5 landscape grid (REQ-26-C)
    };
}

// Generic AKP03 / N3 descriptor — 6 LCD keys exposed; the 3 side buttons
// and 3 encoders are surfaced through the IDevice event stream and the
// IEncoderCapable mix-in but do not appear in `keyCount`.
constexpr auto
akp03_descriptor(std::uint16_t vid, std::uint16_t pid, char const* model, char const* codename) {
    return core::DeviceDescriptor{
        .vendorId = vid,
        .productId = pid,
        .family = core::DeviceFamily::StreamDeck,
        .model = model,
        .codename = codename,
        .keyCount = akp03::DisplayKeyCount,
        .gridColumns = 3,
        .encoderCount = akp03::EncoderCount,
        .keyRows = 2, // 2×3 grid (REQ-26-C)
    };
}

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
 *  @see makeAkp153(), makeAkp03(), makeAkp05(), makeAkp815()
 */
void registerAll(core::DeviceRegistry& registry) {
    auto& reg = registry;

    // A-03 / D-03: every Stream Dock backend inherits IClockCapable (Plan
    // 05-02), so every descriptor row below sets its hasClock flag inline.
    // The per-row repetition is intentional — it keeps capability
    // advertisement visible at every row so a future contributor adding a new
    // row cannot accidentally regress to a hasClock-false row.

    // ---- AKP153 family (15 LCD keys, 3×5 grid, JPEG 85×85) --------------

    // Legacy descriptor we shipped before the 2026-05-14 research pass.
    // Preserved for any deployment that already enumerated under this pair.
    {
        auto d = akp153_descriptor(akp153::VendorId,
                                   akp153::ProductIdInternational,
                                   "AJAZZ AKP153 / Mirabox HSV293S",
                                   "akp153");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp153);
    }
    {
        auto d = akp153_descriptor(
            akp153::VendorId, akp153::ProductIdChinese, "AJAZZ AKP153E", "akp153e");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp153);
    }

    // Canonical Mirabox V1 vendor pair per `[ajazz-sdk]`.
    {
        auto d = akp153_descriptor(
            MiraboxVendorV1, Akp153V1Pid, "AJAZZ AKP153 (Mirabox V1 firmware)", "akp153_v1");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp153);
    }

    // AKP153E (China) — canonical PID is `0x1010` per `[ajazz-sdk]`.
    {
        auto d = akp153_descriptor(
            akp153::VendorId, Akp153EV2Pid, "AJAZZ AKP153E (Mirabox V2 firmware)", "akp153e_v2");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp153);
    }

    // AKP153R — regional revision, same protocol behind a new PID.
    {
        auto d = akp153_descriptor(akp153::VendorId, Akp153RV2Pid, "AJAZZ AKP153R", "akp153r");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp153);
    }

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

    // ---- AKP03 family (6 LCD keys + 3 side buttons + 3 encoders) ------

    // Legacy `0x0300:0x3001` we shipped originally. Kept registered to
    // preserve hot-plug compatibility for any deployment running against
    // an early firmware that reports this PID; canonical is `0x1001`.
    {
        auto d = akp03_descriptor(
            akp03::VendorId, 0x3001, "AJAZZ AKP03 (legacy firmware)", "akp03_legacy");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }

    // Canonical AKP03 family per `[ajazz-sdk]`.
    {
        auto d = akp03_descriptor(
            akp03::VendorId, akp03::ProductIdAkp03, "AJAZZ AKP03 / Mirabox N3", "akp03");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }
    {
        auto d =
            akp03_descriptor(akp03::VendorId, akp03::ProductIdAkp03E, "AJAZZ AKP03E", "akp03e");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }
    {
        auto d =
            akp03_descriptor(akp03::VendorId, akp03::ProductIdAkp03R, "AJAZZ AKP03R", "akp03r");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }
    {
        auto d = akp03_descriptor(
            akp03::VendorId, akp03::ProductIdAkp03RRev2, "AJAZZ AKP03R rev. 2", "akp03r_rev2");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }

    // Mirabox N3 rebrands per `[opendeck-akp03]`.
    {
        auto d = akp03_descriptor(MiraboxN3VendorOld, 0x1002, "Mirabox N3 (rev. 1)", "mirabox_n3");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }
    // MBox-N3E (rev 1, old-vendor variant) — surfaced by Stream Dock vendor
    // deep RE 2026-05-17 (`docs/protocols/streamdeck/akp_device_matrix.md`
    // §4). Was missing from prior catalogue; the new-vendor sibling
    // (`MiraboxVendorV2 + 0x1003` = mirabox_n3en) was already registered.
    {
        auto d =
            akp03_descriptor(MiraboxN3VendorOld, 0x1003, "Mirabox N3E (rev. 1)", "mirabox_n3e");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }
    {
        auto d =
            akp03_descriptor(MiraboxVendorV2, 0x1002, "Mirabox N3 (rev. 3)", "mirabox_n3_rev3");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }
    {
        auto d = akp03_descriptor(MiraboxVendorV2, 0x1003, "Mirabox N3EN", "mirabox_n3en");
        d.hasClock = true;
        reg.registerDevice(d, &makeAkp03);
    }

    // ---- AKP05 / Mirabox N4 (10 LCD keys + 4 encoders + touch strip) --

    // Legacy provisional pair `0x0300:0x5001` — no public source.
    // Kept registered until removed in a future cleanup. Tracking:
    // `TODO.md` → "AKP05 placeholder VID:PID retirement".
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = akp05::VendorId,
            .productId = akp05::ProductId,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP05 / AKP05E (provisional)",
            .codename = "akp05",
            .keyCount = akp05::KeyCount,
            .gridColumns = akp05::KeyCols,
            .encoderCount = akp05::EncoderCount,
            .hasTouchStrip = true,
            .hasClock = true,    // A-03 / D-03: AKP05 backend inherits IClockCapable (Plan 05-02).
            .keyRows = 2,        // 2x5 grid; akp05::KeyRows = 2 (REQ-26-C)
            .touchZoneCount = 4, // 4 zones aligned to encoders; akp05::TouchZoneCount
            .mainScreenWidthPx = 0, // strip is 4 discrete zones, not one wide rect
            .mainScreenHeightPx = 0,
        },
        &makeAkp05);

    // Canonical Mirabox N4 USB ID from `[opendeck-akp05]`. We treat this
    // as the authoritative match for the AKP05 / N4 family.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = MiraboxVendorV2,
            .productId = MiraboxN4Pid,
            .family = core::DeviceFamily::StreamDeck,
            .model = "Mirabox N4 / AJAZZ AKP05 family",
            .codename = "mirabox_n4",
            .keyCount = akp05::KeyCount,
            .gridColumns = akp05::KeyCols,
            .encoderCount = akp05::EncoderCount,
            .hasTouchStrip = true,
            .hasClock = true,    // A-03 / D-03: shares Akp05Device backend -> same IClockCapable.
            .keyRows = 2,        // 2x5 grid; akp05::KeyRows = 2 (REQ-26-C)
            .touchZoneCount = 4, // 4 zones aligned to encoders; akp05::TouchZoneCount
            .mainScreenWidthPx = 0, // strip is 4 discrete zones, not one wide rect
            .mainScreenHeightPx = 0,
        },
        &makeAkp05);

    // AJAZZ AKP05E retail unit, USB `0x0300:0x3004`. Firmware-confirmed via a
    // live `CRT VER` handshake 2026-05-20 (response "V3.AKP05E.01.007"). This
    // was previously mis-filed as `akp03_variant_3004` and routed through
    // `makeAkp03` (6 keys / 3 encoders), which addressed only 6 of 10 keys,
    // 3 of 4 encoders, and ignored the touch strip entirely. Now routed to the
    // AKP05 backend with full 10-key / 4-encoder / touch-strip geometry.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = akp05::VendorId, // 0x0300
            .productId = Akp05ePid,      // 0x3004
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP05E (Stream Dock Plus)",
            .codename = "akp05e",
            .keyCount = akp05::KeyCount,
            .gridColumns = akp05::KeyCols,
            .encoderCount = akp05::EncoderCount,
            .hasTouchStrip = true,
            .hasClock = false,      // DEVICES-11 / ARCH-05: Stream Dock family has no firmware RTC.
            .keyRows = 2,           // 2x5 grid; akp05::KeyRows = 2 (REQ-26-C)
            .touchZoneCount = 4,    // 4 zones aligned to encoders; akp05::TouchZoneCount
            .mainScreenWidthPx = 0, // strip is 4 discrete zones, not one wide rect
            .mainScreenHeightPx = 0,
        },
        &makeAkp05);

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
    // AKP03 family (6-key + 3-encoder, same wire protocol as `makeAkp03`):
    //   AKP03V25, AKP03EV25, AKP03RV25, SD12N3V25, TS16N3V25, VSDN3,
    //   MBox-N3V25, MBox-N3EV25, MBox-N3 EV25, MSD-TWOV25, OMNIDIALV25
    //
    // AKP05 family (10-key + 4-encoder + strip, same wire as `makeAkp05`):
    //   AKP05V25, AKP05EV25, AKP05RV25, MBox-N4Pro, MBox-N4ProE, MBox-N6,
    //   N4Pro, N4ProE, N4V25, MSDPRO, MSDNEO, SD14N4V25, TS10N4V25, VSDN4,
    //   VSDN4Pro, BRHubN4, BRHubN4Pro
    //
    // AKP815 family (15-key 5x3 + 800×480 strip):
    //   TS183 (rebadge)
    // -------------------------------------------------------------------------
}

} // namespace ajazz::streamdeck
