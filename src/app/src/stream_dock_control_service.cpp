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
 * Phase 16 Plan 16-01 (DISPLAY-09): added Q_INVOKABLE setBrightness/clearAll for
 * QML live control (brightness slider -> LIG, clear-all button -> CLE). Added
 * QML_SINGLETON factory (create/registerInstance) mirroring LightingService.
 *
 * Key implementation notes:
 *   Pitfall 1 (T-14b-04): every dynamic_cast<IDisplayCapable*> is followed by a
 *     null-check within 3 lines.
 *   Pitfall 2 (T-14b-01): m_activeDevice holds the shared_ptr for the session;
 *     re-resolved only on setActiveDevice / hot-plug arrival.
 *   Pitfall 3 (T-14b-02): write queue drained via a single-shot QTimer on the GUI
 *     thread -- no dedicated I/O thread (A2).
 *
 * Key-index mapping: Profile::keys stores 0-based std::uint16_t indices;
 *   device backends expect 1-based indices (1..descriptor.keyCount). The
 *   service adds 1 when iterating profile keys for repaintFromProfile().
 *   Direct assignKeyImage() callers must pass 1-based indices (documented
 *   in the header). The actual upper bound is per-family: AKP03=6, AKP05=10,
 *   AKP153/AKP815=15 — enforced by each backend's own setKeyImage guard.
 */
#include "stream_dock_control_service.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"

#include <QImage>
#include <QQmlEngine>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ajazz::app {

namespace {

/// Module-level singleton instance pointer (mirrors LightingService pattern).
StreamDockControlService* g_instance = nullptr;

} // namespace

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

// ---------------------------------------------------------------------------
// QML_SINGLETON factory (Phase 16, DISPLAY-09) -- mirrors LightingService.
// ---------------------------------------------------------------------------

StreamDockControlService* StreamDockControlService::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance != nullptr,
               "StreamDockControlService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    return g_instance;
}

void StreamDockControlService::registerInstance(StreamDockControlService* instance) noexcept {
    g_instance = instance;
}

// ---------------------------------------------------------------------------
// Profile accessor setter
// ---------------------------------------------------------------------------

void StreamDockControlService::setProfileAccessor(ProfileAccessor accessor) {
    m_profileAccessor = std::move(accessor);
}

// ---------------------------------------------------------------------------
// Core device-paint API (Phase 14)
// ---------------------------------------------------------------------------

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
    // and probes firmware via VER GET_FEATURE. Do NOT send a LIG here -- see below.
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
    // packet (confirmed akp05.cpp:443 -- no setBrightness call in open()). The app
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
    // std::system_error. Non-fatal -- device is open; panel may just be dark.
    try {
        disp->setBrightness(kDefaultBrightnessPercent); // LIG -- panel lights
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
    m_pendingWrites[PendingKey{SurfaceTag::Key, keyIndex}] = img;
    if (!m_drainTimer->isActive()) {
        m_drainTimer->start(0); // single-shot, 0 ms -> fires on next event-loop iteration
    }
}

// ---------------------------------------------------------------------------
// Auxiliary-surface assign methods (Phase 23, DISPLAY-10)
//
// PROVISIONAL §5 NOTE (akp05_vendor.md §5 / akp05.md §Layout):
//   The AKP05 wire protocol offers two paths for encoder-adjacent graphics:
//     - ENC (CmdEncImage): per-encoder 100x100 LCD path, as modelled in-code.
//     - DRA (CmdSecondaryScreen): rect-addressable touch-strip zone upload;
//       akp05.md states "no separate encoder LCD — overlays are touch-strip
//       zones (DRA)."
//   BOTH paths are wired and NEITHER is deleted (hardware wins in Phase 25).
//   The encoder-zone -> DRA rect geometry (zone i: location=i, x=i*200, y=0,
//   rectWidth=200, rectHeight=100) is a Ghidra-derived hypothesis
//   (akp05_vendor.md §5). Phase 25 (VERIFY-05) is the live hardware witness
//   that reconciles which path the firmware honors and updates RE doc + code.
//   Do NOT assert this geometry as confirmed.
// ---------------------------------------------------------------------------

void StreamDockControlService::assignMainImage(QImage const& img) {
    // Pitfall 1 (T-23-04): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        return; // no device active or device lacks IDisplayCapable -- no-op
    }
    m_pendingWrites[PendingKey{SurfaceTag::Main, 0}] = img;
    if (!m_drainTimer->isActive()) {
        m_drainTimer->start(0);
    }
}

