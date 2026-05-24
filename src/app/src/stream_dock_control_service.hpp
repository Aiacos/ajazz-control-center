// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stream_dock_control_service.hpp
 * @brief App-layer service that drives the AKP05E Stream Dock LCD panel.
 *
 * StreamDockControlService is the single device-paint path for the v1.3 Stream
 * Dock feature slice (DISPLAY-06/07/08/09, DOCK-01/02). It closes the Phase-10
 * UAT gap where the capture-verified wire layer (BAT/LIG/CLE/ULEND) existed but
 * no app code ever called it.
 *
 * Responsibilities:
 *  - Hold the active Stream Deck open for the session (single held-open
 *    shared_ptr<IDevice> across event-loop turns -- ARCH-03 flyweight invariant).
 *  - Issue setBrightness() at open so the panel lights (DISPLAY-06 -- the LIG
 *    that Akp05Device::open() deliberately does NOT send).
 *  - Coalesce key-image assignments through a single-shot QTimer drain
 *    (last-write-wins per key; Pattern 3 burst mitigation, DOCK-02).
 *  - Repaint all keys from the loaded profile on profileChanged (DISPLAY-08).
 *  - Surface the cached firmware VER string (DOCK-01).
 *  - Expose Q_INVOKABLE setBrightness/clearAll for QML live control (DISPLAY-09,
 *    Phase 16): user-driven brightness slider and clear-all button.
 *
 * Phases 15 (input), 16 (controls/persistence), and 19 (plugin bridge) all
 * build on this reuse surface -- keep its public API stable.
 *
 * Design decisions:
 *  - Phase 14: NOT QML-exposed (plain QObject). Phase 16 adds QML_SINGLETON.
 *  - Phase 16 (DISPLAY-09): QML_SINGLETON exposure added via
 *    QML_NAMED_ELEMENT + QML_SINGLETON + create()/registerInstance() + static_assert.
 *    Application calls registerInstance(m_streamDockControl.get()) in exposeToQml()
 *    so the same instance owned by Application is what QML talks to.
 *    NEVER the bare QML_SINGLETON macro alone (it spawns a second instance per
 *    import -- CLAUDE.md Pitfall 2).
 *  - GUI-thread QTimer drain (Pitfall 3 / A2): avoids cross-thread
 *    shared_ptr<IDevice> hazards; revisit if hardware stalls measured in Phase 25.
 *  - Profile accessor seam: a std::function<core::Profile const&()> injected by
 *    Application so the service is testable without a real ProfileController.
 *  - Default brightness = 80 (Assumption A4); Phase 16 owns the slider.
 *  - Active-device selection: first connected Stream Dock (Phase 14 simplification);
 *    full active-device selection UI is Phase 16.
 */
#pragma once

#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

/**
 * @class StreamDockControlService
 * @brief App-layer device paint path for AKP05E Stream Dock panels.
 *
 * Exposed as `StreamDockControlService` in QML (Phase 16, DISPLAY-09). Pattern
 * mirrors LightingService: non-default-constructible (static_assert build-break
 * lock); QML factory create() returns the Application-owned instance registered
 * via registerInstance(). NEVER the bare QML_SINGLETON macro alone.
 *
 * @note Not thread-safe; must be used on the Qt main (GUI) thread.
 */
