// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stream_dock_control_service.hpp
 * @brief App-layer service that drives the AKP05E Stream Dock LCD panel.
 *
 * StreamDockControlService is the single device-paint path for the v1.3 Stream
 * Dock feature slice (DISPLAY-06/07/08, DOCK-01/02). It closes the Phase-10 UAT
 * gap where the capture-verified wire layer (BAT/LIG/CLE/ULEND) existed but no
 * app code ever called it.
 *
 * Responsibilities:
 *  - Hold the active Stream Deck open for the session (single held-open
 *    shared_ptr<IDevice> across event-loop turns — ARCH-03 flyweight invariant).
 *  - Issue setBrightness() at open so the panel lights (DISPLAY-06 — the LIG
 *    that Akp05Device::open() deliberately does NOT send).
 *  - Coalesce key-image assignments through a single-shot QTimer drain
 *    (last-write-wins per key; Pattern 3 burst mitigation, DOCK-02).
 *  - Repaint all keys from the loaded profile on profileChanged (DISPLAY-08).
 *  - Surface the cached firmware VER string (DOCK-01).
 *
 * Phases 15 (input), 16 (controls/persistence), and 19 (plugin bridge) all
 * build on this reuse surface — keep its public API stable.
 *
 * Design decisions (Phase 14 SUMMARY):
 *  - NOT QML-exposed for Phase 14: a plain QObject suffices; QML wiring is
 *    Phase 16's concern (active-device selection UI, brightness slider).
 *  - GUI-thread QTimer drain (Pitfall 3 / A2): avoids cross-thread
 *    shared_ptr<IDevice> hazards; revisit if hardware stalls measured in Phase 25.
 *  - Profile accessor seam: a std::function<core::Profile const&()> injected by
 *    Application so the service is testable without a real ProfileController.
 *  - Default brightness = 80 (Assumption A4); Phase 16 owns the slider.
 *  - Active-device selection: first connected Stream Deck (Phase 14 simplification);
 *    full active-device selection UI is Phase 16.
 */
#pragma once

#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace ajazz::app {

/**
 * @class StreamDockControlService
 * @brief App-layer device paint path for AKP05E Stream Dock panels.
 *
 * Not a QML singleton for Phase 14 — constructed and owned by Application,
 * which wires the DeviceLookup lambda and the profile accessor. A plain
 * QObject is sufficient; QML singletons are Phase 16.
 *
 * @note Not thread-safe; must be used on the Qt main (GUI) thread.
 */
class StreamDockControlService : public QObject {
    Q_OBJECT

public:
    /// Codename -> shared_ptr<IDevice>; same shape as TimeSyncService / LightingService.
    using DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>;

    /// Accessor for the currently active profile (injected by Application so the
    /// service is testable without a real ProfileController).
    using ProfileAccessor = std::function<core::Profile const&()>;

    /**
     * @brief Construct the service with a device lookup and an optional profile
     *        accessor.
     *
     * @param lookup   Codename → shared_ptr<IDevice> resolver (same DeviceLookup
     *                 shape as TimeSyncService / LightingService).
     * @param parent   QObject parent for lifetime management.
     */
    explicit StreamDockControlService(DeviceLookup lookup, QObject* parent);

    /**
     * @brief Construct the service with a device lookup, a profile accessor, and
     *        an optional QObject parent.
     *
     * Overload used when repaintFromProfile() needs to iterate the active profile
     * without a real ProfileController (unit tests, and Application wiring).
     *
     * @param lookup          Codename → shared_ptr<IDevice> resolver.
     * @param profileAccessor Returns the currently loaded profile by const-ref.
     * @param parent          QObject parent for lifetime management.
     */
    explicit StreamDockControlService(DeviceLookup lookup,
                                      ProfileAccessor profileAccessor,
                                      QObject* parent);

    ~StreamDockControlService() override;

    /**
     * @brief Resolve the device by codename, hold the shared_ptr for the session,
     *        open it, and issue a brightness LIG so the panel lights (DISPLAY-06).
     *
     * Idempotent: if the same codename is already active the function re-resolves
     * and re-opens (open() is idempotent in the backend) — safe to call on hot-plug
     * arrival to refresh the held handle after a yank+replug.
     *
     * @param codename Device codename, e.g. "akp05e".
     */
    void setActiveDevice(QString const& codename);

    /**
     * @brief Enqueue a key-image assignment for the next coalesced drain.
     *
     * Records in the pending write map (last-write-wins per key) and arms the
     * single-shot drain timer. The drain slot calls IDisplayCapable::setKeyImage
     * with the 1-based key index; the backend encodes RGBA -> JPEG -> BAT -> 1024-
     * byte chunks -> ULEND (DISPLAY-07, DOCK-02).
     *
     * @param keyIndex 1-based key index (1..10 for AKP05E).
     * @param img      Source image at any resolution; backend resizes to 85x85.
     */
    void assignKeyImage(std::uint8_t keyIndex, QImage const& img);

    /**
     * @brief Repaint every bound key from the currently loaded profile (DISPLAY-08).
     *
     * Iterates the active profile's keys map and enqueues an image for each bound
     * key through the coalesced drain path. Profile key indices are 0-based
     * (std::uint16_t map keys in Profile::keys); the service adds 1 to map to the
     * device's 1-based scheme.
     *
     * No-op if no device is active or the device lacks IDisplayCapable.
     */
    void repaintFromProfile();

    /**
     * @brief Return the cached firmware VER string for the given codename (DOCK-01).
     *
     * Uses the held device when codename matches the active one; otherwise falls
     * back to a fresh lookup. Returns "unknown" if the device is offline or the
     * probe failed.
     *
     * @param codename Device codename.
     * @return Firmware version string, e.g. "V3.AKP05E.01.007".
     */
    [[nodiscard]] QString firmwareVersionFor(QString const& codename) const;

    /**
     * @brief Set the profile accessor (called by Application after constructing the
     *        service with the two-arg ctor; no-op if the accessor was already set).
     */
    void setProfileAccessor(ProfileAccessor accessor);

private slots:
    /// Drain the pending write map: call setKeyImage() for every queued entry, then
    /// clear the map. Runs on the GUI thread via QTimer::singleShot (Pitfall 3).
    void drainPendingWrites();

private:
    DeviceLookup m_lookup;
    ProfileAccessor m_profileAccessor;

    /// Held shared_ptr keeps the single HID handle open for the session (ARCH-03 /
    /// DISPLAY-06 Pitfall 2). Re-resolved on setActiveDevice / hot-plug arrival.
    std::shared_ptr<core::IDevice> m_activeDevice;
    QString m_activeCodename;

    /// Pending write map: last-write-wins per 1-based key index (Pattern 3).
    std::map<std::uint8_t, QImage> m_pendingWrites;

    /// Single-shot coalescing timer (Pattern 3 / DOCK-02 burst mitigation).
    QTimer* m_drainTimer{nullptr};

    /// Default brightness sent at open (DISPLAY-06 / Assumption A4).
    /// Phase 16 replaces this with a user-persisted slider value.
    static constexpr std::uint8_t kDefaultBrightnessPercent = 80;
};

} // namespace ajazz::app
