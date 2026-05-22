// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file firmware_update_service.hpp
 * @brief QML-facing "update firmware via the vendor tool" deep-link service.
 *
 * Implements the **DECIDED** firmware posture from
 * @c docs/architecture/FIRMWARE-UPDATES.md: we do **not** ship firmware blobs
 * and we do **not** perform the flash. This service exposes the read-only,
 * legally-safe surface the decision calls for:
 *
 *   * a per-family vendor firmware **download-page** deep-link, and
 *   * detection + launch of the vendor's installed **firmware-update tool**
 *     (today only the Stream Dock @c FirmwareUpgradeTool has documented,
 *     per-OS install paths — see @c docs/protocols/streamdeck/akp_dfu_protocol.md
 *     §9.3; keyboard / mouse fall back to the download page).
 *
 * Before spawning the vendor tool we emit @ref aboutToLaunchVendorTool so the
 * Application can close our HID handle first — otherwise the vendor flasher
 * fights us for the device (FIRMWARE-UPDATES.md §Launch vendor app).
 *
 * Out of scope, on purpose: no in-app flashing, no firmware download/host/
 * proxy, no libusb (COD-031), no `aKDFU` decrypt. See FIRMWARE-UPDATES.md
 * §Why not bundle + FIRMWARE-UPDATES-IMPLEMENTATION.md.
 *
 * Pattern follows @c AutostartService / @c AppUpdateService:
 *   * `QML_NAMED_ELEMENT(FirmwareUpdate)` + `QML_SINGLETON`
 *   * QML factory `create()` returns the @ref Application-owned instance
 *     registered via @ref registerInstance() (Pitfall 4 build-break lock).
 *   * Non-default-constructible so QML cannot bypass the factory.
 *   * Pure URL / candidate-path logic exposed `static` for unit testing.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>
#include <QUrl>

#include <functional>
#include <memory>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

namespace ajazz::core {
class IDevice;
}

namespace ajazz::app {

/**
 * @class FirmwareUpdateService
 * @brief QML singleton that deep-links to the vendor firmware-update flow.
 *
 * Exposed as `FirmwareUpdate` in QML. Stateless apart from the QML-singleton
 * plumbing; every query takes a @ref Family so a single instance serves all
 * device rows.
 */
class FirmwareUpdateService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(FirmwareUpdate)
    QML_SINGLETON

public:
    /**
     * @brief Device family a firmware action targets.
     *
     * Q_ENUM so QML can pass `FirmwareUpdate.StreamDock` etc. The split
     * mirrors the per-family rows in FIRMWARE-UPDATES.md: the AJ159 and
     * AJ199 mice are deliberately distinct because they are different wire
     * dialects AND have different vendor download pages
     * (FIRMWARE-UPDATES-IMPLEMENTATION.md §3.2).
     */
    enum Family {
        Unknown = 0, ///< No family resolved; actions fall back to the generic page.
        StreamDock,  ///< AKP03 / AKP05 / AKP153 / AKP815 / Mirabox.
        Keyboard,    ///< AK980 PRO / AK820 Pro and siblings.
        MouseAj159,  ///< AJ159 PRO and the 0x3151 dialect siblings.
        MouseAj199,  ///< AJ199 / AJ199 Max (0x3554 dialect).
    };
    Q_ENUM(Family)

    /// Resolves a device codename to its (possibly null) live backend, so the
    /// service can read the running firmware version. Mirrors the
    /// BatteryService / TimeSyncService DeviceLookup seam. Defaulted empty:
    /// when unset (e.g. unit tests), runningFirmwareVersion() returns "".
    using DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>;

    explicit FirmwareUpdateService(QObject* parent, DeviceLookup lookup = {});
    ~FirmwareUpdateService() override = default;

    static FirmwareUpdateService* create(QQmlEngine* qml, QJSEngine* js);
    static void registerInstance(FirmwareUpdateService* instance) noexcept;

    /**
     * @brief The official vendor firmware download page for @p family.
     *
     * Pure mapping, exposed for testing. URLs are pinned from
     * FIRMWARE-UPDATES.md §Per-family vendor download URLs; @ref Unknown and
     * any unmapped value return the generic AJAZZ firmware blog.
     */
    [[nodiscard]] static QUrl firmwareDownloadUrl(Family family);