class StreamDockControlService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(StreamDockControlService)
    QML_SINGLETON

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
     * @param lookup   Codename -> shared_ptr<IDevice> resolver (same DeviceLookup
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
     * @param lookup          Codename -> shared_ptr<IDevice> resolver.
     * @param profileAccessor Returns the currently loaded profile by const-ref.
     * @param parent          QObject parent for lifetime management.
     */
    explicit StreamDockControlService(DeviceLookup lookup,
                                      ProfileAccessor profileAccessor,
                                      QObject* parent);

    ~StreamDockControlService() override;

    // -------------------------------------------------------------------------
    // QML_SINGLETON factory + build-break lock (Phase 16, DISPLAY-09).
    // Mirrors LightingService pattern (lighting_service.hpp:59-66).
    // -------------------------------------------------------------------------

    /**
     * @brief QML singleton factory -- returns the Application-owned instance.
     *
     * Called by the QML engine the first time the singleton is accessed per
     * import. Returns the instance previously registered via registerInstance().
     * Sets CppOwnership so QML does not try to delete it.
     */
    static StreamDockControlService* create(QQmlEngine* qml, QJSEngine* js);

    /**
     * @brief Register the Application-owned instance with the QML factory.
     *
     * Must be called in Application::exposeToQml() BEFORE the QML engine loads,
     * so that create() returns the live instance (not nullptr).
     *
     * @param instance The Application-owned StreamDockControlService.
     */
    static void registerInstance(StreamDockControlService* instance) noexcept;

    // -------------------------------------------------------------------------
    // Core device-paint API (Phase 14)
    // -------------------------------------------------------------------------

    /**
     * @brief Resolve the device by codename, hold the shared_ptr for the session,
     *        open it, and issue a brightness LIG so the panel lights (DISPLAY-06).
     *
     * Idempotent: if the same codename is already active the function re-resolves
     * and re-opens (open() is idempotent in the backend) -- safe to call on hot-plug
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
     * Delegates to repaintPage("root") (single paint path -- see repaintPage).
     *
     * No-op if no device is active or the device lacks IDisplayCapable.
     */
    void repaintFromProfile();

    /**
     * @brief Repaint the bound keys for a specific profile page (PROFILE-02).
     *
     * Resolves the binding set for @p pageId:
     *   - "root"        -> uses @c Profile::keys (root page).
     *   - other id      -> looks up @c Profile::pages.find(pageId); if not found,
     *                       logs a warning and returns (no-op, no throw -- T-16c-01).
     *
     * Iterates the resolved binding set and enqueues an image for each bound key
     * through the same coalesced drain path as repaintFromProfile (BAT -> chunks
     * -> ULEND). Profile key indices are 0-based (std::uint16_t); the service
     * adds 1 to map to the device's 1-based scheme (same as repaintFromProfile).
     *
     * repaintFromProfile() is implemented as repaintPage("root") — there is only
     * one paint loop.
     *
     * No-op if no device is active or the device lacks IDisplayCapable.
     *
     * @param pageId  Page identifier: "root" or a ProfilePage::id.
     */
    void repaintPage(QString const& pageId);

    /**
     * @brief Navigate the page carousel by @p direction and repaint (PROFILE-02).
     *
     * Intended as a slot connected to StreamDockInputService::pageNavRequested.
     * Implements the carousel semantics from Decision 2:
     *   - Builds an ordered flat list of the profile's top-level pages: "root"
     *     first, then the profile's ProfilePage entries in a deterministic sorted
     *     order (sorted by id string, stable across profile re-loads).
     *   - If the list has only 1 entry (root only), the call is a no-op.
     *   - @p direction == +1: advance index by one (clamp at the end).
     *   - @p direction == -1: go back one (clamp at zero).
     *   - After changing the index, call repaintPage(newPageId).
     *
     * Folder nesting (OpenFolder/BackToParent) is handled by ActionEngine and
     * dispatched separately; this method only drives the flat top-level carousel.
     *
     * @param direction  +1 (next) or -1 (previous).
     */
    Q_SLOT void navigatePage(int direction);

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
     * @brief Set (or replace) the profile accessor.
     *
     * Called by Application after constructing the service with the one-arg ctor.
     * Unconditionally replaces any previously set accessor -- Phase 16 may
     * intentionally supply a richer accessor (persistence-backed slider value)
     * after the initial wiring. The two-arg ctor is preferred for tests where
     * the accessor is known at construction time.
     */
    void setProfileAccessor(ProfileAccessor accessor);

    // -------------------------------------------------------------------------
    // QML live controls (Phase 16, DISPLAY-09)
    // -------------------------------------------------------------------------

    /**
     * @brief Set the panel brightness for the named device (DISPLAY-09).
     *
     * Resolves the device (prefers the held m_activeDevice when codename matches,
     * else m_lookup(codename)), dynamic_cast<IDisplayCapable*> with null-check,
     * clamps percent to [0..100], and calls IDisplayCapable::setBrightness() ->
     * LIG write. No-op if the device is not connected or is not IDisplayCapable.
     *
     * Thread: must be called on the GUI thread (same as all service methods).
     *
     * @param codename Device codename, e.g. "akp05e".
     * @param percent  Brightness 0..100; values outside are clamped.
     * @invokable Callable from QML as StreamDockControlService.setBrightness(codename, value).
     */
    Q_INVOKABLE void setBrightness(QString const& codename, int percent);

    /**
     * @brief Clear all keys on the named device (DISPLAY-09).
     *
     * Resolves the device, dynamic_cast<IDisplayCapable*> with null-check,
     * and calls clearKey(0xFF) -> CLE write. No-op if the device is not
     * connected or is not IDisplayCapable.
     *
     * @param codename Device codename, e.g. "akp05e".
     * @invokable Callable from QML as StreamDockControlService.clearAll(codename).
     */
    Q_INVOKABLE void clearAll(QString const& codename);

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

    /// Current carousel position index (0 = root, 1+ = sorted page ids).
    /// Maintained across navigate() calls; reset when a new profile is loaded.
    int m_carouselIndex{0};

    /// Default brightness sent at open (DISPLAY-06 / Assumption A4).
    /// Phase 16 replaces this with a user-persisted slider value.
    static constexpr std::uint8_t kDefaultBrightnessPercent = 80;
};

// Pitfall 4 build-break lock -- mirrors LightingService.
// If someone adds a default constructor, the compile fails here rather than
// silently spawning a dead QML-owned instance (CLAUDE.md QML_SINGLETON gotcha).
static_assert(!std::is_default_constructible_v<StreamDockControlService>,
              "StreamDockControlService must not be default-constructible -- see ctor note and "
              "LightingService.");

} // namespace ajazz::app
