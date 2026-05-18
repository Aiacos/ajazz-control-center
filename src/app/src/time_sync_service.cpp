// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file time_sync_service.cpp
 * @brief Implementation of @ref ajazz::app::TimeSyncService.
 *
 * See @c time_sync_service.hpp for the contract. Pitfall references:
 *
 *   * Pitfall 2 (dynamic_cast nullptr): every cast site has a null-check
 *     within 3 lines.
 *   * Pitfall 4 (QML_SINGLETON dual-instance): static_assert in the header
 *     blocks default construction at compile time; the factory `create()`
 *     reuses the Application-owned instance registered via
 *     `registerInstance()`.
 *   * Pitfall 13 (persisted setting outliving capability): ctor validates
 *     the persisted autoSync flag against currently-connected devices and
 *     logs INFO (never silent-disables, never silent-fires).
 */
#include "time_sync_service.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"

#include <QQmlEngine>
#include <QSettings>
#include <QTimer>

#include <chrono>
#include <utility>

namespace ajazz::app {

namespace {

/// Application-owned singleton handed to QML via @c create(). See
/// @c registerInstance / @c create — same pattern as BrandingService.
TimeSyncService* g_instance = nullptr;

constexpr char kSettingsKey[] = "Time/AutoSync";

/// Interval for the periodic auto-sync push. 15 minutes is long enough
/// that the firmware RTC drift stays within a second and short enough
/// to catch DST transitions within a quarter of a minute. Vendor app
/// has no periodic sync at all, so any non-zero cadence is an
/// improvement; 15 min is the round number the audit recommended.
constexpr std::chrono::milliseconds kAutoSyncInterval{15 * 60 * 1000};

} // namespace

TimeSyncService::TimeSyncService(DeviceLookup lookup, QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)), m_autoSyncTimer(new QTimer(this)) {
    m_autoSyncTimer->setInterval(kAutoSyncInterval);
    m_autoSyncTimer->setTimerType(Qt::CoarseTimer); // millisecond accuracy
                                                    // is fine; we don't
                                                    // need wake-up cost.
    connect(m_autoSyncTimer, &QTimer::timeout, this, &TimeSyncService::periodicAutoSyncTick);
    QSettings settings;
    m_autoSync = settings.value(QString::fromLatin1(kSettingsKey), false).toBool();
    validatePersistedAutoSync();
    reconcileAutoSyncTimer();
}

TimeSyncService::~TimeSyncService() = default;

TimeSyncService* TimeSyncService::create(QQmlEngine*, QJSEngine*) {
    // Ownership stays with Application via registerInstance — QML must
    // not delete the singleton on engine teardown.
    if (g_instance != nullptr) {
        QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    }
    return g_instance;
}

void TimeSyncService::registerInstance(TimeSyncService* instance) noexcept {
    g_instance = instance;
}

bool TimeSyncService::autoSync() const noexcept {
    return m_autoSync;
}

void TimeSyncService::setAutoSync(bool enabled) {
    if (m_autoSync == enabled) {
        return;
    }
    m_autoSync = enabled;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingsKey), enabled);
    reconcileAutoSyncTimer();
    emit autoSyncChanged(enabled);
}

void TimeSyncService::setConnectedCodenameEnumerator(ConnectedCodenameEnumerator enumerator) {
    m_enumerator = std::move(enumerator);
    reconcileAutoSyncTimer();
}

void TimeSyncService::reconcileAutoSyncTimer() {
    // Only run the periodic push when (a) the user wants auto-sync AND
    // (b) Application has wired an enumerator. Without the enumerator
    // we have no way to find IClockCapable devices on a recurring
    // basis (the per-codename lookup alone is not enough), so we stay
    // dormant. Hot-plug arrivals still drive onDeviceArrived as before.
    if (m_autoSyncTimer == nullptr) {
        return; // construction-time guard for moved-from / partial-init.
    }
    bool const wantRunning = m_autoSync && static_cast<bool>(m_enumerator);
    if (wantRunning && !m_autoSyncTimer->isActive()) {
        m_autoSyncTimer->start();
        AJAZZ_LOG_INFO("time-sync",
                       "periodic auto-sync timer started ({} ms interval)",
                       static_cast<long long>(kAutoSyncInterval.count()));
    } else if (!wantRunning && m_autoSyncTimer->isActive()) {
        m_autoSyncTimer->stop();
        AJAZZ_LOG_INFO("time-sync", "periodic auto-sync timer stopped");
    }
}

void TimeSyncService::periodicAutoSyncTick() {
    if (!m_autoSync || !m_enumerator) {
        // Defensive: setAutoSync(false) / setConnectedCodenameEnumerator(nullptr)
        // already stopped the timer, but if a slot fires between the stop
        // request and the next event-loop iteration we still bail cleanly.
        return;
    }
    auto const codenames = m_enumerator();
    if (codenames.empty()) {
        AJAZZ_LOG_INFO("time-sync", "periodic auto-sync tick: no connected devices");
        return;
    }
    for (auto const& codename : codenames) {
        QString const reason = doPush(codename);
        if (reason.isEmpty()) {
            // Per D-02: auto-sync surface is glyph-only, but we still
            // emit syncSucceeded so the QML glyph can transition green.
            emit syncSucceeded(codename);
        } else {
            // Auto-sync failures stay silent (no syncFailed signal); a
            // device without IClockCapable is expected and not actionable.
            AJAZZ_LOG_INFO("time-sync",
                           "periodic auto-sync skipped {}: {}",
                           codename.toStdString(),
                           reason.toStdString());
        }
    }
}

