// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stream_dock_input_service.hpp
 * @brief App-layer input dispatch service for AKP05E Stream Dock devices.
 *
 * StreamDockInputService is the first instantiation of the core ActionEngine
 * in the app. It bridges the device-event stream (produced by
 * Akp05Device::poll() / onEvent) to bound action chains in the active Profile,
 * executing them through one ActionEngine constructed with the app's
 * ActionExecutors and the injected QtExecutor (so Sleep never blocks the poll
 * thread — audit A2 / Pitfall 4).
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
 *      TouchStrip/Tap -> provisional zone map -> encoders[zone].onPress (INPUT-05a)
 *      TouchStrip/Swipe -> emit pageNavRequested(-1/+1) (INPUT-05b)
 *  - Expose the `pageNavRequested(int)` Q_SIGNAL for Phase 16's page model.
 *  - Expose `encoderReleaseSynthesised(uint16_t)` Q_SIGNAL for testability.
 *
 * Design decisions (Phase 15 SUMMARY):
 *  - NOT QML-exposed for Phase 15 — headless C++ dispatch; QML/UI wiring is
 *    Phase 16's concern.
 *  - ProfileAccessor seam: std::function<Profile const&()> injected by the
 *    caller (Application / test) for testability without a real ProfileController.
 *  - ActionEngine owned by this service (unique_ptr): Application supplies a
 *    pre-constructed engine with the app's real executors + QtExecutor; tests
 *    inject spy engines.
 *  - Poll cadence 8 ms: poll() drains <=8 reports/cycle (akp05.cpp:474), so
 *    8 ms * 8 = effective read ceiling ~1000 reports/s — matches the >100 Hz
 *    encoder spec (event_bus.hpp:33). Qt::PreciseTimer minimises cadence drift.
 *    Revisit with a dedicated reader thread only if hardware stalls appear
 *    in Phase 25.
 *  - GUI-thread dispatch: onEvent fires on the GUI thread (poll is QTimer-
 *    driven), so ActionEngine::run() executes there. Safe per device.hpp:199
 *    "callback invoked from I/O thread" — here I/O thread == GUI thread.
 *  - 16 ms rotation coalescer: accumulates signed delta per encoder; one
 *    onCw/onCcw dispatch per 16 ms window (Pattern 3 / INPUT-04c).
 *    Accumulated magnitude is preserved in m_encAccum for potential per-detent
 *    semantics in a later phase (just read the array before zeroing).
 *  - Encoder release synthesis: device is press-only (akp05.md:76); release
 *    is synthesised immediately after onPress (Pattern 4).
 *  - Touch zone derivation: PROVISIONAL formula X*4/640 (akp05.md §5);
 *    hardware-reconciled in Phase 25.
 */
#pragma once

#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"

#include <QObject>
#include <QTimer>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace ajazz::app {

/**
 * @class StreamDockInputService
 * @brief App-layer input dispatch service for AKP05E Stream Dock devices.
 *
 * Not a QML singleton for Phase 15 — plain QObject owned by Application.
 * QML wiring is Phase 16.
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

    /// Synthesise a paired EncoderReleased event for the host (device is
    /// press-only — see Pattern 4 / Pitfall 2 / akp05.md:76).
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

    /// Per-encoder signed delta accumulator for the rotation coalescer.
    /// Matches akp05::EncoderCount = 4 (akp05_protocol.hpp:72).
    static constexpr std::size_t kEncoderCount = 4;
    std::array<std::int32_t, kEncoderCount> m_encAccum{};

    /// Poll cadence in milliseconds.
    static constexpr int kPollIntervalMs = 8;

    /// Rotation coalescer window in milliseconds.
    static constexpr int kCoalesceIntervalMs = 16;
};

} // namespace ajazz::app
