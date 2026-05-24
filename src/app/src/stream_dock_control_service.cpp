// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stream_dock_control_service.cpp
 * @brief Implementation of StreamDockControlService.
 *
 * Phase 14 Plan 14-02: closes the UAT gap where the capture-verified wire layer
 * (BAT/LIG/CLE/ULEND) existed but no app code ever called it. This service is
 * the single device-paint path that Phases 15 (input), 16 (controls/persistence),
 * and 19 (plugin bridge) all reuse.
 *
 * Key implementation notes:
 *   Pitfall 1 (T-14b-04): every dynamic_cast<IDisplayCapable*> is followed by a
 *     null-check within 3 lines.
 *   Pitfall 2 (T-14b-01): m_activeDevice holds the shared_ptr for the session;
 *     re-resolved only on setActiveDevice / hot-plug arrival.
 *   Pitfall 3 (T-14b-02): write queue drained via a single-shot QTimer on the GUI
 *     thread — no dedicated I/O thread (A2).
 *
 * Key-index mapping: Profile::keys stores 0-based std::uint16_t indices;
 *   the device backend (Akp05Device::setKeyImage / keyIndexInRange) expects
 *   1-based indices (1..KeyCount=10). The service adds 1 when iterating profile
 *   keys for repaintFromProfile(). Direct assignKeyImage() callers must pass
 *   1-based indices (documented in the header).
 */
#include "stream_dock_control_service.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"

#include <QImage>
#include <QTimer>

#include <stdexcept>
#include <utility>

namespace ajazz::app {

StreamDockControlService::StreamDockControlService(DeviceLookup lookup, QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)), m_drainTimer(new QTimer(this)) {
    m_drainTimer->setSingleShot(true);
    m_drainTimer->setTimerType(Qt::CoarseTimer);
    connect(m_drainTimer, &QTimer::timeout, this, &StreamDockControlService::drainPendingWrites);
}

StreamDockControlService::StreamDockControlService(DeviceLookup lookup,
                                                   ProfileAccessor profileAccessor,
                                                   QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)), m_profileAccessor(std::move(profileAccessor)),
      m_drainTimer(new QTimer(this)) {
    m_drainTimer->setSingleShot(true);
    m_drainTimer->setTimerType(Qt::CoarseTimer);
    connect(m_drainTimer, &QTimer::timeout, this, &StreamDockControlService::drainPendingWrites);
}

StreamDockControlService::~StreamDockControlService() = default;

void StreamDockControlService::setProfileAccessor(ProfileAccessor accessor) {
    m_profileAccessor = std::move(accessor);
}

void StreamDockControlService::setActiveDevice(QString const& codename) {
    if (!m_lookup) {
        AJAZZ_LOG_WARN("stream-dock-control", "setActiveDevice: DeviceLookup not set");
        return;
    }

    // Hold the shared_ptr for the session (ARCH-03 / DISPLAY-06 Pitfall 2).
    // Re-resolve on every call so hot-plug arrival refreshes a yanked handle.
    m_activeDevice = m_lookup(codename);
    if (!m_activeDevice) {
        AJAZZ_LOG_INFO("stream-dock-control",
                       "setActiveDevice: device '{}' not currently connected",
                       codename.toStdString());
        m_activeCodename.clear(); // keep codename in sync with device handle (WR-05)
        return;
    }
    m_activeCodename = codename;

    // open() is idempotent in the backend (returns early if transport already open)
    // and probes firmware via VER GET_FEATURE. Do NOT send a LIG here — see below.
    // CR-01: open() is documented @throws std::runtime_error; catch so a device
    // yank between lookup and open() does not reach the Qt event loop and trigger
    // std::terminate().
    try {
        m_activeDevice->open();
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-control",
                       "setActiveDevice: open() failed for '{}': {}",
                       codename.toStdString(),
                       e.what());
        m_activeDevice.reset();
        m_activeCodename.clear();
        return;
    }

    // DISPLAY-06: Akp05Device::open() deliberately does NOT send a LIG brightness
    // packet (confirmed akp05.cpp:443 — no setBrightness call in open()). The app
    // service owns the "panel lights" decision (CONTEXT.md locked, Pitfall 4).
    // Pitfall 1 (T-14b-04): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        AJAZZ_LOG_INFO("stream-dock-control",
                       "setActiveDevice: device '{}' has no IDisplayCapable surface",
                       codename.toStdString());
        return;
    }
    // CR-01: setBrightness() writes to the HID transport and can throw
    // std::system_error. Non-fatal — device is open; panel may just be dark.
    try {
        disp->setBrightness(kDefaultBrightnessPercent); // LIG — panel lights
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-control",
                       "setActiveDevice: setBrightness failed for '{}': {}",
                       codename.toStdString(),
                       e.what());
        // Non-fatal: device is open; panel may just be dark.
    }
}

