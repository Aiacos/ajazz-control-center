// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stream_dock_input_service.hpp
 * @brief App-layer input dispatch service for AKP-family Stream Dock devices
 *        (AKP03, AKP05/05E, AKP153, AKP815).
 *
 * StreamDockInputService is the first instantiation of the core ActionEngine
 * in the app. It bridges the device-event stream (produced by IDevice::poll()
 * / onEvent) to bound action chains in the active Profile, executing them
 * through one ActionEngine constructed with the app's ActionExecutors and the
 * injected QtExecutor (so Sleep never blocks the poll thread — audit A2 / Pitfall 4).
 *
 * The service is descriptor-driven (Phase 24 generalization): all AKP families
 * share the same dispatch/coalescer/synthesis code paths, parameterised by the
 * active device's descriptor.encoderCount and descriptor.hasTouchStrip.
 *
 *   Family     | LCD keys | Encoders | Touch strip
 *   -----------|----------|----------|-----------
 *   AKP03      | 6        | 3        | no
 *   AKP05/05E  | 10       | 4        | yes (800x480)
 *   AKP153     | 15       | 0        | no
 *   AKP815     | 15       | 0        | no
 *
 * Responsibilities:
 *  - Hold the active Stream Dock's shared_ptr<IDevice> (Pitfall 2 — no raw
 *    ptr) and register the onEvent callback for the session.
 *  - Pump poll() on a tight QTimer cadence (~8 ms, Qt::PreciseTimer) so fast
 *    key/encoder events are not dropped (Pitfall 5). Tests call pump() directly.
 *  - Dispatch each DeviceEvent to the bound chain in the active Profile:
 *      KeyPressed   -> Profile.keys[i].onPress
 *      KeyReleased  -> Profile.keys[i].onRelease
 *      EncoderTurned -> 16 ms rotation coalescer (Pattern 3 / Pitfall 23)
 *      EncoderPressed -> encoders[i].onPress + synthesise paired release (Pattern 4)
 *      TouchDown/Up (raw) -> host-synthesised tap/swipe from the X delta:
 *        small delta = tap -> provisional zone map -> encoders[zone].onPress (INPUT-05a)
 *        large delta = swipe -> emit pageNavRequested(-1/+1) (INPUT-05b)
 *  - Expose the `pageNavRequested(int)` Q_SIGNAL for Phase 16's page model.
 *  - Expose `encoderReleaseSynthesised(uint16_t)` Q_SIGNAL for testability.
 *
 * Design decisions (Phase 15 + Phase 24 generalization):
 *  - NOT QML-exposed for Phase 15 — headless C++ dispatch; QML/UI wiring is
 *    Phase 16's concern.
 *  - ProfileAccessor seam: std::function<Profile const&()> injected by the
 *    caller (Application / test) for testability without a real ProfileController.
 *  - ActionEngine owned by this service (unique_ptr): Application supplies a
 *    pre-constructed engine with the app's real executors + QtExecutor; tests
 *    inject spy engines.
 *  - Poll cadence 8 ms: poll() drains <=8 reports/cycle, so 8 ms * 8 =
 *    effective read ceiling ~1000 reports/s — matches the >100 Hz encoder spec.
 *    Qt::PreciseTimer minimises cadence drift. Revisit with a dedicated reader
 *    thread only if hardware stalls appear in Phase 25.
 *  - GUI-thread dispatch: onEvent fires on the GUI thread (poll is QTimer-
 *    driven), so ActionEngine::run() executes there.
 *  - 16 ms rotation coalescer: accumulates signed delta per encoder; one
 *    onCw/onCcw dispatch per 16 ms window (Pattern 3 / INPUT-04c). The
 *    per-encoder accumulator vector is sized from descriptor.encoderCount on
 *    setActiveDevice() (AKP03=3, AKP05=4, AKP153/AKP815=0).
 *  - Encoder release synthesis: AKP05 is press-only (akp05.md:76); release
 *    is synthesised immediately after onPress (Pattern 4). AKP03 v3 firmware
 *    does emit real release events (akp03.cpp handles EncoderReleased), but
 *    EncoderBinding has no onRelease field yet — see WR-05 TODO.
 *  - Touch zone derivation: PROVISIONAL formula X*4/640 (akp05.md §5);
 *    hardware-reconciled in Phase 25. Only AKP05 triggers this path
 *    (hasTouchStrip=true); other families are gated out.
 */
#pragma once

#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"

#include <QObject>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ajazz::app {