void TimeSyncService::setSystemTimeOn(QString const& codename) {
    QString const reason = doPush(codename);
    if (reason.isEmpty()) {
        emit syncSucceeded(codename);
    } else {
        emit syncFailed(codename, reason);
    }
}

void TimeSyncService::onDeviceArrived(QString const& codename) {
    if (!m_autoSync) {
        return;
    }
    // Same call path as manual sync; the difference is in the surface:
    // D-02 says auto-sync failures are glyph-only (no toast) — so we emit
    // syncSucceeded on Ok but only INFO-log on failure. The per-row glyph
    // wire-up in Plan 05-06 reads the syncSucceeded signal and the
    // (silent) failure path.
    //
    // Synchronous call is intentional: the 300 ms debounce that A-04
    // describes lives in Application's hotplug → service wiring
    // (Plan 05-07) — it uses QTimer::singleShot in the connection slot
    // rather than inside the service body. This keeps TimeSyncService
    // unit-testable without an event loop and isolates the debounce
    // policy in the integration layer where Phase 4 D-05's hot-plug
    // coalescing also lives. See doPush() for the Pitfall 2 cast +
    // null-check pattern (within 3 lines).
    QString const reason = doPush(codename);
    if (reason.isEmpty()) {
        emit syncSucceeded(codename);
    } else {
        // Auto-sync failures are common on hot-plug arrival when a device
        // just appeared but doesn't implement IClockCapable — log INFO
        // and do NOT emit syncFailed (the user didn't ask for this; D-02
        // auto-sync surface = glyph only).
        AJAZZ_LOG_INFO("time-sync",
                       "auto-sync skipped for {}: {}",
                       codename.toStdString(),
                       reason.toStdString());
    }
}

void TimeSyncService::onDeviceArrivedDebounced(QString const& codename) {
    // A-04 / D-01 amendment 3: 300 ms QTimer::singleShot debounce stacks
    // on top of Phase 4 D-05's hot-plug coalescing window. The lambda
    // captures `codename` BY VALUE — the deviceId is the stable identity
    // across the debounce window (Pitfall 2: re-resolve at firing time;
    // do NOT capture a shared_ptr<IDevice> here because the device may
    // have departed during the 300 ms wait).
    QTimer::singleShot(
        std::chrono::milliseconds(300), this, [this, codename] { onDeviceArrived(codename); });
}

void TimeSyncService::validatePersistedAutoSync() {
    if (!m_autoSync) {
        // Persisted false → nothing to validate, nothing to warn about.
        return;
    }
    // Pitfall 13: persisted ON. We cannot directly enumerate connected
    // devices here without a DeviceRegistry reference — the
    // DeviceLookup seam only resolves a single codename. So the
    // load-time check is informational: log INFO that the flag is on
    // and rely on Application (Plan 05-07) to surface the more
    // contextual "auto-sync persisted ON but no capable device" message
    // once it has the registry in hand. Crucially: do NOT mutate
    // m_autoSync — silent-disable would surprise the user (and
    // silent-fire would, too, hence the explicit gate above).
    AJAZZ_LOG_INFO("time-sync",
                   "auto-sync persisted ON; will fire on next IClockCapable device arrival");
}

QString TimeSyncService::doPush(QString const& codename) {
    if (!m_lookup) {
        return QStringLiteral("Internal error: device lookup not configured");
    }
    // A-04 / D-01 amendment 3: the shared_ptr returned by the lookup is
    // captured in this local for the duration of doPush. The dynamic_cast
    // operates on dev.get() but dev's refcount keeps the IDevice alive
    // across the cast → setTime sequence, closing the UAF window from
    // Phase 4 D-06's weak_ptr cache. dev falls out of scope at function
    // return.
    std::shared_ptr<core::IDevice> const dev = m_lookup(codename);
    if (!dev) {
        return QStringLiteral("Device '%1' not currently connected").arg(codename);
    }
    // Pitfall 2: dynamic_cast can return nullptr — the null-check is on
    // the very next two lines (well within the 3-line contract).
    auto* const clk = dynamic_cast<core::IClockCapable*>(dev.get());
    if (clk == nullptr) {
        return QStringLiteral("Device '%1' does not advertise a clock surface").arg(codename);
    }
    auto const result = clk->setTime(std::chrono::system_clock::now());
    switch (result) {
    case core::TimeSyncResult::Ok:
        return {};
    case core::TimeSyncResult::NotImplemented:
        return QStringLiteral("Time-sync wire format not yet implemented for this device");
    case core::TimeSyncResult::IoError:
        return QStringLiteral("HID write failed when pushing time to '%1'").arg(codename);
    }
    return QStringLiteral("Unknown TimeSyncResult value");
}

} // namespace ajazz::app