    /**
     * @brief Map the coarse core @c DeviceFamily + codename to a granular
     *        @ref Family.
     *
     * Exposed @c Q_INVOKABLE so the QML device row can turn its @c family
     * role (a @c ajazz::core::DeviceFamily integer) + @c codename into the
     * argument the launch / URL methods expect. The mouse split (AJ159 vs
     * AJ199) is decided by codename prefix — `aj199*` is the distinct
     * dialect / download page (FIRMWARE-UPDATES-IMPLEMENTATION.md §3.2);
     * every other mouse maps to @ref MouseAj159. Pure, exposed for testing.
     *
     * @param coreDeviceFamily @c static_cast<int> of @c ajazz::core::DeviceFamily.
     * @param codename         Backend codename (e.g. "akp05", "aj159pro").
     */
    [[nodiscard]] Q_INVOKABLE static Family familyForDevice(int coreDeviceFamily,
                                                            QString const& codename);

    /**
     * @brief Candidate absolute paths to the vendor firmware-update tool for
     *        @p family on the current OS, most-specific first.
     *
     * Pure (no filesystem touch), exposed for testing. Only @ref StreamDock
     * has documented per-OS paths today (akp_dfu_protocol.md §9.3); other
     * families return an empty list (callers then fall back to the download
     * page). The Windows list intentionally omits the registry-derived path —
     * that one is resolved at runtime in @ref detectedVendorToolPath because
     * it requires a registry read.
     */
    [[nodiscard]] static QStringList vendorToolCandidatePaths(Family family);

    /**
     * @brief First existing vendor-tool path for @p family, or empty.
     *
     * Reads the registry (Windows) and probes @ref vendorToolCandidatePaths
     * on disk. Empty string means "tool not installed — use the download page".
     */
    [[nodiscard]] Q_INVOKABLE QString detectedVendorToolPath(Family family) const;

    /// @return true iff @ref detectedVendorToolPath is non-empty.
    [[nodiscard]] Q_INVOKABLE bool isVendorToolInstalled(Family family) const;

    /**
     * @brief The device's currently-running firmware version string.
     *
     * Resolves @p codename via the injected @ref DeviceLookup and returns
     * `IDevice::firmwareVersion()` (e.g. the AKP05E's `CRT VER` "V3.AKP05E.01.007",
     * or the AK980's `major.minor.patch`). Returns an empty string when no
     * lookup is wired or the device is unavailable. May perform a short HID
     * round-trip — call it on demand (e.g. when the Firmware tab opens), not in
     * a binding.
     */
    [[nodiscard]] Q_INVOKABLE QString runningFirmwareVersion(QString const& codename) const;

public Q_SLOTS:
    /**
     * @brief Open the vendor firmware download page in the user's browser.
     * @return true if the URL was handed to the OS successfully.
     */
    bool openFirmwareDownloadPage(Family family);

    /**
     * @brief Launch the installed vendor firmware-update tool for @p family.
     *
     * Emits @ref aboutToLaunchVendorTool first (so the app can release its HID
     * handle), then spawns the detected tool detached. Returns false — and
     * does NOT emit the signal — if no vendor tool is installed; the caller
     * should then call @ref openFirmwareDownloadPage.
     */
    bool launchVendorTool(Family family);

Q_SIGNALS:
    /**
     * @brief Emitted immediately before the vendor tool is spawned.
     *
     * Application connects this to close any open HID handle for the device so
     * the vendor flasher can claim the USB interface uncontested
     * (FIRMWARE-UPDATES.md §Launch vendor app).
     */
    void aboutToLaunchVendorTool(Family family);

private:
    /// Windows-only: read HKLM\SOFTWARE\HotSpot\StreamDock\InstallPath and
    /// join the FirmwareUpgradeTool executable, or return empty. No-op
    /// (returns empty) on non-Windows / non-StreamDock.
    [[nodiscard]] static QString registryVendorToolPath(Family family);

    DeviceLookup m_lookup; ///< codename -> live backend; empty in tests.
};

// Pitfall 4 build-break lock — co-located with QML_SINGLETON.
static_assert(!std::is_default_constructible_v<FirmwareUpdateService>,
              "FirmwareUpdateService must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
