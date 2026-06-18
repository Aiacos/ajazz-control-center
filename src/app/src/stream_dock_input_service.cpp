// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stream_dock_input_service.cpp
 * @brief StreamDockInputService implementation — poll pump, DeviceEvent dispatch,
 *        16 ms rotation coalescer, encoder release synthesis, provisional touch
 *        zone derivation, and the app's ActionExecutors.
 *
 * Architecture notes (Phase 15):
 *  - ALL dispatch runs on the GUI thread via the QTimer pump (Pitfall 3/5 /
 *    audit A2). No dedicated reader thread; revisit only on a measured hardware
 *    stall in Phase 25.
 *  - The service holds a shared_ptr<IDevice> from the Phase-14 control service.
 *    It NEVER calls DeviceRegistry::open() or a second IDevice::open(). This
 *    preserves the ARCH-03 single-handle invariant.
 *  - COD-031: no nlohmann::json in core or public headers. The service is
 *    app-layer; ActionEngine hands settingsJson to the executors verbatim.
 */
#include "stream_dock_input_service.hpp"

#include "ajazz/core/action_chain_adapter.hpp"
#include "ajazz/core/builtin_action_registry.hpp"
#include "ajazz/core/logger.hpp"

#include <QDesktopServices>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ajazz::app {

namespace {

// ---------------------------------------------------------------------------
// Protocol constants (mirrors akp05_protocol.hpp — do NOT alter wire decode).
// ---------------------------------------------------------------------------

// kEncoderCount is now m_encoderCount (runtime, from descriptor.encoderCount).
// AKP05-specific zoneForX uses a local kAkp05EncoderCount=4 constant.
/// AKP05 touch-strip X range. Touch X is a SINGLE byte (0..255) per the vendor
/// RE (akp05_input_corrections.md §4) — NOT the old 640 BE16 model. Used only by
/// the touch dispatch path (other families never emit touch events because
/// hasTouchStrip=false in their descriptors).
inline constexpr std::uint16_t kTouchStripRangeX = 256;

// ---------------------------------------------------------------------------
// Multi Action resolution (BIND-04/05)
// ---------------------------------------------------------------------------

/// A firing binding is a Multi Action when it carries an @ref ActionInstance
/// whose id is the canonical built-in Multi Action id. Id-match (over a bare
/// "children non-empty" test) is the OpenDeck-parity classification: a Multi
/// Action is identified by its action UUID, not merely by having children. This
/// uses the REAL dispatch id (com.hotspot.streamdock.multiaction) so it stays in
/// lock-step with BuiltinActionRegistry::handles(); no SKU / codename is
/// consulted (BIND-06 invariant).
[[nodiscard]] bool isMultiAction(std::optional<core::ActionInstance> const& instance) {
    return instance.has_value() && instance->id == core::BuiltinActionRegistry::kMultiActionId;
}

/// A firing binding is a Toggle Action when it carries an @ref ActionInstance
/// whose id is the canonical built-in Toggle Action id (BIND-05/07). Id-match
/// (over a bare "states.size() > 1" heuristic) keeps the classification in
/// lock-step with the registry: a Toggle is identified by its action UUID, not
/// merely by having more than one state. Uses the REAL dispatch id
/// (com.hotspot.streamdock.toggleaction); no SKU / codename is consulted
/// (BIND-06 invariant).
[[nodiscard]] bool isToggleAction(std::optional<core::ActionInstance> const& instance) {
    return instance.has_value() && instance->id == core::BuiltinActionRegistry::kToggleActionId;
}

/// Run a press binding: if it is a Multi Action, flatten its children into the
/// existing ActionChain (instanceChildrenToChain) and walk them through the
/// shared ActionEngine (which already defers each child's delayMs -- no new
/// async runner). Otherwise run the legacy @p legacyChain unchanged so bindings
/// without a Multi Action instance keep their existing behaviour (no regression).
template <typename BindingT>
void runPressBinding(core::ActionEngine& engine,
                     BindingT const& binding,
                     core::ActionChain const& legacyChain) {
    if (isMultiAction(binding.instance)) {
        engine.run(core::instanceChildrenToChain(*binding.instance));
        return;
    }
    engine.run(legacyChain);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

StreamDockInputService::StreamDockInputService(ProfileAccessor profileAccessor,
                                               std::unique_ptr<core::ActionEngine> engine,
                                               QObject* parent)
    : QObject(parent), m_profileAccessor(std::move(profileAccessor)), m_engine(std::move(engine)) {
    // Poll timer: tight 8 ms cadence on the GUI thread (Pitfall 5 / audit A2).
    // poll() drains <=8 reports/cycle; 8 ms * 8 = ~1000 reports/s ceiling.
    // Qt::PreciseTimer minimises cadence drift vs Qt::CoarseTimer's ±5% slack.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this] { pump(); });

    // Rotation coalescer: single-shot 16 ms window (Pattern 3 / Pitfall 23).
    // Qt::CoarseTimer is fine — ±5% jitter on 16 ms is negligible for UX.
    m_coalesceTimer = new QTimer(this);
    m_coalesceTimer->setTimerType(Qt::CoarseTimer);
    m_coalesceTimer->setSingleShot(true);
    m_coalesceTimer->setInterval(kCoalesceIntervalMs);
    connect(
        m_coalesceTimer, &QTimer::timeout, this, &StreamDockInputService::drainCoalescedRotation);
}

StreamDockInputService::~StreamDockInputService() = default;

// ---------------------------------------------------------------------------
// setActiveDevice
// ---------------------------------------------------------------------------

void StreamDockInputService::setActiveDevice(std::shared_ptr<core::IDevice> device) {
    // Stop polling if we had a device before.
    m_pollTimer->stop();
    m_coalesceTimer->stop();
    std::fill(m_encAccum.begin(), m_encAccum.end(), 0);

    // CR-03: always deregister the [this]-capturing callback on the outgoing
    // device BEFORE releasing the handle. The backend (Akp05Device) retains
    // the last lambda passed to onEvent(); after m_device.reset() this service
    // no longer owns the backend but StreamDockControlService still holds a
    // shared_ptr to it (ARCH-03 flyweight). If anything drives poll() on that
    // shared backend later, the stale callback would fire through a dangling
    // this-pointer -- a future UAF. Clearing with {} makes the stored lambda
    // a no-op std::function (empty).
    if (m_device) {
        m_device->onEvent({});
    }

    if (!device) {
        m_device.reset();
        m_activeDeviceId.clear();
        m_encoderCount = 0;
        m_encAccum.clear();
        m_hasTouchStrip = false;
        return;
    }

    // Hold the shared_ptr for the session (ARCH-03 -- Pitfall 2: no raw ptr).
    m_device = std::move(device);

    // Descriptor-driven geometry: AKP03=3 encoders, AKP05=4, AKP153/815=0.
    // Size the accumulator to match so each family takes the correct path.
    auto const& desc = m_device->descriptor();
    m_encoderCount = static_cast<std::size_t>(desc.encoderCount);
    m_encAccum.assign(m_encoderCount, 0);
    // Touch-strip flag: AKP05=true, AKP03/153/815=false. Used to gate the
    // TouchStrip dispatch branch (defence-in-depth; backends are already
    // responsible for only emitting events they support).
    m_hasTouchStrip = desc.hasTouchStrip;

    // Register the onEvent callback (synchronously invoked by poll() on the
    // GUI thread -- device.hpp:199 says "I/O thread", which here IS the GUI
    // thread because we pump on it).
    m_device->onEvent([this](core::DeviceEvent const& ev) { dispatch(ev); });

    // Start the poll pump.
    m_pollTimer->start();
}

// ---------------------------------------------------------------------------
// setActiveDeviceCodename
// ---------------------------------------------------------------------------

void StreamDockInputService::setActiveDeviceCodename(QString const& codename) {
    m_activeDeviceId = codename;
}

// ---------------------------------------------------------------------------
// pump
// ---------------------------------------------------------------------------

std::size_t StreamDockInputService::pump() {
    if (!m_device) {
        return 0;
    }
    // CR-01: poll() -> ITransport::read() throws std::runtime_error on device
    // yank (hid_read_timeout returns -1). Catch here so the exception cannot
    // propagate through the QTimer::timeout slot into Qt's event loop, which
    // would call std::terminate (Phase 14 CR-01/CR-02 same failure class).
    // On yank: deregister the stale callback, release the handle, and stop
    // both timers — same teardown as setActiveDevice(nullptr).
    try {
        return m_device->poll();
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-input", "poll() failed (device likely yanked): {}", e.what());
        // Deregister this-capturing callback before releasing the handle so
        // the still-alive backend (held by StreamDockControlService) does not
        // retain a dangling pointer.
        m_device->onEvent({});
        m_device.reset();
        m_pollTimer->stop();
        m_coalesceTimer->stop();
        return 0;
    }
}

// ---------------------------------------------------------------------------
// zoneForX — PROVISIONAL (akp05.md §5 / Phase 25)
// ---------------------------------------------------------------------------

// PROVISIONAL zone map (akp05.md §5) — hardware-reconciled in Phase 25.
// This function is AKP05-specific: only AKP05 emits TouchStrip events (it
// is the only family member with hasTouchStrip=true). AKP03/153/815 never
// call this path because their backends never emit DeviceEvent::Kind::TouchStrip.
// The zone formula uses the AKP05 encoder count (4) since the touch strip
// maps its X coordinate to 4 encoder zones on that device.
std::uint16_t StreamDockInputService::zoneForX(std::uint16_t x) noexcept {
    // AKP05 has 4 encoders and a 640-px touch strip.
    // PROVISIONAL zone map (akp05.md §5) — hardware-reconciled in Phase 25.
    static constexpr std::uint32_t kAkp05EncoderCount = 4u;
    auto const raw = static_cast<std::uint32_t>(x) * kAkp05EncoderCount /
                     static_cast<std::uint32_t>(kTouchStripRangeX);
    auto const capped = std::min<std::uint32_t>(raw, kAkp05EncoderCount - 1u);
    return static_cast<std::uint16_t>(capped);
}

// ---------------------------------------------------------------------------
// dispatch — the DeviceEvent -> Binding -> ActionEngine switch
// ---------------------------------------------------------------------------

void StreamDockInputService::dispatch(core::DeviceEvent const& ev) {
    if (!m_profileAccessor) {
        return;
    }
    auto const& prof = m_profileAccessor();

    switch (ev.kind) {

    // ---- Key events (INPUT-03) -------------------------------------------
    case core::DeviceEvent::Kind::KeyPressed:
        if (auto it = prof.keys.find(ev.index); it != prof.keys.end()) {
            if (isToggleAction(it->second.instance)) {
                // BIND-05/07: a Toggle Action cycles currentState + renders the
                // new state + emits a state-change willAppear (it does NOT run an
                // action chain). Resolved here where the binding identity + index
                // are in scope (the BuiltinHandler cannot see them -- Finding 3).
                dispatchToggle(QStringLiteral("Keypad"), ev.index);
            } else {
                // BIND-04/05: a Multi Action instance runs its children sequentially
                // via the shared engine; otherwise the legacy onPress chain fires.
                runPressBinding(*m_engine, it->second, it->second.onPress);
            }
        }
        break;

    case core::DeviceEvent::Kind::KeyReleased:
        if (auto it = prof.keys.find(ev.index); it != prof.keys.end()) {
            m_engine->run(it->second.onRelease);
        }
        break;

    // ---- Encoder events (INPUT-04) ---------------------------------------
    case core::DeviceEvent::Kind::EncoderTurned:
        onEncoderTurned(ev.index, ev.value);
        break;

    case core::DeviceEvent::Kind::EncoderPressed:
        // Run the press chain, then synthesise the paired release immediately.
        // AKP05 is press-only (akp05.md:76) so the synthesised release is the
        // only release it ever gets. AKP03 v3 firmware sends a real wire release
        // separately (handled in EncoderReleased case below), but since
        // EncoderBinding has no onRelease field yet the extra event is harmless.
        if (auto it = prof.encoders.find(ev.index); it != prof.encoders.end()) {
            if (isToggleAction(it->second.instance)) {
                // BIND-05/07: an encoder-press Toggle cycles + renders via the
                // same seam as a key Toggle (no action chain).
                dispatchToggle(QStringLiteral("Encoder"), ev.index);
            } else {
                // BIND-04/05: an encoder-press Multi Action runs its children too.
                runPressBinding(*m_engine, it->second, it->second.onPress);
            }
        }
        synthesiseEncoderRelease(ev.index);
        break;

    case core::DeviceEvent::Kind::EncoderReleased:
        // WR-05: AKP03 protocol-v3 firmware emits a real EncoderReleased (akp03 dispatches
        // DeviceEvent::Kind::EncoderReleased). Route it to the binding's onRelease chain — this
        // was previously dropped. AKP05 is press-only (akp05.md:76): a real wire release is never
        // emitted by that family, so this case is unreachable for it and the synthesised release
        // (synthesiseEncoderRelease() on EncoderPressed) is intentionally NOT routed to onRelease
        // — doing so would double-fire onRelease on AKP03 v3 (which both synthesises on press AND
        // delivers a real release here).
        if (m_engine) {
            if (auto it = prof.encoders.find(ev.index);
                it != prof.encoders.end() && !it->second.onRelease.empty()) {
                m_engine->run(it->second.onRelease);
            }
        }
        break;

    // ---- Touch strip (INPUT-05): raw down/move/up -> synthesised tap/swipe ----
    // The AKP05 firmware emits only down/move/up + a single-byte X
    // (akp05_input_corrections.md §4); tap vs swipe is derived HERE from the
    // down->up X delta. All three cases gate on descriptor.hasTouchStrip so the
    // AKP05-specific zoneForX formula is never reached on a non-touch family.
    case core::DeviceEvent::Kind::TouchDown:
        if (!m_hasTouchStrip) {
            break;
        }
        m_touchActive = true;
        m_touchDownX = static_cast<std::uint16_t>(static_cast<std::uint32_t>(ev.value) & 0xFFFFu);
        break;

    case core::DeviceEvent::Kind::TouchMove:
        // Position is consumed only at the up edge today. Kept as a distinct
        // case so a future drag/scrub gesture can hook the moving X here.
        break;

    case core::DeviceEvent::Kind::TouchUp: {
        if (!m_hasTouchStrip || !m_touchActive) {
            break;
        }
        m_touchActive = false;
        auto const upX = static_cast<int>(static_cast<std::uint32_t>(ev.value) & 0xFFFFu);
        auto const delta = upX - static_cast<int>(m_touchDownX);
        if (std::abs(delta) >= kSwipeThresholdX) {
            // INPUT-05b: swipe -> page-nav intent (X increasing = rightward = next).
            emit pageNavRequested(delta > 0 ? +1 : -1);
        } else {
            // INPUT-05a: tap -> provisional zone (from the down X) -> encoders[zone].onPress
            auto const zone = zoneForX(m_touchDownX);
            if (auto it = prof.encoders.find(zone); it != prof.encoders.end()) {
                if (isToggleAction(it->second.instance)) {
                    // BIND-05/07: a tapped zone bound to a Toggle cycles + renders
                    // (touch zones register under the "Encoder" controller).
                    dispatchToggle(QStringLiteral("Encoder"), zone);
                } else {
                    // BIND-04/05: a tapped zone bound to a Multi Action runs its children.
                    runPressBinding(*m_engine, it->second, it->second.onPress);
                }
            }
        }
        break;
    }

    // ---- Hotplug events (handled by StreamDockControlService, not here) ---
    case core::DeviceEvent::Kind::Connected:
    case core::DeviceEvent::Kind::Disconnected:
        break;
    }

    // Phase 19 seam: emit the raw DeviceEvent so PluginDeviceBridge can map
    // it to §4.4 plugin events. Emitted AFTER the action chain so the normal
    // input dispatch happens first; the bridge is an observer, not an interceptor.
    emit deviceEvent(m_activeDeviceId, ev);
}

// ---------------------------------------------------------------------------
// onEncoderTurned — accumulate + arm coalescer
// ---------------------------------------------------------------------------

void StreamDockInputService::onEncoderTurned(std::uint16_t encIndex, std::int32_t delta) {
    if (encIndex >= m_encoderCount) {
        return; // Guard: sized by descriptor.encoderCount (AKP03=3, AKP05=4, AKP153/815=0)
    }
    m_encAccum[encIndex] += delta;
    // Arm the single-shot 16 ms coalescer if not already running.
    if (!m_coalesceTimer->isActive()) {
        m_coalesceTimer->start(kCoalesceIntervalMs);
    }
}

// ---------------------------------------------------------------------------
// drainCoalescedRotation — called on 16 ms timer timeout
// ---------------------------------------------------------------------------

void StreamDockInputService::drainCoalescedRotation() {
    if (!m_profileAccessor) {
        std::fill(m_encAccum.begin(), m_encAccum.end(), 0);
        return;
    }
    auto const& prof = m_profileAccessor();

    for (std::uint16_t e = 0; e < static_cast<std::uint16_t>(m_encoderCount); ++e) {
        if (m_encAccum[e] == 0) {
            continue;
        }
        // The accumulated magnitude is accessible here for potential per-detent
        // semantics in a later phase (just read m_encAccum[e] before zeroing).
        auto it = prof.encoders.find(e);
        if (it != prof.encoders.end()) {
            if (m_encAccum[e] > 0) {
                m_engine->run(it->second.onCw);
            } else {
                m_engine->run(it->second.onCcw);
            }
        }
        m_encAccum[e] = 0;
    }
}

// ---------------------------------------------------------------------------
// synthesiseEncoderRelease — host-side release for press-only encoders
// ---------------------------------------------------------------------------

void StreamDockInputService::synthesiseEncoderRelease(std::uint16_t encIndex) {
    // Emit the observable signal (for tests + any future listener).
    emit encoderReleaseSynthesised(encIndex);
    // Note: EncoderBinding has no onRelease field as of profile.hpp:113-118.
    // If a Phase-16 onRelease is added, run it here.
}

// ---------------------------------------------------------------------------
// dispatchToggle — Toggle Action: cycle currentState, render, willAppear (BIND-07)
// ---------------------------------------------------------------------------

void StreamDockInputService::dispatchToggle(QString const& controller, std::uint16_t index) {
    // 1. Cycle currentState = (currentState + 1) % states.size() (mutate + persist).
    //    The cycle hook owns the mutable Profile (ProfileController) -- the input
    //    service only knows the binding through the const ProfileAccessor and so
    //    cannot mutate it directly. A states.size() <= 1 guard lives inside the
    //    mutator (single-state / no-instance bindings are a clean no-op).
    if (m_toggleCycleHook) {
        m_toggleCycleHook(controller, static_cast<int>(index));
    }

    // 2. Re-read the binding's instance through the ProfileAccessor so the render
    //    sees the NEW currentState the cycle hook just wrote. Sourcing from the
    //    live profile (not a pre-cycle copy) is what makes the render reflect the
    //    advance + wrap.
    if (!m_toggleRenderHook || !m_profileAccessor) {
        return;
    }
    auto const& prof = m_profileAccessor();
    core::ActionInstance const* inst = nullptr;
    if (controller == QStringLiteral("Keypad")) {
        if (auto it = prof.keys.find(index); it != prof.keys.end() && it->second.instance) {
            inst = &*it->second.instance;
        }
    } else if (controller == QStringLiteral("Encoder")) {
        if (auto it = prof.encoders.find(index); it != prof.encoders.end() && it->second.instance) {
            inst = &*it->second.instance;
        }
    }
    if (inst == nullptr || inst->states.empty()) {
        return; // nothing to render
    }

    // 3. Render states[currentState] + emit the state-change willAppear (the hook
    //    reuses the existing setState repaint path + the willAppear envelope; the
    //    cross-plugin ownership guard lives inside the bridge -- BIND-06).
    m_toggleRenderHook(controller, static_cast<int>(index), *inst);
}

} // namespace ajazz::app