void StreamDockControlService::assignEncoderImage(std::uint8_t encoderIndex, QImage const& img) {
    // Pitfall 1 (T-23-04): null-check within 3 lines of the cast.
    auto* enc = dynamic_cast<core::IEncoderCapable*>(m_activeDevice.get());
    if (enc == nullptr) {
        return; // no device active or device lacks IEncoderCapable -- no-op
    }
    // Pass encoderIndex THROUGH unchanged -- the backend range-checks < EncoderCount
    // (4) and refuses out-of-range (Pitfall 3 / T-23-02). Do NOT pre-validate.
    m_pendingWrites[PendingKey{SurfaceTag::Encoder, encoderIndex}] = img;
    if (!m_drainTimer->isActive()) {
        m_drainTimer->start(0);
    }
}

void StreamDockControlService::assignTouchStripZone(std::uint8_t zone, QImage const& img) {
    // Pitfall 1 (T-23-04): null-check within 3 lines of the cast.
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(m_activeDevice.get());
    if (strip == nullptr) {
        return; // no device active or device lacks ITouchStripDisplayCapable -- no-op
    }
    // Pass zone THROUGH unchanged -- the backend range-checks < zoneCount (4)
    // and refuses out-of-range (Pitfall 3 / T-23-02). Do NOT pre-validate.
    m_pendingWrites[PendingKey{SurfaceTag::TouchZone, zone}] = img;
    if (!m_drainTimer->isActive()) {
        m_drainTimer->start(0);
    }
}

void StreamDockControlService::repaintFromProfile() {
    // WR-01: new profile always starts at root (member contract: "reset when a
    // new profile is loaded"). Reset here so navigatePage() after a profile
    // switch advances from index 0 rather than from a stale position.
    m_carouselIndex = 0;
    // Single paint path: delegate to repaintPage("root").
    repaintPage(QStringLiteral("root"));
}

void StreamDockControlService::repaintPage(QString const& pageId) {
    if (!m_activeDevice) {
        return;
    }
    // Pitfall 1 (T-14b-04): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        return;
    }

    if (!m_profileAccessor) {
        AJAZZ_LOG_INFO("stream-dock-control", "repaintPage: no profile accessor set");
        return;
    }

    core::Profile const& prof = m_profileAccessor();

    // Resolve the binding set for this page.
    // "root" -> Profile::keys; other id -> look up in Profile::pages.
    std::unordered_map<std::uint16_t, core::Binding> const* bindingSet = nullptr;
    if (pageId == QStringLiteral("root") || pageId.isEmpty()) {
        if (prof.keys.empty()) {
            return; // no-op: root has no bindings
        }
        bindingSet = &prof.keys;
    } else {
        // T-16c-01: use find(), not unguarded at() -- missing page is a no-op.
        auto const it = prof.pages.find(pageId.toStdString());
        if (it == prof.pages.end()) {
            AJAZZ_LOG_INFO("stream-dock-control",
                           "repaintPage: unknown page id '{}', no-op",
                           pageId.toStdString());
            return;
        }
        if (it->second.keys.empty()) {
            return; // page exists but has no bindings -- no-op
        }
        bindingSet = &it->second.keys;
    }

    // Key-index mapping: Profile::keys uses 0-based std::uint16_t map keys;
    // the AKP05E backend requires 1-based indices (1..10). Add 1.
    // (Identical to the loop that was in repaintFromProfile -- single path.)
    for (auto const& [profileKeyIndex, binding] : *bindingSet) {
        // WR-03: profileKeyIndex is uint16_t; adding 1 can overflow uint8_t for
        // indices >= 255. Skip with a warning rather than silently wrapping to 0
        // (index 0 is rejected by keyIndexInRange(), image never sent).
        if (profileKeyIndex >= std::numeric_limits<std::uint8_t>::max()) {
            AJAZZ_LOG_WARN("stream-dock-control",
                           "repaintPage: profile key index {} exceeds uint8_t range, skipping",
                           static_cast<int>(profileKeyIndex));
            continue;
        }
        auto const deviceKeyIndex = static_cast<std::uint8_t>(profileKeyIndex + 1);
        // Render KeyState: imagePath takes priority; fall back to background fill.
        QImage img;
        if (binding.state.imagePath && !binding.state.imagePath->empty()) {
            img = QImage(QString::fromStdString(*binding.state.imagePath));
            if (img.isNull()) {
                // Image load failed -- fall through to background fill or skip.
                AJAZZ_LOG_WARN("stream-dock-control",
                               "repaintPage: failed to load image '{}'",
                               *binding.state.imagePath);
            }
        }
        if (img.isNull()) {
            if (binding.state.background) {
                // Solid background fill at the device's native key resolution.
                // Read from IDisplayCapable::displayInfo() -- descriptor-driven
                // (AKP03=60x60, AKP05=85x85, AKP153=85x85, AKP815=100x100).
                auto const info = disp->displayInfo();
                auto const w = info.widthPx > 0 ? static_cast<int>(info.widthPx) : 85;
                auto const h = info.heightPx > 0 ? static_cast<int>(info.heightPx) : 85;
                img = QImage(w, h, QImage::Format_RGBA8888);
                img.fill(qRgba(binding.state.background->r,
                               binding.state.background->g,
                               binding.state.background->b,
                               255));
            } else {
                // No image and no background -- skip this key.
                continue;
            }
        }
        assignKeyImage(deviceKeyIndex, img);
    }
}

