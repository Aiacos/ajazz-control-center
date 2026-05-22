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

FirmwareUpdateService::FirmwareUpdateService(QObject* parent, DeviceLookup lookup)
    : QObject(parent), m_lookup(std::move(lookup)) {}

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
        return QUrl(QStringLiteral("https://epomaker.com/blogs/software/ajazz-aj159-pro-driver-1"));
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
    // We launch the vendor's MAIN app (it auto-checks, downloads, decrypts and
    // flashes), NOT the bare FirmwareUpgradeTool — launching the tool directly
    // would make the user pick a firmware file by hand, which is exactly the
    // manual step the maintainer wants gone. Only the Stream Dock app has a
    // guessable, region-stable-ish install dir; the keyboard / mouse driver dirs
    // are model-named (often CJK, e.g. "AJAZZ AK980 ... Keyboard"), so those
    // resolve via the Uninstall registry (registryVendorToolPath) only.
    if (family != StreamDock) {
        return {};
    }
#if defined(Q_OS_WIN)
    QStringList out;
    for (auto const* base : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        auto const dir = qEnvironmentVariable(base);
        if (dir.isEmpty()) {
            continue;
        }
        // The retail installer dir is "Stream Dock AJAZZ Global"; older/region
        // builds drop the "Global" suffix. Try both.
        for (auto const* sub : {"Stream Dock AJAZZ Global", "Stream Dock AJAZZ"}) {
            out << dir + QLatin1Char('\\') + QString::fromLatin1(sub) +
                       QStringLiteral("\\Stream Dock AJAZZ.exe");
        }
    }
    return out;
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("/Applications/Stream Dock AJAZZ.app")};
#else
    // No vendor app exists on Linux (Stream Dock app is Windows/macOS only).
    return {};
#endif
}

QString FirmwareUpdateService::vendorAppExeName(Family family) {
    switch (family) {
    case StreamDock:
        return QStringLiteral("Stream Dock AJAZZ.exe");
    case Keyboard:
    case MouseAj159:
    case MouseAj199:
        return QStringLiteral("DeviceDriver.exe");
    case Unknown:
        break;
    }
    return {};
}

QString FirmwareUpdateService::registryVendorToolPath(Family family) {
#if defined(Q_OS_WIN)
    QString const exeName = vendorAppExeName(family);
    if (exeName.isEmpty()) {
        return {};
    }
    // Match the family's vendor app by its uninstall DisplayName. "Stream Dock
    // AJAZZ" covers the Stream Dock app; the keyboard/mouse driver bundles read
    // "AJAZZ <model> Keyboard" / "... Mouse".
    auto const matches = [family](QString const& name) {
        QString const n = name.toLower();
        switch (family) {
        case StreamDock:
            return n.contains(QStringLiteral("stream dock"));
        case Keyboard:
            return n.contains(QStringLiteral("ajazz")) && n.contains(QStringLiteral("keyboard"));
        case MouseAj159:
        case MouseAj199:
            return n.contains(QStringLiteral("ajazz")) && n.contains(QStringLiteral("mouse"));
        case Unknown:
            break;
        }
        return false;
    };
    // Scan both the native and WOW6432 uninstall hives for an InstallLocation.
    for (auto const* root :
         {"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
          "HKEY_LOCAL_"
          "MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"}) {
        QSettings reg(QString::fromLatin1(root), QSettings::NativeFormat);
        for (auto const& sub : reg.childGroups()) {
            reg.beginGroup(sub);
            QString const name = reg.value(QStringLiteral("DisplayName")).toString();
            QString loc = reg.value(QStringLiteral("InstallLocation")).toString();
            reg.endGroup();
            if (loc.isEmpty() || !matches(name)) {
                continue;
            }
            if (loc.endsWith(QLatin1Char('\\')) || loc.endsWith(QLatin1Char('/'))) {
                loc.chop(1);
            }
            QString const exe = loc + QLatin1Char('\\') + exeName;
            if (QFileInfo::exists(exe)) {
                return exe;
            }
        }
    }
    return {};
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

QString FirmwareUpdateService::runningFirmwareVersion(QString const& codename) const {
    if (!m_lookup) {
        return {};
    }
    auto const device = m_lookup(codename);
    if (!device) {
        return {};
    }
    try {
        return QString::fromStdString(device->firmwareVersion());
    } catch (...) {
        return {};
    }
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
                       "no vendor updater app installed for family {}; caller should fall back "
                       "to the download/install page",
                       static_cast<int>(family));
        return false;
    }

    // We launch the vendor's MAIN app, which performs the firmware update
    // automatically (version check -> download -> decrypt -> flash) — the user
    // does not pick a firmware file by hand. Drop our HID handle first so the
    // vendor flasher can claim the device (FIRMWARE-UPDATES.md §Launch vendor app).
    Q_EMIT aboutToLaunchVendorTool(family);

    bool started = false;
#if defined(Q_OS_MACOS)
    // macOS ships a .app bundle; launch it via `open` rather than exec'ing the
    // directory.
    started = QProcess::startDetached(QStringLiteral("open"), {path});
#else
    started = QProcess::startDetached(path, {});
#endif
    if (!started) {
        AJAZZ_LOG_WARN("firmware", "failed to launch vendor updater {}", path.toStdString());
    } else {
        AJAZZ_LOG_INFO("firmware", "launched vendor updater app {}", path.toStdString());
    }
    return started;
}

} // namespace ajazz::app
