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

#include "ajazz/core/logger.hpp"

#include <QDesktopServices>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace ajazz::app {

namespace {

// ---------------------------------------------------------------------------
// Protocol constants (mirrors akp05_protocol.hpp — do NOT alter wire decode).
// ---------------------------------------------------------------------------

/// Matches akp05::EncoderCount = 4 (akp05_protocol.hpp:72).
inline constexpr std::uint8_t kEncoderCount = 4;
/// Matches akp05::TouchStripRangeX = 640 (akp05_protocol.hpp:82).
inline constexpr std::uint16_t kTouchStripRangeX = 640;

/// Touch gesture indices packed into the upper 16 bits of DeviceEvent::value
/// by akp05.cpp:513-517.
inline constexpr std::uint32_t kGestureTap = 0u;
inline constexpr std::uint32_t kGestureSwipeLeft = 1u;
inline constexpr std::uint32_t kGestureSwipeRight = 2u;
// gesture 3 = LongPress — no-op for Phase 15; note for Phase 16.

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
    m_encAccum.fill(0);

    if (!device) {
        m_device.reset();
        return;
    }

    // Hold the shared_ptr for the session (ARCH-03 — Pitfall 2: no raw ptr).
    m_device = std::move(device);

    // Register the onEvent callback (synchronously invoked by poll() on the
    // GUI thread — device.hpp:199 says "I/O thread", which here IS the GUI
    // thread because we pump on it).
    m_device->onEvent([this](core::DeviceEvent const& ev) { dispatch(ev); });

    // Start the poll pump.
    m_pollTimer->start();
}

// ---------------------------------------------------------------------------
// pump
// ---------------------------------------------------------------------------

std::size_t StreamDockInputService::pump() {
    if (!m_device) {
        return 0;
    }
    return m_device->poll();
}

// ---------------------------------------------------------------------------
// zoneForX — PROVISIONAL (akp05.md §5 / Phase 25)
// ---------------------------------------------------------------------------

// PROVISIONAL zone map (akp05.md §5) — hardware-reconciled in Phase 25.
std::uint16_t StreamDockInputService::zoneForX(std::uint16_t x) noexcept {
    // Bounded formula: result is in [0, EncoderCount-1].
    // PROVISIONAL zone map (akp05.md §5) — hardware-reconciled in Phase 25.
    auto const raw = static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(kEncoderCount) /
                     static_cast<std::uint32_t>(kTouchStripRangeX);
    auto const capped =
        std::min<std::uint32_t>(raw, static_cast<std::uint32_t>(kEncoderCount) - 1u);
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
            m_engine->run(it->second.onPress);
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
        // Device is press-only per akp05.md:76 — do NOT wait for a wire release.
        if (auto it = prof.encoders.find(ev.index); it != prof.encoders.end()) {
            m_engine->run(it->second.onPress);
        }
        synthesiseEncoderRelease(ev.index);
        break;

    case core::DeviceEvent::Kind::EncoderReleased:
        // Dormant on hardware (akp05.md:76 press-only); ignore safely.
        break;

    // ---- Touch strip (INPUT-05) ------------------------------------------
    case core::DeviceEvent::Kind::TouchStrip: {
        // Touch value packing: (gesture<<16)|X (akp05.cpp:513-517).
        auto const gesture = static_cast<std::uint32_t>(ev.value) >> 16u;
        auto const x = static_cast<std::uint16_t>(static_cast<std::uint32_t>(ev.value) & 0xFFFFu);

        if (gesture == kGestureTap) {
            // INPUT-05a: tap -> provisional zone -> encoders[zone].onPress
            auto const zone = zoneForX(x);
            if (auto it = prof.encoders.find(zone); it != prof.encoders.end()) {
                m_engine->run(it->second.onPress);
            }
        } else if (gesture == kGestureSwipeLeft) {
            // INPUT-05b: swipe-left -> prev page intent
            emit pageNavRequested(-1);
        } else if (gesture == kGestureSwipeRight) {
            // INPUT-05b: swipe-right -> next page intent
            emit pageNavRequested(+1);
        }
        // gesture 3 = LongPress: no-op for Phase 15 (note for Phase 16).
        break;
    }

    // ---- Hotplug events (handled by StreamDockControlService, not here) ---
    case core::DeviceEvent::Kind::Connected:
    case core::DeviceEvent::Kind::Disconnected:
        break;
    }
}

// ---------------------------------------------------------------------------
// onEncoderTurned — accumulate + arm coalescer
// ---------------------------------------------------------------------------

void StreamDockInputService::onEncoderTurned(std::uint16_t encIndex, std::int32_t delta) {
    if (encIndex >= kEncoderCount) {
        return; // Guard: backend already validates index < EncoderCount
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
        m_encAccum.fill(0);
        return;
    }
    auto const& prof = m_profileAccessor();

    for (std::uint16_t e = 0; e < kEncoderCount; ++e) {
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

} // namespace ajazz::app