void StreamDockControlService::navigatePage(int direction) {
    if (!m_profileAccessor) {
        return;
    }
    core::Profile const& prof = m_profileAccessor();

    // Build the ordered flat list of top-level pages for the carousel (Decision 2):
    //   [0] = "root"
    //   [1..N] = profile's ProfilePage ids in sorted (deterministic) order.
    // Profile::pages is an unordered_map; sort by id string for a stable ordering.
    std::vector<std::string> pageList;
    pageList.reserve(1 + prof.pages.size());
    pageList.emplace_back("root");
    for (auto const& [id, _page] : prof.pages) {
        pageList.push_back(id);
    }
    std::sort(pageList.begin() + 1, pageList.end()); // root stays first

    // Decision 2: single-root profile -> no-op.
    if (pageList.size() <= 1) {
        return;
    }

    // T-16c-02: clamp index to [0, list.size()-1] -- no wrap.
    auto const listSize = static_cast<int>(pageList.size());
    m_carouselIndex = std::clamp(m_carouselIndex + direction, 0, listSize - 1);

    QString const newPageId =
        QString::fromStdString(pageList[static_cast<std::size_t>(m_carouselIndex)]);
    repaintPage(newPageId);

    // WR-02: notify observers (e.g. PluginDeviceBridge::onActivePageChanged) that the
    // active page has changed so they can retire old-page plugin contexts (willDisappear)
    // and populate new-page contexts (willAppear).
    emit pageNavigated(m_activeCodename, newPageId);
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

// ---------------------------------------------------------------------------
// QML live controls (Phase 16, DISPLAY-09)
// ---------------------------------------------------------------------------

void StreamDockControlService::setBrightness(QString const& codename, int percent) {
    // Resolve device: prefer held handle when codename matches (avoids a
    // redundant DeviceRegistry lookup on the hot path), else fall back to lookup.
    std::shared_ptr<core::IDevice> dev;
    if (!m_activeCodename.isEmpty() && m_activeCodename == codename) {
        dev = m_activeDevice;
    } else if (m_lookup) {
        dev = m_lookup(codename);
    }
    if (!dev) {
        return; // device not connected -- no-op
    }
    // Pitfall 1 (T-16a-02): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(dev.get());
    if (disp == nullptr) {
        return; // not an LCD-key device -- no-op
    }
    // T-16a-04: clamp at the service boundary (backend also clamps, belt+suspenders).
    auto const safe = static_cast<std::uint8_t>(std::clamp(percent, 0, 100));
    // CR-01 (inherited from Phase 14): setBrightness can throw on device yank.
    try {
        disp->setBrightness(safe);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-control",
                       "setBrightness: write failed for '{}': {}",
                       codename.toStdString(),
                       e.what());
    }
}

void StreamDockControlService::clearAll(QString const& codename) {
    // Resolve device: prefer held handle, else lookup.
    std::shared_ptr<core::IDevice> dev;
    if (!m_activeCodename.isEmpty() && m_activeCodename == codename) {
        dev = m_activeDevice;
    } else if (m_lookup) {
        dev = m_lookup(codename);
    }
    if (!dev) {
        return; // device not connected -- no-op
    }
    // Pitfall 1 (T-16a-02): null-check within 3 lines of the cast.
    auto* disp = dynamic_cast<core::IDisplayCapable*>(dev.get());
    if (disp == nullptr) {
        return; // not an LCD-key device -- no-op
    }
    // 0xFF = clear all keys -> CLE opcode (per interfaces block, DISPLAY-09).
    // CR-01: clearKey can throw on device yank.
    try {
        disp->clearKey(0xFF);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-control",
                       "clearAll: write failed for '{}': {}",
                       codename.toStdString(),
                       e.what());
    }
}

