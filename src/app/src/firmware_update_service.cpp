// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file firmware_update_service.cpp
 * @brief Implementation of @ref ajazz::app::FirmwareUpdateService.
 *
 * Deep-link only: no flashing, no firmware download, no libusb. See the
 * header doc and docs/architecture/FIRMWARE-UPDATES.md.
 */
#include "firmware_update_service.hpp"

#include "ajazz/core/device.hpp"
#include "ajazz/core/logger.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QQmlEngine>

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace ajazz::app {

namespace {

/// Pointer set by registerInstance(), consumed by ::create.
FirmwareUpdateService* s_instance = nullptr;

} // namespace

FirmwareUpdateService::FirmwareUpdateService(QObject* parent) : QObject(parent) {}

FirmwareUpdateService* FirmwareUpdateService::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_instance != nullptr,
               "FirmwareUpdateService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void FirmwareUpdateService::registerInstance(FirmwareUpdateService* instance) noexcept {
    s_instance = instance;
}

QUrl FirmwareUpdateService::firmwareDownloadUrl(Family family) {
    // Pinned from FIRMWARE-UPDATES.md §Per-family vendor download URLs. If any
    // of these die, update the doc + this map together.
    switch (family) {
    case StreamDock:
        // The vendor bundles firmware inside the installer; the support page
        // is the stable entry point.
        return QUrl(QStringLiteral("https://stream-dock.com/pages/support"));
    case Keyboard:
        return QUrl(QStringLiteral("https://ajazzstore.com/blogs/firmware"));
    case MouseAj159:
        return QUrl(
            QStringLiteral("https://epomaker.com/blogs/software/ajazz-aj159-pro-driver-1"));
    case MouseAj199:
        return QUrl(QStringLiteral("https://epomaker.com/blogs/software/ajazz-aj199-driver"));
    case Unknown:
        break;
    }
    // Generic fallback: the AJAZZ firmware blog lists every model.
    return QUrl(QStringLiteral("https://ajazzstore.com/blogs/firmware"));
}

FirmwareUpdateService::Family FirmwareUpdateService::familyForDevice(int coreDeviceFamily,
                                                                     QString const& codename) {
    switch (static_cast<core::DeviceFamily>(coreDeviceFamily)) {
    case core::DeviceFamily::StreamDeck:
        return StreamDock;
    case core::DeviceFamily::Keyboard:
        return Keyboard;
    case core::DeviceFamily::Mouse:
        // AJ199 / AJ199 Max (0x3554) is a separate wire dialect AND a separate
        // download page; everything else on the mouse side is the AJ159
        // (0x3151) dialect we actually speak.
        return codename.startsWith(QStringLiteral("aj199"), Qt::CaseInsensitive) ? MouseAj199
                                                                                 : MouseAj159;
    case core::DeviceFamily::Unknown:
        break;
    }
    return Unknown;
}

QStringList FirmwareUpdateService::vendorToolCandidatePaths(Family family) {
    // Only the Stream Dock tool has documented, per-OS install paths
    // (akp_dfu_protocol.md §9.3). Keyboard/mouse vendor tools ship inside
    // driver bundles whose executable names we have not pinned, so they
    // resolve to the download page instead of a guessed path.
    if (family != StreamDock) {
        return {};
    }
#if defined(Q_OS_WIN)
    QStringList out;
    for (auto const* base :
         {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        if (auto const dir = qEnvironmentVariable(base); !dir.isEmpty()) {
            out << dir + QStringLiteral("\\Stream Dock AJAZZ\\FirmwareUpgradeTool.exe");
        }
    }
    return out;
#elif defined(Q_OS_MACOS)
    return {QStringLiteral(
        "/Applications/Stream Dock AJAZZ.app/Contents/Resources/FirmwareUpgradeTool.app")};
#elif defined(Q_OS_LINUX)
    return {QStringLiteral("/opt/Stream Dock AJAZZ/FirmwareUpgradeTool")};
#else
    return {};
#endif
}

QString FirmwareUpdateService::registryVendorToolPath(Family family) {
#if defined(Q_OS_WIN)
    if (family != StreamDock) {
        return {};
    }
    // The vendor records its install root under HKLM; the firmware tool sits
    // alongside the main app there (akp_dfu_protocol.md §9.3).
    QSettings reg(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\HotSpot\\StreamDock"),
        QSettings::NativeFormat);
    QString const root = reg.value(QStringLiteral("InstallPath")).toString();
    if (root.isEmpty()) {
        return {};
    }
    return root + QStringLiteral("\\FirmwareUpgradeTool.exe");
#else
    Q_UNUSED(family)
    return {};
#endif
}

QString FirmwareUpdateService::detectedVendorToolPath(Family family) const {
    // Registry-derived path (Windows) is most authoritative; try it first.
    if (auto const fromReg = registryVendorToolPath(family);
        !fromReg.isEmpty() && QFileInfo::exists(fromReg)) {
        return fromReg;
    }
    for (auto const& candidate : vendorToolCandidatePaths(family)) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool FirmwareUpdateService::isVendorToolInstalled(Family family) const {
    return !detectedVendorToolPath(family).isEmpty();
}

bool FirmwareUpdateService::openFirmwareDownloadPage(Family family) {
    QUrl const url = firmwareDownloadUrl(family);
    bool const ok = QDesktopServices::openUrl(url);
    if (!ok) {
        AJAZZ_LOG_WARN("firmware", "failed to open download page {}", url.toString().toStdString());
    }
    return ok;
}

bool FirmwareUpdateService::launchVendorTool(Family family) {
    QString const path = detectedVendorToolPath(family);
    if (path.isEmpty()) {
        AJAZZ_LOG_INFO("firmware",
                       "no vendor firmware tool installed for family {}; caller should fall back "
                       "to the download page",
                       static_cast<int>(family));
        return false;
    }

    // Let the app drop its HID handle before the vendor flasher claims the
    // device (FIRMWARE-UPDATES.md §Launch vendor app).
    Q_EMIT aboutToLaunchVendorTool(family);

    bool started = false;
#if defined(Q_OS_MACOS)
    // The tool is a .app bundle; launch it via `open` rather than exec'ing the
    // directory.
    started = QProcess::startDetached(QStringLiteral("open"), {path});
#else
    started = QProcess::startDetached(path, {});
#endif
    if (!started) {
        AJAZZ_LOG_WARN("firmware", "failed to launch vendor tool {}", path.toStdString());
    } else {
        AJAZZ_LOG_INFO("firmware", "launched vendor firmware tool {}", path.toStdString());
    }
    return started;
}

} // namespace ajazz::app
