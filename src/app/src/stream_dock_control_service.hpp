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
    Q_INVOKABLE void setActiveDevice(QString const& codename);

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

    // -------------------------------------------------------------------------
    // Auxiliary-surface assign methods (Phase 23, DISPLAY-10)
    //
    // PROVISIONAL §5 NOTE (akp05_vendor.md §5 / akp05.md §Layout):
    //   The AKP05 wire protocol includes two paths for encoder-adjacent graphics:
    //     - ENC (CmdEncImage): per-encoder 100x100 LCD path, as modelled in-code.
    //     - DRA (CmdSecondaryScreen): rect-addressable touch-strip zones; the spec
    //       states "no separate encoder LCD — overlays are touch-strip zones (DRA)".
    //   BOTH paths are wired and NEITHER is deleted. The encoder-zone -> DRA rect
    //   geometry (zone i: location=i, x=i*200, y=0, rectWidth=200, rectHeight=100)
    //   is a Ghidra-derived hypothesis (akp05_vendor.md §5). Phase 25 (VERIFY-05)
    //   is the live hardware witness that reconciles which path the firmware honors
    //   and updates the RE doc + code. Do NOT assert this geometry as confirmed.
    // -------------------------------------------------------------------------

    /**
     * @brief Assign an image to the AKP05E main LCD strip (MAI, 800x100).
     *
     * Routes through dynamic_cast<IDisplayCapable*> with null-check and calls
     * setMainImage(rgba, w, h). The backend resizes to 800x100 and emits
     * MAI header + chunks + ULEND. This is an editor/explicit action —
     * no profile field is read here (RESEARCH A4).
     *
     * No-op if no device is active or the device lacks IDisplayCapable.
     *
     * @param img Source image at any resolution; backend resizes to 800x100.
     */
    void assignMainImage(QImage const& img);

    /**
     * @brief Assign an image to a per-encoder LCD (ENC path, 0-based index).
     *
     * Routes through dynamic_cast<IEncoderCapable*> with null-check and calls
     * setEncoderImage(encoderIndex, rgba, w, h). The 0-based index is passed
     * THROUGH to the backend unchanged — the backend range-checks < EncoderCount
     * (4) and refuses out-of-range (Pitfall 3 / T-23-02). Backend emits ENC
     * header with index at byte 12 + chunks + ULEND.
     *
     * PROVISIONAL: akp05.md states the AKP05E has no separate encoder LCDs;
     * per-encoder graphics render as touch-strip zones via DRA. Both ENC and DRA
     * paths are kept reachable for Phase-25 hardware reconciliation (VERIFY-05).
     *
     * No-op if no device is active or the device lacks IEncoderCapable.
     *
     * @param encoderIndex 0-based encoder index (0..3 for AKP05E; backend refuses >=4).
     * @param img          Source image; backend resizes to 100x100.
     */
    void assignEncoderImage(std::uint8_t encoderIndex, QImage const& img);

    /**
     * @brief Assign an image to a touch-strip zone via the DRA rect path.
     *
     * Routes through dynamic_cast<ITouchStripDisplayCapable*> with null-check
     * and calls setTouchStripImage(rgba, srcW, srcH, location=zone, x=zone*200,
     * y=0, rectWidth=200, rectHeight=100). The zone id is passed THROUGH to the
     * backend — the backend range-checks < zoneCount (4) and refuses out-of-range
     * (Pitfall 3 / T-23-02). Backend emits DRA rect header + chunks + ULEND.
     *
     * PROVISIONAL geometry (akp05_vendor.md §5): zone i -> (x=i*200, y=0,
     * w=200, h=100). This is a Ghidra-derived hypothesis; Phase 25 (VERIFY-05)
     * verifies it against physical hardware and reconciles the ENC-vs-DRA
     * question.
     *
     * No-op if no device is active or the device lacks ITouchStripDisplayCapable.
     *
     * @param zone 0-based zone index (0..3 for AKP05E; backend refuses >=4).
     * @param img  Source image; backend resizes to 200x100 for this zone rect.
     */
    void assignTouchStripZone(std::uint8_t zone, QImage const& img);

    /**
     * @brief Repaint every bound key from the currently loaded profile (DISPLAY-08).
     *
     * Delegates to repaintPage("root") (single paint path -- see repaintPage).
     *
     * No-op if no device is active or the device lacks IDisplayCapable.
     */
    void repaintFromProfile();

    /**
     * @brief Repaint every bound encoder overlay from the currently loaded profile
     *        (DISPLAY-10 profile-repaint half).
     *
     * Iterates @c Profile::encoders (encoder index -> EncoderBinding; each
     * @c EncoderBinding::state is a @c KeyState carrying imagePath/background).
     * For each bound entry (imagePath OR background present), renders the overlay
     * QImage using the SAME helper as the key repaint (imagePath -> loaded QImage
     * via Qt safe decoders; else background -> solid fill at 200x100), then routes
     * it through @c assignTouchStripZone(encoderIndex, img) — the vendor-preferred
     * DRA zone path (x=encoderIndex*200, PROVISIONAL; akp05_vendor.md §5).
     *
     * Reuses the SAME profile-accessor seam, SAME held handle, and SAME coalesced
     * drain as repaintFromProfile (RESEARCH A5 — no second accessor or timer).
     *
     * Profile encoder indices are 0-based (std::uint16_t); they are passed THROUGH
     * to @c assignTouchStripZone unchanged (the backend range-checks < TouchZoneCount).
     *
     * PROVISIONAL: both ENC and DRA paths are wired and neither is deleted.
     * @c assignEncoderImage (ENC) remains reachable as the documented fallback.
     * Phase 25 (VERIFY-05) is the live hardware witness that reconciles which path
     * the firmware honors and updates the RE doc + code (LOCKED: hardware wins).
     *
     * No-op if no device is active, no profile accessor is set, or the encoder map
     * is empty.
     */
    void repaintEncodersFromProfile();

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