// ---------------------------------------------------------------------------
// Drain slot (Phase 14)
// ---------------------------------------------------------------------------

void StreamDockControlService::drainPendingWrites() {
    if (!m_activeDevice) {
        m_pendingWrites.clear();
        return;
    }

    // Resolve capability pointers once per drain cycle (Pitfall 1 / T-23-04):
    // null-check all three casts immediately. Any of them may be nullptr on
    // non-aux-capable devices -- the per-entry dispatch below handles nullptr
    // gracefully (no-op for that surface).
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    auto* enc = dynamic_cast<core::IEncoderCapable*>(m_activeDevice.get());
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(m_activeDevice.get());

    if (disp == nullptr && enc == nullptr && strip == nullptr) {
        // Device has none of the expected capabilities -- clear and bail.
        m_pendingWrites.clear();
        return;
    }

    for (auto const& [key, img] : m_pendingWrites) {
        // Convert to RGBA8 -- all backend methods expect RGBA8.
        QImage const rgba = img.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) {
            continue;
        }
        auto const* bits = reinterpret_cast<std::uint8_t const*>(rgba.constBits());
        std::size_t const byteCount =
            static_cast<std::size_t>(rgba.width()) * static_cast<std::size_t>(rgba.height()) * 4u;
        auto const w = static_cast<std::uint16_t>(rgba.width());
        auto const h = static_cast<std::uint16_t>(rgba.height());

        try {
            switch (key.tag) {
            case SurfaceTag::Key:
                // ARCH-04: key image -> BAT header -> chunks -> ULEND.
                // CR-02: setKeyImage() can throw std::system_error on device yank.
                if (disp != nullptr) {
                    disp->setKeyImage(key.index, {bits, byteCount}, w, h);
                }
                break;

            case SurfaceTag::Main:
                // DISPLAY-10 MAI: whole main LCD strip (800x100). Backend
                // resizes to native + JPEG-encodes. setMainImage does NOT throw
                // on out-of-range (no index); it can throw on transport failure.
                if (disp != nullptr) {
                    disp->setMainImage({bits, byteCount}, w, h);
                }
                break;

            case SurfaceTag::Encoder:
                // DISPLAY-10 ENC (PROVISIONAL -- see PROVISIONAL §5 comment above):
                // per-encoder LCD path. Pass 0-based index through; backend
                // range-checks < EncoderCount (4) and refuses out-of-range.
                // setEncoderImage is a VOID return; backend logs WARN on out-of-range.
                if (enc != nullptr) {
                    enc->setEncoderImage(key.index, {bits, byteCount}, w, h);
                }
                break;

            case SurfaceTag::TouchZone:
                // DISPLAY-10 DRA (PROVISIONAL -- see PROVISIONAL §5 comment above):
                // rect-addressable touch-strip zone. zone i -> (location=i,
                // x=i*200, y=0, rectWidth=200, rectHeight=100).
                // PROVISIONAL geometry (akp05_vendor.md §5; confirm Phase 25).
                // setTouchStripImage returns false on out-of-range location or
                // transport failure; log but do not abort the drain loop.
                if (strip != nullptr) {
                    auto const zone = key.index;
                    auto const zoneX = static_cast<std::uint16_t>(zone * 200u);
                    static constexpr std::uint16_t kZoneWidth = 200u;
                    static constexpr std::uint16_t kZoneHeight = 100u;
                    static constexpr std::uint16_t kZoneY = 0u;
                    bool const ok = strip->setTouchStripImage(
                        {bits, byteCount}, w, h, zone, zoneX, kZoneY, kZoneWidth, kZoneHeight);
                    if (!ok) {
                        AJAZZ_LOG_WARN("stream-dock-control",
                                       "drainPendingWrites: setTouchStripImage zone {} failed "
                                       "(out-of-range or transport error)",
                                       static_cast<int>(zone));
                    }
                }
                break;
            }
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("stream-dock-control",
                           "drainPendingWrites: surface write failed (tag={}, index={}): {}",
                           static_cast<int>(key.tag),
                           static_cast<int>(key.index),
                           e.what());
            // Device likely yanked. Release held handle so next hot-plug arrival
            // triggers a clean setActiveDevice() cycle.
            m_activeDevice.reset();
            m_activeCodename.clear();
            break; // remaining writes in this burst cannot be sent
        }
    }
    m_pendingWrites.clear(); // always clear, even after partial failure (CR-02)
}

} // namespace ajazz::app
