// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file time_sync_service.hpp
 * @brief Application-layer service that pushes the host clock to devices.
 *
 * Owns no devices — locates them on demand via a caller-supplied
 * @c DeviceLookup function (production wires this to
 * @c DeviceRegistry::open + @c IClockCapable dynamic_cast; unit tests
 * inject hand-crafted mocks). Two entry points:
 *
 *   * @c setSystemTimeOn(codename) — manual one-shot push, invoked by the
 *     QML "Sync now" button.
 *   * @c onDeviceArrived(codename) — auto-sync hook called by the
 *     application (Plan 05-07) when @c HotplugMonitor reports a fresh
 *     arrival; pushes the time iff the autoSync setting is on.
 *
 * The DeviceLookup decoupling is the seam Plan 05-04's unit tests target
 * (no need to fake a full DeviceRegistry). In production (Plan 05-07) the
 * lookup is a lambda that captures @c m_registry, calls
 * @c registry.open(deviceId), and (per A-04 / D-01 amendment 3) holds the
 * @c std::shared_ptr<IDevice> in a local for the lifetime of the call —
 * closing the UAF window across the dynamic_cast → setTime sequence.
 *
 * Persistent: @c autoSync flag is mirrored into
 * @c QSettings("Time/AutoSync") at construction and on every setter call.
 * Pitfall 13 load-time validation: if the persisted flag is @c true but no
 * currently-connected device advertises @c Capability::Clock, an INFO log
 * surfaces and the flag is left as-is (never silent-disabled, never
 * silent-fired).
 *
 * @note Thread-affine: lives on the Qt main thread. Hotplug callbacks
 *       must dispatch onto the main thread before invoking
 *       @c onDeviceArrived (Application is responsible for this).
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <chrono>
#include <functional>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

namespace ajazz::core {
class IDevice;
} // namespace ajazz::core

namespace ajazz::app {

/**
 * @class TimeSyncService
 * @brief Pushes the host clock to AJAZZ devices that implement
 *        @ref ajazz::core::IClockCapable.
 *
 * Exposed to QML as a singleton via @c qmlRegisterSingletonInstance —
 * see the matching pattern + static_assert guard at the bottom of this
 * file (the same Pitfall 4 build-break lock used by @c BrandingService).
 */
class TimeSyncService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(TimeSyncService)
    QML_SINGLETON

    /// Whether new device arrivals trigger an automatic time push.
    /// Mirrored to @c QSettings("Time/AutoSync") on every change.
    Q_PROPERTY(bool autoSync READ autoSync WRITE setAutoSync NOTIFY autoSyncChanged)

public:
    /**
     * @brief Caller-supplied lookup: codename → currently-open device, or null.
     *
     * The contract: the returned pointer must outlive the call to
     * @c setSystemTimeOn / @c onDeviceArrived (the service does not own
     * it). In production (Plan 05-07), the lookup lambda captures a
     * @c std::shared_ptr<IDevice> in a local (A-04 / D-01 amendment 3)
     * before returning the raw pointer — that lambda's stack frame keeps
     * the @c shared_ptr alive across the dynamic_cast + setTime, closing
     * the UAF window from Phase 4 D-06.
     *
     * In unit tests (Plan 05-04), the mock device is held by the test
     * fixture's @c std::unique_ptr / @c std::shared_ptr for the entire
     * test case.
     */
    using DeviceLookup = std::function<core::IDevice*(QString const&)>;

    // No default on `parent`: see BrandingService — a default-constructible
    // QML_SINGLETON makes Qt 6 pick `Constructor` mode and silently bypass
    // the static `create()` factory, spawning a duplicate QML-side instance
    // (Pitfall 4). The static_assert below pins this at the build site.
    explicit TimeSyncService(DeviceLookup lookup, QObject* parent);
    ~TimeSyncService() override;

    /// QML singleton factory — see BrandingService::create for the pattern.
    static TimeSyncService* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(TimeSyncService* instance) noexcept;

    /// @return current value of the autoSync flag.
    [[nodiscard]] bool autoSync() const noexcept;

    /// Set the autoSync flag and persist it to @c QSettings.
    void setAutoSync(bool enabled);

    /**
     * @brief Push the current host time to the device with this codename.
     *
     * Invoked from QML by the per-device "Sync now" button. Locates the
     * device via the constructor-supplied lookup, dynamic-casts to
     * @c IClockCapable (Pitfall 2: null-check within 3 lines), calls
     * @c setTime(now), surfaces the outcome via @c syncSucceeded /
     * @c syncFailed signals.
     *
     * @param codename Device codename (matches @c DeviceDescriptor::codename).
     */
    Q_INVOKABLE void setSystemTimeOn(QString const& codename);

    /**
     * @brief Auto-sync hook — called by the application on each
     *        @c HotplugMonitor arrival event (Plan 05-07 wires the signal).
     *
     * Synchronous. No-op unless @c autoSync is true and the device
     * implements @c IClockCapable. Auto-sync failures surface only via
     * INFO log + the per-row glyph (D-02: auto-sync is glyph-only,
     * never a toast).
     *
     * For the 300 ms QTimer::singleShot-debounced variant invoked by
     * production (which absorbs the composite-USB double-arrival per
     * Phase 4 D-05), see @ref onDeviceArrivedDebounced. Unit tests call
     * @ref onDeviceArrived directly to avoid the event-loop dependency.
     */
    void onDeviceArrived(QString const& codename);

    /**
     * @brief 300 ms-debounced variant of @ref onDeviceArrived.
     *
     * Plan 05-07's Application wires @c HotplugMonitor::deviceArrived
     * here. The 300 ms debounce (A-04 / D-01 amendment 3) absorbs the
     * composite-USB double-arrival window noted in Phase 4 D-05.
     * Captures the codename by value into the lambda; re-validates
     * capability and connectedness at firing time (Pitfall 2).
     */
    void onDeviceArrivedDebounced(QString const& codename);

signals:
    /// Emitted whenever @c autoSync changes value.
    void autoSyncChanged(bool enabled);

    /// Emitted on a successful @c setSystemTimeOn / @c onDeviceArrived push.
    void syncSucceeded(QString const& codename);

    /// Emitted on a failed manual push. @p message is human-readable and
    /// safe to surface in a toast / tooltip. Per D-02, auto-sync failures
    /// do NOT emit this signal — they only log at INFO and update the
    /// per-row glyph.
    void syncFailed(QString const& codename, QString const& message);

private:
    /// Internal: do the dynamic-cast + setTime call. Returns the textual
    /// reason on failure, or an empty string on Ok.
    QString doPush(QString const& codename);

    /// Pitfall 13 (load-time validation): logged once from the ctor if the
    /// persisted autoSync flag is `true` but no currently-connected device
    /// advertises Capability::Clock. The flag is left as-is — never
    /// silent-disabled, never silent-fired.
    void validatePersistedAutoSync();

    DeviceLookup m_lookup;
    bool m_autoSync{false};
};

// Pitfall 4 build-break lock — see BrandingService's matching static_assert.
// QML_SINGLETON + default-constructible class → Qt 6 picks Constructor mode,
// bypasses our static create() factory, spawns a duplicate QML-side instance.
// The non-defaulted ctor + this assertion together force every caller to
// pass a DeviceLookup AND a parent (QML routes through create() instead).
static_assert(
    !std::is_default_constructible_v<TimeSyncService>,
    "TimeSyncService must not be default-constructible — see ctor @note and BrandingService.");

} // namespace ajazz::app
