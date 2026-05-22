// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_firmware_update_service.cpp
 * @brief Pure-logic coverage for FirmwareUpdateService.
 *
 * We pin the two stateless surfaces that don't need a device or a running
 * vendor install:
 *
 *   * firmwareDownloadUrl() -- the per-family URL map matches the pinned
 *     entries in docs/architecture/FIRMWARE-UPDATES.md, and Unknown falls
 *     back to the generic AJAZZ firmware blog;
 *   * vendorToolCandidatePaths() -- only the Stream Dock family yields
 *     per-OS candidate paths (akp_dfu_protocol.md §9.3); keyboard / mouse
 *     return an empty list so callers fall back to the download page;
 *   * detectedVendorToolPath() / isVendorToolInstalled() -- return
 *     empty / false on a machine without the vendor tool installed (the
 *     CI runners), proving the "not installed -> use the page" contract.
 *
 * The actual QProcess launch + QDesktopServices open are NOT exercised here
 * (they touch the host OS); they are thin wrappers over the logic pinned
 * above. TEST_CASE names are ASCII-only per the Windows ctest filter rule.
 */
#include "firmware_update_service.hpp"

#include <QString>
#include <QUrl>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::FirmwareUpdateService;

TEST_CASE("FirmwareUpdateService download URLs match the pinned per-family map", "[firmware]") {
    REQUIRE(FirmwareUpdateService::firmwareDownloadUrl(FirmwareUpdateService::StreamDock)
                .host() == QStringLiteral("stream-dock.com"));
    REQUIRE(FirmwareUpdateService::firmwareDownloadUrl(FirmwareUpdateService::Keyboard)
                .host() == QStringLiteral("ajazzstore.com"));

    auto const aj159 = FirmwareUpdateService::firmwareDownloadUrl(FirmwareUpdateService::MouseAj159);
    REQUIRE(aj159.host() == QStringLiteral("epomaker.com"));
    REQUIRE(aj159.path().contains(QStringLiteral("aj159")));

    auto const aj199 = FirmwareUpdateService::firmwareDownloadUrl(FirmwareUpdateService::MouseAj199);
    REQUIRE(aj199.host() == QStringLiteral("epomaker.com"));
    REQUIRE(aj199.path().contains(QStringLiteral("aj199")));
}

TEST_CASE("FirmwareUpdateService Unknown family falls back to the generic blog", "[firmware]") {
    auto const url = FirmwareUpdateService::firmwareDownloadUrl(FirmwareUpdateService::Unknown);
    REQUIRE(url.host() == QStringLiteral("ajazzstore.com"));
    REQUIRE(url.isValid());
}

TEST_CASE("FirmwareUpdateService every family yields a valid https URL", "[firmware]") {
    for (auto const family : {FirmwareUpdateService::StreamDock,
                              FirmwareUpdateService::Keyboard,
                              FirmwareUpdateService::MouseAj159,
                              FirmwareUpdateService::MouseAj199,
                              FirmwareUpdateService::Unknown}) {
        auto const url = FirmwareUpdateService::firmwareDownloadUrl(family);
        REQUIRE(url.isValid());
        REQUIRE(url.scheme() == QStringLiteral("https"));
    }
}

TEST_CASE("FirmwareUpdateService only Stream Dock has documented vendor-tool paths", "[firmware]") {
    // Keyboard / mouse vendor tools ship inside driver bundles whose exe
    // names we have not pinned -> empty list (download-page fallback).
    REQUIRE(FirmwareUpdateService::vendorToolCandidatePaths(FirmwareUpdateService::Keyboard)
                .isEmpty());
    REQUIRE(FirmwareUpdateService::vendorToolCandidatePaths(FirmwareUpdateService::MouseAj159)
                .isEmpty());
    REQUIRE(FirmwareUpdateService::vendorToolCandidatePaths(FirmwareUpdateService::MouseAj199)
                .isEmpty());
    REQUIRE(FirmwareUpdateService::vendorToolCandidatePaths(FirmwareUpdateService::Unknown)
                .isEmpty());

    auto const sd = FirmwareUpdateService::vendorToolCandidatePaths(FirmwareUpdateService::StreamDock);
#if defined(Q_OS_WIN)
    REQUIRE_FALSE(sd.isEmpty());
    for (auto const& p : sd) {
        REQUIRE(p.endsWith(QStringLiteral("FirmwareUpgradeTool.exe")));
        REQUIRE(p.contains(QStringLiteral("Stream Dock AJAZZ")));
    }
#elif defined(Q_OS_MACOS)
    REQUIRE(sd.size() == 1);
    REQUIRE(sd.first().contains(QStringLiteral(".app")));
    REQUIRE(sd.first().contains(QStringLiteral("Stream Dock AJAZZ")));
#elif defined(Q_OS_LINUX)
    REQUIRE(sd.size() == 1);
    REQUIRE(sd.first() == QStringLiteral("/opt/Stream Dock AJAZZ/FirmwareUpgradeTool"));
#endif
}

TEST_CASE("FirmwareUpdateService familyForDevice maps the coarse core family", "[firmware]") {
    using F = FirmwareUpdateService;
    // core::DeviceFamily: Unknown=0, StreamDeck=1, Keyboard=2, Mouse=3.
    REQUIRE(F::familyForDevice(1, QStringLiteral("akp05")) == F::StreamDock);
    REQUIRE(F::familyForDevice(2, QStringLiteral("ak980pro")) == F::Keyboard);
    REQUIRE(F::familyForDevice(0, QStringLiteral("whatever")) == F::Unknown);
}

TEST_CASE("FirmwareUpdateService familyForDevice splits the mouse dialects by codename",
          "[firmware]") {
    using F = FirmwareUpdateService;
    // AJ199 (0x3554) is its own dialect + download page; everything else on
    // the mouse side is the AJ159 (0x3151) dialect we speak.
    REQUIRE(F::familyForDevice(3, QStringLiteral("aj159pro")) == F::MouseAj159);
    REQUIRE(F::familyForDevice(3, QStringLiteral("aj179")) == F::MouseAj159);
    REQUIRE(F::familyForDevice(3, QStringLiteral("aj199")) == F::MouseAj199);
    REQUIRE(F::familyForDevice(3, QStringLiteral("aj199max")) == F::MouseAj199);
    REQUIRE(F::familyForDevice(3, QStringLiteral("AJ199")) == F::MouseAj199); // case-insensitive
}

TEST_CASE("FirmwareUpdateService reports tool not installed on a bare machine", "[firmware]") {
    // CI runners + dev boxes without the vendor app installed: detection
    // must return empty / false rather than a phantom path.
    FirmwareUpdateService const svc(nullptr);
    auto const path = svc.detectedVendorToolPath(FirmwareUpdateService::StreamDock);
    if (path.isEmpty()) {
        REQUIRE_FALSE(svc.isVendorToolInstalled(FirmwareUpdateService::StreamDock));
    } else {
        // If a developer happens to have the tool installed, the contract is
        // simply that the reported path exists on disk.
        REQUIRE(svc.isVendorToolInstalled(FirmwareUpdateService::StreamDock));
    }

    // Families with no documented tool are never "installed".
    REQUIRE_FALSE(svc.isVendorToolInstalled(FirmwareUpdateService::Keyboard));
    REQUIRE_FALSE(svc.isVendorToolInstalled(FirmwareUpdateService::MouseAj159));
}