/**
 * @class StreamDockInputService
 * @brief App-layer input dispatch service for AKP-family Stream Dock devices
 *        (AKP03, AKP05/05E, AKP153, AKP815).
 *
 * Not a QML singleton for Phase 15 — plain QObject owned by Application.
 * QML wiring is Phase 16.
 *
 * After Phase 24 the service is fully descriptor-driven: setActiveDevice()
 * reads descriptor.encoderCount and descriptor.hasTouchStrip to configure
 * m_encAccum and the touch-strip gate. No family-specific code remains in
 * the dispatch / coalescer paths.
 *
 * @note Not thread-safe; must be used on the Qt main (GUI) thread.
 */
class StreamDockInputService : public QObject {
    Q_OBJECT

public:
    /// Accessor returning the currently active profile by const-ref.
    /// Injected by Application (or by a test lambda).
    using ProfileAccessor = std::function<core::Profile const&()>;

    /**
     * @brief Construct the service.
     *
     * @param profileAccessor  Returns the currently loaded Profile by const-ref.
     *                         Application wires `[this]{ return
     * m_profileController->activeProfile(); }`. Tests inject a lambda returning a stack-allocated
     * fake Profile.
     * @param engine           Owned ActionEngine constructed with the app's ActionExecutors
     *                         (keyPress/runCommand/openUrl real; plugin stub) and the app's
     *                         QtExecutor. Tests inject an engine with spy executors.
     * @param parent           QObject parent for lifetime management.
     */
    explicit StreamDockInputService(ProfileAccessor profileAccessor,
                                    std::unique_ptr<core::ActionEngine> engine,
                                    QObject* parent = nullptr);

    ~StreamDockInputService() override;

    /**
     * @brief Set (or replace) the active device and start polling.
     *
     * Holds the shared_ptr for the session (ARCH-03 — never re-opens a second
     * handle). Registers the onEvent callback and starts the poll QTimer.
     *
     * Passing a null device stops the timer and releases the held handle.
     *
     * @param device  The device supplied by the Phase-14 StreamDockControlService
     *                (or by makeAkp05WithTransport in tests). Must already be open.
     */
    void setActiveDevice(std::shared_ptr<core::IDevice> device);

    /**
     * @brief Record the codename of the active device for use in deviceEvent().
     *
     * Phase 19 seam: Application calls this alongside setActiveDevice so the
     * deviceEvent signal carries the device id that PluginDeviceBridge needs for
     * registry lookups. Call BEFORE setActiveDevice so the first events carry
     * the correct id.
     *
     * @param codename  Device codename, e.g. "akp05e". Pass an empty string to
     *                  clear (matching a setActiveDevice(nullptr) call).
     */
    void setActiveDeviceCodename(QString const& codename);

    /**
     * @brief Return the codename of the currently active device (or empty string).
     *
     * Phase 21-03 seam: BuiltinActionsService's BrightnessSink needs the active codename
     * to route setBrightness to the right device.
     */
    [[nodiscard]] QString activeDeviceCodename() const noexcept { return m_activeDeviceId; }

    /**
     * @brief Pump one poll cycle on the held device.
     *
     * Calls `m_device->poll()`, which invokes the registered onEvent callback
     * synchronously for each decoded DeviceEvent. Tests call this directly
     * instead of waiting on the internal QTimer.
     *
     * @return Number of events emitted by poll().
     */
    std::size_t pump();

    /**
     * @brief Provisional touch-strip X -> zone index (0..EncoderCount-1).
     *
     * Zone = min(x * EncoderCount / TouchStripRangeX, EncoderCount-1).
     *
     * // PROVISIONAL zone map (akp05.md §5) — hardware-reconciled in Phase 25.
     *
     * @param x  Absolute X coordinate (0..639), already clamped by the backend.
     * @return   Zone index in [0, EncoderCount-1].
     */
    static std::uint16_t zoneForX(std::uint16_t x) noexcept;

    /**
     * @brief Access the owned ActionEngine (non-owning pointer).
     *
     * Phase 21-03 seam: BuiltinActionsService needs a pointer to the single
     * ActionEngine owned by this service so it can call pushPage/popPage and
     * run() for multiactions. The engine is constructed before this service and
     * moved-in; the pointer is valid for the lifetime of this service.
     *
     * @return Non-owning pointer to the ActionEngine (never null after ctor).
     */
    [[nodiscard]] core::ActionEngine* engine() const noexcept { return m_engine.get(); }

Q_SIGNALS:
    /**
     * @brief Emitted when a touch swipe is detected.
     *
     * @param direction -1 = swipe-left (prev page), +1 = swipe-right (next page).
     *
     * Phase 16 owns the page model; this service only emits the intent.
     */
    void pageNavRequested(int direction);