signals:
    /**
     * @brief Emitted after navigatePage() successfully advances to a new page.
     *
     * Connected to PluginDeviceBridge::onActivePageChanged (WR-02 / Phase 19-03):
     * the bridge retires old-page contexts (willDisappear) and populates new-page
     * contexts (willAppear) each time the carousel advances.
     *
     * @param deviceId  Active device codename, e.g. "akp05e".
     * @param pageId    New active page id: "root" or a ProfilePage::id.
     */
    void pageNavigated(QString const& deviceId, QString const& pageId);

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

    /// Surface-tag discriminant for the pending write map (Phase 23, DISPLAY-10).
    /// Distinguishes key images from auxiliary-surface images in the same drain map.
    /// The tag value encodes the surface type; the index encodes the sub-address:
    ///   - Key images:      tag=0, index=keyIndex (1-based, 1..10).
    ///   - Main image:      tag=1, index=0 (whole-strip; no sub-address).
    ///   - Encoder images:  tag=2, index=encoderIndex (0-based, 0..3).
    ///   - Touch-strip zone: tag=3, index=zone (0-based, 0..3).
    /// Last-write-wins per (tag, index) pair (Pattern 3 / DOCK-02).
    enum class SurfaceTag : std::uint8_t { Key = 0, Main = 1, Encoder = 2, TouchZone = 3 };
    struct PendingKey {
        SurfaceTag tag;
        std::uint8_t index;
        [[nodiscard]] bool operator<(PendingKey const& o) const noexcept {
            if (tag != o.tag) {
                return static_cast<std::uint8_t>(tag) < static_cast<std::uint8_t>(o.tag);
            }
            return index < o.index;
        }
    };

    /// Pending write map: last-write-wins per (surface, sub-index) pair (Pattern 3).
    std::map<PendingKey, QImage> m_pendingWrites;

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