void StreamDockControlService::assignKeyImage(std::uint8_t keyIndex, QImage const& img) {
    // Record in the pending map (last-write-wins) and arm the drain timer.
    // The drain slot converts to RGBA8 and calls setKeyImage (Pattern 3).
    m_pendingWrites[keyIndex] = img;
    if (!m_drainTimer->isActive()) {
        m_drainTimer->start(0); // single-shot, 0 ms → fires on next event-loop iteration
    }
}

void StreamDockControlService::repaintFromProfile() {
    if (!m_activeDevice) {
        return;
    }
    // Pitfall 1 (T-14b-04): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        return;
    }

    if (!m_profileAccessor) {
        AJAZZ_LOG_INFO("stream-dock-control", "repaintFromProfile: no profile accessor set");
        return;
    }

    core::Profile const& prof = m_profileAccessor();
    if (prof.keys.empty()) {
        return;
    }

    // Key-index mapping: Profile::keys uses 0-based std::uint16_t map keys;
    // the AKP05E backend requires 1-based indices (1..10). Add 1.
    for (auto const& [profileKeyIndex, binding] : prof.keys) {
        auto const deviceKeyIndex = static_cast<std::uint8_t>(profileKeyIndex + 1);
        // Render KeyState: imagePath takes priority; fall back to background fill.
        QImage img;
        if (binding.state.imagePath && !binding.state.imagePath->empty()) {
            img = QImage(QString::fromStdString(*binding.state.imagePath));
            if (img.isNull()) {
                // Image load failed — fall through to background fill or skip.
                AJAZZ_LOG_WARN("stream-dock-control",
                               "repaintFromProfile: failed to load image '{}'",
                               *binding.state.imagePath);
            }
        }
        if (img.isNull()) {
            if (binding.state.background) {
                // Solid background fill: create an 85x85 image (device native size).
                img = QImage(85, 85, QImage::Format_RGBA8888);
                img.fill(qRgba(binding.state.background->r,
                               binding.state.background->g,
                               binding.state.background->b,
                               255));
            } else {
                // No image and no background — skip this key.
                continue;
            }
        }
        assignKeyImage(deviceKeyIndex, img);
    }
}

QString StreamDockControlService::firmwareVersionFor(QString const& codename) const {
    // Use the held device when codename matches; otherwise do a fresh lookup.
    std::shared_ptr<core::IDevice> dev;
    if (!m_activeCodename.isEmpty() && m_activeCodename == codename) {
        dev = m_activeDevice;
    } else if (m_lookup) {
        dev = m_lookup(codename);
    }
    if (!dev) {
        return QStringLiteral("unknown");
    }
    return QString::fromStdString(dev->firmwareVersion());
}

void StreamDockControlService::drainPendingWrites() {
    if (!m_activeDevice) {
        m_pendingWrites.clear();
        return;
    }
    // Pitfall 1 (T-14b-04): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        m_pendingWrites.clear();
        return;
    }

    for (auto const& [keyIndex, img] : m_pendingWrites) {
        // Convert to RGBA8 if needed — the backend's setKeyImage expects RGBA8.
        QImage const rgba = img.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) {
            continue;
        }
        // ARCH-04: setKeyImage handles JPEG encode -> BAT header -> chunks -> ULEND.
        // Cast to const uchar* and build span for IDisplayCapable::setKeyImage.
        auto const* bits = reinterpret_cast<std::uint8_t const*>(rgba.constBits());
        std::size_t const byteCount =
            static_cast<std::size_t>(rgba.width()) * static_cast<std::size_t>(rgba.height()) * 4u;
        // CR-02: setKeyImage() is documented @throws std::system_error if the
        // transport fails. Catch here so a device yank mid-burst does not propagate
        // through QTimer::timeout into std::terminate(). On failure release the held
        // handle so the next hot-plug arrival triggers a clean setActiveDevice() cycle.
        try {
            disp->setKeyImage(keyIndex,
                              {bits, byteCount},
                              static_cast<std::uint16_t>(rgba.width()),
                              static_cast<std::uint16_t>(rgba.height()));
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("stream-dock-control",
                           "drainPendingWrites: setKeyImage key {} failed: {}",
                           static_cast<int>(keyIndex),
                           e.what());
            // Device likely yanked. Release held handle so next hot-plug arrival
            // triggers a clean setActiveDevice() cycle.
            m_activeDevice.reset();
            m_activeCodename.clear();
            break; // remaining keys in this burst cannot be sent
        }
    }
    m_pendingWrites.clear(); // always clear, even after partial failure (CR-02)
}

} // namespace ajazz::app