    /**
     * @brief Observable hook for encoder press -> synthetic release pairing.
     *
     * Emitted immediately after synthesising the release that the hardware
     * never sends (device is press-only per akp05.md:76). Tests assert this
     * fires WITHOUT a wire release frame being fed.
     *
     * @param encoderIndex  0-based encoder index.
     */
    void encoderReleaseSynthesised(std::uint16_t encoderIndex);

    /**
     * @brief Emitted for every DeviceEvent that passes through dispatch().
     *
     * Phase 19 seam: PluginDeviceBridge connects to this signal to receive the
     * raw DeviceEvent stream and map it to §4.4 plugin events via sendEvent.
     * Emitted AFTER the Action chain has been dispatched (input service
     * executes its own bindings first, then the bridge observes the event).
     *
     * @param deviceId  The codename of the device that produced the event,
     *                  e.g. "akp05e" (passed through from the active-device
     *                  codename set by setActiveDevice via Application).
     * @param ev        The DeviceEvent — index and value semantics per
     *                  device.hpp DeviceEvent::Kind documentation.
     */
    void deviceEvent(QString const& deviceId, ajazz::core::DeviceEvent const& ev);

private Q_SLOTS:
    /// Drain accumulated encoder deltas: fire one onCw / onCcw per encoder
    /// per 16 ms window, then zero the accumulators.
    void drainCoalescedRotation();

private:
    /// Main dispatch function; called from the onEvent callback.
    void dispatch(core::DeviceEvent const& ev);

    /// Accumulate a rotation delta and arm the 16 ms coalescer timer.
    void onEncoderTurned(std::uint16_t encIndex, std::int32_t delta);

    /// Synthesise a paired EncoderReleased event for the host.
    /// AKP05 is press-only (akp05.md:76) so this is always needed there.
    /// AKP03 v3 firmware does emit real release events; once EncoderBinding
    /// gains an onRelease field the hardware event should be used instead
    /// of a synthetic one on AKP03 (see WR-05 and dispatch()).
    void synthesiseEncoderRelease(std::uint16_t encIndex);

    ProfileAccessor m_profileAccessor;
    std::unique_ptr<core::ActionEngine> m_engine;

    /// Codename of the currently active device (set by setActiveDevice via Application).
    /// Passed in the deviceEvent signal so PluginDeviceBridge can look up the context.
    QString m_activeDeviceId;

    /// Held device handle (ARCH-03 single-handle invariant — Pitfall 2).
    std::shared_ptr<core::IDevice> m_device;

    /// Poll QTimer: tight 8 ms cadence on the GUI thread (Qt::PreciseTimer).
    QTimer* m_pollTimer{nullptr};

    /// Rotation coalescer timer: single-shot 16 ms (Qt::CoarseTimer).
    QTimer* m_coalesceTimer{nullptr};

    /// Whether the active device has a touch strip (AKP05=true; AKP03/153/815=false).
    /// Set from descriptor.hasTouchStrip on setActiveDevice(). Used to gate the
    /// touch dispatch branch so it is inert for non-touch families even if a
    /// touch event were somehow emitted (defence-in-depth; in practice only
    /// AKP05's backend ever emits the DeviceEvent::Kind::Touch{Down,Move,Up} events).
    bool m_hasTouchStrip{false};

    /// Touch-gesture synthesis state. The AKP05 firmware emits only raw
    /// down/move/up (DeviceEvent::Kind::TouchDown/Move/Up); tap-vs-swipe is
    /// derived HERE from the down->up X delta (the firmware has no gesture
    /// concept — akp05_input_corrections.md §4). m_touchActive tracks whether a
    /// press is in progress; m_touchDownX records X at the TouchDown edge.
    bool m_touchActive{false};
    std::uint16_t m_touchDownX{0};

    /// Swipe threshold against the single-byte X range (0..255): a down->up
    /// |X delta| >= this is a swipe (page nav); below it, a tap (zone press).
    /// Heuristic — tune once retail-hardware touch data exists (§4).
    static constexpr int kSwipeThresholdX = 40;

    /// Per-encoder signed delta accumulator for the rotation coalescer.
    /// Sized dynamically from the active device's descriptor.encoderCount on
    /// setActiveDevice() (descriptor-driven: AKP03=3, AKP05=4, AKP153/815=0).
    /// An empty vector means no encoders -- the coalescer loop is inert.
    std::size_t m_encoderCount{0};
    std::vector<std::int32_t> m_encAccum{};

    /// Poll cadence in milliseconds.
    static constexpr int kPollIntervalMs = 8;

    /// Rotation coalescer window in milliseconds.
    static constexpr int kCoalesceIntervalMs = 16;
};

} // namespace ajazz::app
