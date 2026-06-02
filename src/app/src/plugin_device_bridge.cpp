// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_device_bridge.cpp
 * @brief PluginDeviceBridge implementation — ContextRegistry + pure helpers +
 *        inbound action router (Phase 19-02).
 *
 * Phase 19-01 delivers:
 *   - ContextRegistry method bodies (pure map operations, no seams).
 *   - coordsForKeyIndex / keyIndexForCoords (Pitfall 2 — single named pair).
 *   - decodeDataUriImage (Don't Hand-Roll — Qt's fromBase64 + loadFromData).
 *   - ownerForActionUuid (Pitfall 4 — longest-prefix dotted-component match).
 *
 * Phase 19-02 adds:
 *   - PluginDeviceBridge constructor wires SdPluginServer::actionReceived -> onAction.
 *   - onAction dispatches the visual action family (setImage/setTitle/setState/
 *     setBG/setFeedback/setText) with context-ownership enforcement (T-19-xplugin).
 *   - onSetImage: decode data-URI -> assignKeyImage (ARCH-04 single encode path);
 *     paintPlaceholder (solid fill via assignKeyImage) on decode failure; no failure
 *     event back (§5).
 *   - onSetTitle / onSetBG: resolve key, render to QImage, call assignKeyImage.
 *   - setFeedback / setText: acknowledged-but-deferred (Phase 23 aux-surface rendering).
 *
 * COD-031: app-layer QJson/QImage only; never nlohmann.
 * ARCH-04: The bridge NEVER calls image_pipeline::encodeForDevice, QImageWriter, or
 *          QImage::save. It passes a QImage to assignKeyImage; the backend + pipeline
 *          handle RGBA->resize->JPEG->BAT->chunks->ULEND.
 */
// AJAZZ_HAVE_WEBSOCKETS gate: this entire TU compiles away when the WebSockets module is
// absent (the bridge composes SdPluginServer). Mirrors obs_client.cpp. Belt-and-suspenders
// against the CMake source list re-adding this .cpp on a minimal build.
#if defined(AJAZZ_HAVE_WEBSOCKETS)

#include "plugin_device_bridge.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"
#include "sd_plugin_server.hpp"
#include "stream_dock_control_service.hpp"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QPolygon>
#include <QRect>
#include <QTimer>

#include <algorithm>

namespace ajazz::app {

// ---------------------------------------------------------------------------
// ContextRegistry
// ---------------------------------------------------------------------------

// static private
QString ContextRegistry::deriveContextId(ActionContext const& ctx) {
    return ctx.deviceId + QChar('#') + ctx.pageId + QChar('#') + ctx.controller + QChar('#') +
           QString::number(ctx.row) + QChar('#') + QString::number(ctx.column);
}

// static private
QString
ContextRegistry::coordKey(QString const& deviceId, QString const& controller, int row, int column) {
    // CR-02: deviceId is the FIRST component so two devices sharing the same
    // controller/row/col cannot collide in m_byCoord (multi-device isolation).
    return deviceId + QChar('#') + controller + QChar('#') + QString::number(row) + QChar('#') +
           QString::number(column);
}

QString ContextRegistry::registerContext(ActionContext const& ctx) {
    QString const ctxId = deriveContextId(ctx);
    ActionContext stored = ctx;
    // Idempotent re-registration (e.g. page re-activation) must NOT wipe the
    // current action state: the context id excludes stateIndex, so preserve the
    // prior stateIndex if this context already exists (setState tracks it).
    auto const it = m_byContext.constFind(ctxId);
    if (it != m_byContext.constEnd()) {
        stored.stateIndex = it->stateIndex;
    }
    m_byContext.insert(ctxId, stored);
    QString const ck = coordKey(ctx.deviceId, ctx.controller, ctx.row, ctx.column);
    m_byCoord.insert(ck, ctxId);
    return ctxId;
}

std::optional<ActionContext> ContextRegistry::byContext(QString const& context) const {
    auto it = m_byContext.constFind(context);
    if (it == m_byContext.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

std::optional<ActionContext> ContextRegistry::byCoord(QString const& deviceId,
                                                      QString const& controller,
                                                      int row,
                                                      int column) const {
    QString const ck = coordKey(deviceId, controller, row, column);
    auto it = m_byCoord.constFind(ck);
    if (it == m_byCoord.constEnd()) {
        return std::nullopt;
    }
    // Resolve the coord key to an ActionContext via the context string.
    auto ctxIt = m_byContext.constFind(*it);
    if (ctxIt == m_byContext.constEnd()) {
        return std::nullopt;
    }
    return *ctxIt;
}

void ContextRegistry::retire(QString const& context) {
    auto it = m_byContext.find(context);
    if (it == m_byContext.end()) {
        return;
    }
    ActionContext const& ctx = *it;
    m_byCoord.remove(coordKey(ctx.deviceId, ctx.controller, ctx.row, ctx.column));
    m_byContext.erase(it);
}

void ContextRegistry::retireDevice(QString const& deviceId) {
    // Collect keys to remove (can't modify while iterating).
    QList<QString> toRemove;
    for (auto it = m_byContext.cbegin(); it != m_byContext.cend(); ++it) {
        if (it->deviceId == deviceId) {
            toRemove.append(it.key());
        }
    }
    for (auto const& ctx : toRemove) {
        retire(ctx);
    }
}

void ContextRegistry::retirePage(QString const& deviceId, QString const& pageId) {
    QList<QString> toRemove;
    for (auto it = m_byContext.cbegin(); it != m_byContext.cend(); ++it) {
        if (it->deviceId == deviceId && it->pageId == pageId) {
            toRemove.append(it.key());
        }
    }
    for (auto const& ctx : toRemove) {
        retire(ctx);
    }
}

void ContextRegistry::clear() {
    m_byContext.clear();
    m_byCoord.clear();
}

bool ContextRegistry::setState(QString const& context, int stateIndex) {
    auto it = m_byContext.find(context);
    if (it == m_byContext.end()) {
        return false; // stale / unknown context
    }
    it->stateIndex = std::max(0, stateIndex); // Elgato states are 0-based
    return true;
}

int ContextRegistry::size() const noexcept {
    return static_cast<int>(m_byContext.count());
}

QList<std::pair<QString, ActionContext>> ContextRegistry::snapshot() const {
    QList<std::pair<QString, ActionContext>> result;
    result.reserve(static_cast<qsizetype>(m_byContext.size()));
    for (auto it = m_byContext.cbegin(); it != m_byContext.cend(); ++it) {
        result.append({it.key(), it.value()});
    }
    return result;
}

// ---------------------------------------------------------------------------
// Pure helpers: coordinate conversion
// ---------------------------------------------------------------------------

GridCoord coordsForKeyIndex(std::uint8_t oneBasedKeyIndex, std::uint8_t keyCols) noexcept {
    // Pitfall 2: 1-based device index -> 0-based Elgato {row, column}.
    // Formula: row = (k-1) / keyCols,  column = (k-1) % keyCols.
    if (keyCols == 0) {
        return {0, 0};
    }
    int const zeroBasedIndex = static_cast<int>(oneBasedKeyIndex) - 1;
    return {zeroBasedIndex / static_cast<int>(keyCols), zeroBasedIndex % static_cast<int>(keyCols)};
}

std::uint8_t keyIndexForCoords(int row, int column, std::uint8_t keyCols) noexcept {
    // Pitfall 2: 0-based Elgato {row, column} -> 1-based device index.
    // Formula: row * keyCols + column + 1.
    return static_cast<std::uint8_t>(row * static_cast<int>(keyCols) + column + 1);
}

// ---------------------------------------------------------------------------
// Pure helper: data: URI -> QImage decoder
// ---------------------------------------------------------------------------

DecodedImage decodeDataUriImage(QString const& dataUri) {
    // Don't hand-roll: use Qt's QByteArray::fromBase64 + QImage::loadFromData.
    // T-19-img: return {ok:false} on any failure (no crash, no exception).

    if (dataUri.isEmpty()) {
        return {false, {}};
    }

    // Find the first comma: everything after it is the base64 body.
    // If no comma is present, treat the whole string as the base64 body
    // (tolerates a raw body without the "data:" prefix).
    qsizetype const commaPos = dataUri.indexOf(QLatin1Char(','));
    QString const bodyStr = (commaPos >= 0) ? dataUri.mid(commaPos + 1) : dataUri;

    if (bodyStr.isEmpty()) {
        return {false, {}};
    }

    // T-19-img: cap base64 body length before any allocation (ARCH-04 / security
    // checklist). 512 KB of base64 encodes at most ~384 KB of raw bytes, which is
    // comfortably above any real key icon (85x85 RGBA = 28,900 bytes). A malicious
    // plugin on the loopback interface cannot allocate more than this via a data: URI.
    constexpr qsizetype kMaxBase64Bytes = 512 * 1024; // 512 KB
    if (bodyStr.size() > kMaxBase64Bytes) {
        return {false, {}};
    }

    // Decode base64. fromBase64 with the default IgnoreBase64DecodingErrors flag
    // silently ignores non-base64 characters rather than returning empty bytes.
    // An empty result indicates an all-whitespace or zero-length input.
    // loadFromData is the actual rejection gate for any garbage payload.
    QByteArray const raw = QByteArray::fromBase64(bodyStr.toUtf8());
    if (raw.isEmpty()) {
        return {false, {}};
    }

    // T-19-img: cap raw byte count before QImage::loadFromData to prevent OOM via
    // an adversarially-crafted compressed image (e.g. 16384x16384 RGBA = 1 GB decoded).
    constexpr qsizetype kMaxRawBytes = 384 * 1024; // 384 KB
    if (raw.size() > kMaxRawBytes) {
        return {false, {}};
    }

    QImage img;
    if (!img.loadFromData(raw)) {
        return {false, {}};
    }

    if (img.isNull()) {
        return {false, {}};
    }

    return {true, img};
}

// ---------------------------------------------------------------------------
// Pure helper: ownerForActionUuid (longest-prefix, dotted-component boundary)
// ---------------------------------------------------------------------------

QString ownerForActionUuid(QString const& dottedActionUuid,
                           QSet<QString> const& registeredPluginUuids) {
    // Pitfall 4: must NOT naively trim the last dotted segment — a plugin uuid
    // "com.x.plugin" must NOT match "com.x.pluginXYZ.action".
    // Correct rule: a registered uuid P is a prefix of dottedActionUuid iff
    //   - dottedActionUuid == P  (exact match — action id IS the plugin uuid)
    //   - dottedActionUuid starts with P + "."  (P is a proper dotted prefix)
    // Among all matching registered uuids, return the longest one.

    QString best;
    for (QString const& candidate : registeredPluginUuids) {
        // Check dotted-prefix boundary.
        bool const isPrefix = (dottedActionUuid == candidate) ||
                              dottedActionUuid.startsWith(candidate + QLatin1Char('.'));
        if (!isPrefix) {
            continue;
        }
        // Keep the longest match.
        if (candidate.length() > best.length()) {
            best = candidate;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// PluginDeviceBridge constructor / destructor (Phase 19-02)
// ---------------------------------------------------------------------------

PluginDeviceBridge::PluginDeviceBridge(SdPluginServer* server,
                                       StreamDockControlService* control,
                                       StreamDockInputService* input,
                                       QObject* parent)
    : QObject(parent), m_server(server), m_control(control), m_input(input) {
    // Phase 19-02: wire SdPluginServer::actionReceived -> onAction.
    // Gate: m_server may be null in tests that only exercise the pure helpers
    // or the registry directly (without a real server).
    if (m_server != nullptr) {
        QObject::connect(
            m_server, &SdPluginServer::actionReceived, this, &PluginDeviceBridge::onAction);
    }
    // Phase 19-03 wires the DeviceEvent -> sendEvent path here.
}

PluginDeviceBridge::~PluginDeviceBridge() = default;

// ---------------------------------------------------------------------------
// Inbound action router (Phase 19-02)
// ---------------------------------------------------------------------------

namespace {

/// Visual action family: the events that target a device key (so they get
/// context resolution + ownership + control-service access in onAction). All
/// other routed events (getSettings, openUrl, logMessage, …) are handled
/// elsewhere (app layer) and are a no-op here.
bool isVisualAction(QString const& event) {
    return event == QStringLiteral("setImage") || event == QStringLiteral("setTitle") ||
           event == QStringLiteral("setState") || event == QStringLiteral("setBG") ||
           event == QStringLiteral("setFeedback") || event == QStringLiteral("setText") ||
           event == QStringLiteral("showAlert") || event == QStringLiteral("showOk");
}

/// Render a transient feedback glyph (Elgato showAlert/showOk) onto an 85x85
/// key image. Drawn with geometric QPainter ops ONLY (no text) so it is safe in
/// a headless QCoreApplication test environment (text needs a font backend).
QImage makeFeedbackGlyph(bool ok) {
    constexpr int kSize = 85;
    QImage img(kSize, kSize, QImage::Format_RGBA8888);
    img.fill(ok ? QColor(20, 90, 30) : QColor(110, 80, 10)); // dark green / amber bg
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (ok) {
        // Green check mark (two strokes).
        QPen pen(QColor(120, 230, 130), 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.drawLine(22, 45, 38, 61);
        p.drawLine(38, 61, 65, 26);
    } else {
        // Amber warning triangle with an exclamation bar + dot.
        p.setBrush(QColor(240, 200, 60));
        p.setPen(Qt::NoPen);
        QPolygon tri;
        tri << QPoint(kSize / 2, 16) << QPoint(16, 68) << QPoint(kSize - 16, 68);
        p.drawPolygon(tri);
        p.setBrush(QColor(40, 30, 0));
        p.drawRect(kSize / 2 - 3, 34, 6, 18); // exclamation bar
        p.drawRect(kSize / 2 - 3, 57, 6, 6);  // exclamation dot
    }
    p.end();
    return img;
}

/// Composite @p title over a copy of @p base (or a neutral fill if base is
/// null), returning the 85x85 result. The text is rendered ONLY when a GUI
/// application is present — QPainter::drawText needs a font backend, which a
/// headless QCoreApplication (unit tests) lacks; there it returns the base
/// unchanged (title acknowledged, not painted) so the bridge stays crash-safe.
QImage compositeTitle(QImage const& base, QString const& title) {
    constexpr int kKeySize = 85;
    QImage img(kKeySize, kKeySize, QImage::Format_RGBA8888);
    if (base.isNull()) {
        img.fill(QColor(30, 30, 30)); // dark neutral background
    } else {
        img.fill(Qt::transparent);
        QPainter bp(&img);
        bp.drawImage(
            QRect(0, 0, kKeySize, kKeySize),
            base.scaled(kKeySize, kKeySize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        bp.end();
    }
    if (title.isEmpty() ||
        qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return img; // no GUI font backend (headless) -> skip text
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font;
    font.setPixelSize(16);
    font.setBold(true);
    p.setFont(font);
    QRect const rect(2, 2, kKeySize - 4, kKeySize - 4);
    auto const flags = Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap;
    // 1px black outline (8 offsets) so the title stays legible over any image.
    p.setPen(QColor(0, 0, 0, 210));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx != 0 || dy != 0) {
                p.drawText(rect.translated(dx, dy), flags, title);
            }
        }
    }
    p.setPen(QColor(255, 255, 255));
    p.drawText(rect, flags, title);
    p.end();
    return img;
}

} // namespace

void PluginDeviceBridge::onAction(QString const& pluginUuid, QJsonObject const& action) {
    // T-19-input: read defensively — toString returns "" on missing/wrong type.
    QString const event = action.value(QStringLiteral("event")).toString();
    if (!isVisualAction(event)) {
        return; // no-op for non-visual actions (handled in later phases)
    }

    // Resolve context (T-19-stale: unknown context -> no-op).
    QString const contextId = action.value(QStringLiteral("context")).toString();
    auto const ctxOpt = m_registry.byContext(contextId);
    if (!ctxOpt.has_value()) {
        return; // stale / unknown context
    }
    ActionContext const& ctx = *ctxOpt;

    // T-19-xplugin: ownership enforcement — a plugin may only drive keys whose
    // bound action it owns (ctx.pluginUuid must match the sending pluginUuid).
    if (ctx.pluginUuid != pluginUuid) {
        return; // cross-plugin denial: no paint, no crash
    }

    // Determine keyCols from the active device's displayInfo (T-19-coord).
    // Guard: if m_control is null (test shim without a real control service) we
    // cannot drive a device, so skip — but still do NOT crash.
    if (m_control == nullptr) {
        return;
    }

    // Source keyCols from the active device via m_control's DeviceLookup.
    // We need keyCols to convert 0-based Elgato coordinates to a 1-based keyIndex.
    // The AKP05E grid is 2 rows × 5 columns; keyCols=5 is the canonical value.
    // Phase 14 holds the device; we call back through the control service's held
    // handle. For the Phase 19-02 implementation we derive keyCols from the control
    // service's firmware accessor pattern — the simplest approach that avoids
    // coupling to an internal detail of StreamDockControlService is to use the
    // device's displayInfo when reachable. Because StreamDockControlService does not
    // expose the held device pointer, we use a conservative fallback: accept keyCols
    // from the context's deviceId via the registry (which is populated with the
    // device's actual key layout at willAppear time in Phase 19-03). For Phase 19-02
    // tests we supply keyCols via a fixed 5 (AKP05E 2×5 layout). The production path
    // wires keyCols through the registry ActionContext's own device descriptor at
    // willAppear time (Phase 19-03 completes this). See the test fixture pattern.
    //
    // Practical resolution: the test pre-registers contexts that carry deviceId;
    // keyCols is hardcoded to the AKP05E constant here. A future Phase-23+ refactor
    // will source keyCols from the DeviceRegistry via the bridge's device accessor.
    // For now, 5 is the correct value for the only connected Stream Dock family
    // device (AKP05E, 2×5). This is documented in the SUMMARY.
    constexpr std::uint8_t kDefaultKeyCols = 5; // AKP05E 2x5 grid

    if (event == QStringLiteral("setImage")) {
        onSetImage(pluginUuid, action, ctx, kDefaultKeyCols);
    } else if (event == QStringLiteral("setTitle")) {
        onSetTitle(pluginUuid, action, ctx, kDefaultKeyCols);
    } else if (event == QStringLiteral("setBG")) {
        onSetBG(pluginUuid, action, ctx, kDefaultKeyCols);
    } else if (event == QStringLiteral("setState")) {
        // setState changes the current 0-based action state. Track it on the
        // context so subsequent willAppear / keyDown / keyUp / dial* events report
        // the correct `state` (Elgato §4.4). The state survives idempotent
        // re-registration (registerContext preserves it). Auto-rendering the
        // manifest's States[index].Image is a follow-up (needs the action manifest
        // plumbed into the bridge); today a plugin pairs setState with its own
        // setImage, which still paints via onSetImage above.
        QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
        int const newState = payload.value(QStringLiteral("state")).toInt(0);
        bool const ok = m_registry.setState(contextId, newState);
        AJAZZ_LOG_INFO("plugin-bridge",
                       "setState: context '{}' -> state {} ({})",
                       contextId.toStdString(),
                       newState,
                       ok ? "updated" : "unknown context");

        // Auto-render the manifest's declared image for the new state (Keypad
        // only for now), so a multi-state action changes its key image without
        // having to push its own setImage. Graceful no-op if no resolver is set
        // or the action declares no image for this state.
        if (ok && m_stateImageResolver && ctx.controller == QStringLiteral("Keypad")) {
            QString const imgPath = m_stateImageResolver(ctx.actionUUID, newState);
            if (!imgPath.isEmpty()) {
                QImage const stateImg(imgPath);
                if (!stateImg.isNull()) {
                    std::uint8_t const keyIndex =
                        keyIndexForCoords(ctx.row, ctx.column, kDefaultKeyCols);
                    try {
                        m_control->assignKeyImage(keyIndex, stateImg);
                    } catch (std::exception const& e) {
                        AJAZZ_LOG_WARN("plugin-bridge",
                                       "setState: assignKeyImage threw for key {}: {}",
                                       static_cast<int>(keyIndex),
                                       e.what());
                    }
                    reapplyTitle(keyIndex); // keep the title over the new state image
                } else {
                    AJAZZ_LOG_WARN("plugin-bridge",
                                   "setState: state image failed to load: {}",
                                   imgPath.toStdString());
                }
            }
        }
    } else if (event == QStringLiteral("showAlert") || event == QStringLiteral("showOk")) {
        // Transient feedback flash (Elgato): paint a warning/ok glyph, then
        // revert to the cached key image after a short dwell. Keypad only.
        if (ctx.controller == QStringLiteral("Keypad")) {
            bool const okGlyph = (event == QStringLiteral("showOk"));
            std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, kDefaultKeyCols);
            QImage const prev = m_control->lastKeyImage(keyIndex);
            try {
                // updateBase=false: the flash is a transient overlay, it must
                // not redefine the key's base image.
                m_control->assignKeyImage(keyIndex, makeFeedbackGlyph(okGlyph), false);
            } catch (std::exception const& e) {
                AJAZZ_LOG_WARN(
                    "plugin-bridge", "{}: assignKeyImage threw: {}", event.toStdString(), e.what());
            }
            // showOk dwells ~0.5s, showAlert ~1.3s (Elgato-ish). Revert to the
            // image that was on the key before the flash (also transient).
            int const dwellMs = okGlyph ? 500 : 1300;
            QPointer<StreamDockControlService> control(m_control);
            QTimer::singleShot(dwellMs, this, [control, keyIndex, prev]() {
                if (control && !prev.isNull()) {
                    try {
                        control->assignKeyImage(keyIndex, prev, false);
                    } catch (std::exception const&) {
                        // Device yanked during the dwell — nothing to restore.
                    }
                }
            });
        }
    } else if (event == QStringLiteral("setFeedback") || event == QStringLiteral("setText")) {
        // Aux-surface rendering (encoder LCD strip / touch strip) deferred to Phase 23.
        AJAZZ_LOG_INFO("plugin-bridge",
                       "{}: aux-surface rendering deferred to Phase 23 for context '{}'",
                       event.toStdString(),
                       contextId.toStdString());
    }
}

void PluginDeviceBridge::onSetImage(QString const& /*pluginUuid*/,
                                    QJsonObject const& action,
                                    ActionContext const& ctx,
                                    std::uint8_t keyCols) {
    // T-19-input: read payload keys defensively.
    QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
    QString const dataUri = payload.value(QStringLiteral("image")).toString();
    // target: 0=hw+sw (default), 1=hw-only, 2=sw-only.
    // Phase 19: treat 1 and 2 as equivalent to 0 (paint the physical key).
    // The sw-only mirror surface is a later refinement (note in SUMMARY).
    // int const target = payload.value(QStringLiteral("target")).toInt(0);

    // Decode the data-URI (T-19-img: returns {ok:false} on any failure — no crash).
    DecodedImage const decoded = decodeDataUriImage(dataUri);
    if (!decoded.ok) {
        // Spec §5: paint crash-free placeholder, send NO failure event back.
        paintPlaceholder(ctx, keyCols);
        return;
    }

    // Compute the 1-based device keyIndex from the 0-based Elgato coordinates.
    // keyIndexForCoords: Pitfall 2 — single named converter from 0-based to 1-based.
    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);

    // Hand the QImage to assignKeyImage — the backend + image_pipeline do the
    // scale + RGBA8 conversion + JPEG q85 encode + BAT->chunks->ULEND burst.
    // ARCH-04: the bridge NEVER calls encodeForDevice / QImageWriter / QImage::save.
    try {
        m_control->assignKeyImage(keyIndex, decoded.image);
    } catch (std::exception const& e) {
        // Device yank during paint (T-14b-01 Phase-14 yank guard). Log and skip;
        // the control service will re-try on the next hot-plug arrival.
        AJAZZ_LOG_WARN("plugin-bridge",
                       "onSetImage: assignKeyImage threw for key {}: {}",
                       static_cast<int>(keyIndex),
                       e.what());
    }
    // Re-apply a previously-set title over the new base (Elgato layering).
    reapplyTitle(keyIndex);
}

void PluginDeviceBridge::onSetTitle(QString const& /*pluginUuid*/,
                                    QJsonObject const& action,
                                    ActionContext const& ctx,
                                    std::uint8_t keyCols) {
    QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
    QString const title = payload.value(QStringLiteral("title")).toString();

    // Track the title as an independent layer per key, then composite it over the
    // BASE image (the action surface set by setImage/setState/setBG) — not over
    // the displayed image, so repeated setTitle calls don't stack layers. The
    // tracked title is re-applied by reapplyTitle() after any later base paint,
    // so it persists across a subsequent setImage (Elgato behaviour).
    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
    if (title.isEmpty()) {
        m_titleByKey.erase(keyIndex); // clearing the title
    } else {
        m_titleByKey[keyIndex] = title;
    }

    QImage const base = m_control->baseKeyImage(keyIndex);
    QImage const composited = compositeTitle(base, title);
    try {
        m_control->assignKeyImage(keyIndex, composited, /*updateBase=*/false);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("plugin-bridge",
                       "onSetTitle: assignKeyImage threw for key {}: {}",
                       static_cast<int>(keyIndex),
                       e.what());
    }
}

void PluginDeviceBridge::reapplyTitle(std::uint8_t keyIndex) {
    auto const it = m_titleByKey.find(keyIndex);
    if (it == m_titleByKey.end() || it->second.isEmpty()) {
        return; // no title overlay for this key
    }
    QImage const composited = compositeTitle(m_control->baseKeyImage(keyIndex), it->second);
    try {
        m_control->assignKeyImage(keyIndex, composited, /*updateBase=*/false);
    } catch (std::exception const&) {
        // Device yank — the title re-applies on the next paint.
    }
}

void PluginDeviceBridge::onSetBG(QString const& /*pluginUuid*/,
                                 QJsonObject const& action,
                                 ActionContext const& ctx,
                                 std::uint8_t keyCols) {
    QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
    // setBG payload: {color:"#RRGGBB"} (per spec §4.3 AJAZZ-only extension).
    QString const colorStr = payload.value(QStringLiteral("color")).toString();
    QColor color(colorStr);
    if (!color.isValid()) {
        color = QColor(30, 30, 30); // fallback neutral
    }

    constexpr int kKeySize = 85;
    QImage img(kKeySize, kKeySize, QImage::Format_RGBA8888);
    img.fill(color);

    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
    try {
        m_control->assignKeyImage(keyIndex, img);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("plugin-bridge",
                       "onSetBG: assignKeyImage threw for key {}: {}",
                       static_cast<int>(keyIndex),
                       e.what());
    }
    reapplyTitle(keyIndex); // keep the title layer over the new background
}

void PluginDeviceBridge::paintPlaceholder(ActionContext const& ctx, std::uint8_t keyCols) {
    // Spec §5: paint a crash-free solid-fill placeholder. Never pass a null QImage
    // to assignKeyImage — use a solid neutral fill at the key's canonical size.
    // T-19-img: no failure event sent back.
    constexpr int kKeySize = 85;
    QImage placeholder(kKeySize, kKeySize, QImage::Format_RGBA8888);
    placeholder.fill(QColor(50, 50, 50)); // neutral dark grey placeholder

    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
    try {
        m_control->assignKeyImage(keyIndex, placeholder);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("plugin-bridge",
                       "paintPlaceholder: assignKeyImage threw for key {}: {}",
                       static_cast<int>(keyIndex),
                       e.what());
    }
}

ContextRegistry& PluginDeviceBridge::registry() noexcept {
    return m_registry;
}

ContextRegistry const& PluginDeviceBridge::registry() const noexcept {
    return m_registry;
}

// ---------------------------------------------------------------------------
// Phase 19-03: onDeviceEvent — DeviceEvent -> §4.4 envelope -> sendEvent
// ---------------------------------------------------------------------------

void PluginDeviceBridge::setProfileAccessor(std::function<core::Profile const&()> accessor) {
    m_profileAccessor = std::move(accessor);
}

void PluginDeviceBridge::setStateImageResolver(
    std::function<QString(QString const&, int)> resolver) {
    m_stateImageResolver = std::move(resolver);
}

void PluginDeviceBridge::onDeviceEvent(QString const& deviceId, core::DeviceEvent const& ev) {
    // T-19-sock: sendEvent re-resolves the live slot each call. Never cache socket*.
    // T-19-leak: only sendEvent to the plugin that owns the context at this coord.
    // COD-031: QJsonObject only; no nlohmann.

    if (m_server == nullptr) {
        return;
    }

    using Kind = core::DeviceEvent::Kind;

    switch (ev.kind) {

    // ------------------------------------------------------------------
    // Key press / release -> keyDown / keyUp
    // ------------------------------------------------------------------
    case Kind::KeyPressed:
    case Kind::KeyReleased: {
        // ev.index is 1-based (device.hpp: "index = 1-based key number").
        constexpr std::uint8_t kDefaultKeyCols = 5; // AKP05E 2x5; Phase 23 sources from registry
        auto const gc = coordsForKeyIndex(static_cast<std::uint8_t>(ev.index), kDefaultKeyCols);
        // CR-02 / WR-04: pass deviceId to byCoord so events from one device cannot
        // route to a plugin context registered for a different device.
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Keypad"), gc.row, gc.column);
        if (!ctxOpt.has_value()) {
            return; // unbound coordinate — silent drop (T-19-leak)
        }
        ActionContext const& ctx = *ctxOpt;
        // Build §4.4 payload.
        QJsonObject const coords{
            {QStringLiteral("row"), gc.row},
            {QStringLiteral("column"), gc.column},
        };
        QJsonObject const payload{
            {QStringLiteral("coordinates"), coords},
            {QStringLiteral("state"), ctx.stateIndex}, // §4.4 current action state
            {QStringLiteral("isInMultiAction"), false},
        };
        QString const eventName =
            (ev.kind == Kind::KeyPressed) ? QStringLiteral("keyDown") : QStringLiteral("keyUp");
        // sendEvent returns false safely if socket is closed (T-19-sock).
        m_server->sendEvent(ctx.pluginUuid, eventName, payload);
        break;
    }

    // ------------------------------------------------------------------
    // Encoder turned -> dialRotate with signed ticks
    // ------------------------------------------------------------------
    case Kind::EncoderTurned: {
        // ev.index is 0-based encoder index; ev.value is signed delta (device.hpp).
        // Convention: encoder at (controller="Encoder", row=0, column=encoderIndex).
        // CR-02 / WR-04: scope lookup to this device.
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, static_cast<int>(ev.index));
        if (!ctxOpt.has_value()) {
            return; // unbound encoder — silent drop
        }
        ActionContext const& ctx = *ctxOpt;
        QJsonObject const payload{
            {QStringLiteral("ticks"), ev.value}, // signed (int32) preserved
            {QStringLiteral("pressed"), false},
            {QStringLiteral("controller"), QStringLiteral("Encoder")},
            {QStringLiteral("state"), ctx.stateIndex},
        };
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("dialRotate"), payload);
        break;
    }

    // ------------------------------------------------------------------
    // Encoder pressed -> dialDown + legacy keyDownCord alias
    // ------------------------------------------------------------------
    case Kind::EncoderPressed: {
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, static_cast<int>(ev.index));
        if (!ctxOpt.has_value()) {
            return;
        }
        ActionContext const& ctx = *ctxOpt;
        QJsonObject const payload{
            {QStringLiteral("controller"), QStringLiteral("Encoder")},
            {QStringLiteral("state"), ctx.stateIndex},
        };
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("dialDown"), payload);
        // AJAZZ legacy alias (§4.4 keyDownCord — AJAZZ-only).
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("keyDownCord"), payload);
        break;
    }

    // ------------------------------------------------------------------
    // Encoder released -> dialUp + legacy keyUpCord alias
    // ------------------------------------------------------------------
    case Kind::EncoderReleased: {
        // Synthesised release from Phase 15's synthesiseEncoderRelease hook.
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, static_cast<int>(ev.index));
        if (!ctxOpt.has_value()) {
            return;
        }
        ActionContext const& ctx = *ctxOpt;
        QJsonObject const payload{
            {QStringLiteral("controller"), QStringLiteral("Encoder")},
            {QStringLiteral("state"), ctx.stateIndex},
        };
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("dialUp"), payload);
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("keyUpCord"), payload);
        break;
    }

    // ------------------------------------------------------------------
    // Touch strip -> touchTap on the completed touch (TouchUp).
    // ------------------------------------------------------------------
    // The firmware emits raw down/move/up + a single-byte X
    // (akp05_input_corrections.md §4). down/move are intermediate edges and are
    // not plugin events; a completed touch (the up edge) is surfaced to plugins
    // as touchTap at its X. Tap-vs-swipe discrimination is a host gesture owned
    // by StreamDockInputService (the action side), not this seam — so the bridge
    // forwards every completed touch and does not attempt to classify it.
    case Kind::TouchDown:
    case Kind::TouchMove:
        return;

    case Kind::TouchUp: {
        auto const x = static_cast<int>(static_cast<std::uint32_t>(ev.value) & 0xFFFFu);

        // Derive the encoder zone from X position (mirrors zoneForX in input service).
        // The zone index is the 0-based encoder index; look up the encoder context.
        // PROVISIONAL zone map (akp05_input_corrections.md §4/§5).
        constexpr int kEncoderCount = 4;
        constexpr int kTouchStripRangeX = 256; // single-byte X per akp05_input_corrections.md §4
        int const zone =
            std::min(static_cast<int>((x * kEncoderCount) / kTouchStripRangeX), kEncoderCount - 1);

        // CR-02 / WR-04: scope lookup to this device.
        auto const ctxOpt = m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, zone);
        if (!ctxOpt.has_value()) {
            return;
        }
        ActionContext const& ctx = *ctxOpt;
        QJsonObject const payload{
            {QStringLiteral("x"), x},
            {QStringLiteral("y"), 0},
            {QStringLiteral("hold"), false},
        };
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("touchTap"), payload);
        break;
    }

    // Connected/Disconnected are handled by onDeviceConnected/Disconnected lifecycle.
    case Kind::Connected:
    case Kind::Disconnected:
        break;
    }
    // WR-04: deviceId is now used in all byCoord lookups above (Q_UNUSED removed).
}

// ---------------------------------------------------------------------------
// Phase 19-03: Lifecycle helpers
// ---------------------------------------------------------------------------

void PluginDeviceBridge::populateContextsForActivePage(QString const& deviceId,
                                                       QString const& pluginUuid) {
    // A6 simplification: scope to root page only (multi-page navigation is Phase 16).
    // willAppear is sent per bound ActionKind::Plugin action on the root page.
    if (!m_profileAccessor || m_server == nullptr) {
        return;
    }

    auto const& prof = m_profileAccessor();
    constexpr std::uint8_t kDefaultKeyCols = 5; // AKP05E 2x5; Phase 23 sources from registry
    QString const pageId = QStringLiteral("root");

    // Enumerate key bindings (0-based uint16_t key index in Profile::keys).
    for (auto const& [keyIdx0, binding] : prof.keys) {
        for (auto const& action : binding.onPress) {
            if (action.kind != core::ActionKind::Plugin) {
                continue;
            }
            QString const actionId = QString::fromStdString(action.id);
            if (actionId.isEmpty()) {
                continue;
            }
            // Resolve the owner via longest-prefix match (T-19-owner).
            QString const owner = ownerForActionUuid(actionId, m_registeredPlugins);
            if (owner.isEmpty()) {
                continue;
            }
            // Filter: if pluginUuid is non-empty, only emit for that plugin.
            if (!pluginUuid.isEmpty() && owner != pluginUuid) {
                continue;
            }
            // Profile::keys use 0-based uint16_t index; device uses 1-based.
            std::uint8_t const keyIdx1 = static_cast<std::uint8_t>(keyIdx0 + 1);
            auto const gc = coordsForKeyIndex(keyIdx1, kDefaultKeyCols);

            ActionContext ctx;
            ctx.deviceId = deviceId;
            ctx.pageId = pageId;
            ctx.row = gc.row;
            ctx.column = gc.column;
            ctx.controller = QStringLiteral("Keypad");
            ctx.actionUUID = actionId;
            ctx.pluginUuid = owner;

            QString const ctxId = m_registry.registerContext(ctx);
            auto const regOpt = m_registry.byContext(ctxId);
            int const stState = regOpt.has_value() ? regOpt->stateIndex : 0;

            // Send willAppear (§4.4) — carries context + coordinates + state.
            QJsonObject const coords{
                {QStringLiteral("row"), gc.row},
                {QStringLiteral("column"), gc.column},
            };
            QJsonObject const payload{
                {QStringLiteral("context"), ctxId},
                {QStringLiteral("coordinates"), coords},
                {QStringLiteral("state"), stState},
                {QStringLiteral("isInMultiAction"), false},
            };
            m_server->sendEvent(owner, QStringLiteral("willAppear"), payload);
        }
    }

    // Enumerate encoder bindings (0-based encoder index in Profile::encoders).
    for (auto const& [encIdx, encBinding] : prof.encoders) {
        for (auto const& action : encBinding.onPress) {
            if (action.kind != core::ActionKind::Plugin) {
                continue;
            }
            QString const actionId = QString::fromStdString(action.id);
            if (actionId.isEmpty()) {
                continue;
            }
            QString const owner = ownerForActionUuid(actionId, m_registeredPlugins);
            if (owner.isEmpty()) {
                continue;
            }
            if (!pluginUuid.isEmpty() && owner != pluginUuid) {
                continue;
            }

            // Encoder convention: controller="Encoder", row=0, column=encoderIndex (0-based).
            ActionContext ctx;
            ctx.deviceId = deviceId;
            ctx.pageId = pageId;
            ctx.row = 0;
            ctx.column = static_cast<int>(encIdx);
            ctx.controller = QStringLiteral("Encoder");
            ctx.actionUUID = actionId;
            ctx.pluginUuid = owner;

            QString const ctxId = m_registry.registerContext(ctx);
            auto const regOpt = m_registry.byContext(ctxId);
            int const stState = regOpt.has_value() ? regOpt->stateIndex : 0;

            QJsonObject const coords{
                {QStringLiteral("row"), 0},
                {QStringLiteral("column"), static_cast<int>(encIdx)},
            };
            QJsonObject const payload{
                {QStringLiteral("context"), ctxId},
                {QStringLiteral("coordinates"), coords},
                {QStringLiteral("state"), stState},
                {QStringLiteral("isInMultiAction"), false},
            };
            m_server->sendEvent(owner, QStringLiteral("willAppear"), payload);
        }
    }

    // Enumerate touch-zone bindings (0-based zone index in Profile::touchZones).
    //
    // CONVENTION LOCK (A2 / Pitfall 5): touch-zone contexts are registered under
    // controller="Encoder", row=0, column=zoneIndex.  This MUST match the locked
    // TouchUp lookup in onDeviceEvent (~line 628):
    //     m_registry.byCoord(deviceId, "Encoder", 0, zone)
    // Do NOT change this registration convention without also changing that lookup.
    for (auto const& [zoneIdx, tzBinding] : prof.touchZones) {
        for (auto const& action : tzBinding.onTap) {
            if (action.kind != core::ActionKind::Plugin) {
                continue;
            }
            QString const actionId = QString::fromStdString(action.id);
            if (actionId.isEmpty()) {
                continue;
            }
            QString const owner = ownerForActionUuid(actionId, m_registeredPlugins);
            if (owner.isEmpty()) {
                continue;
            }
            if (!pluginUuid.isEmpty() && owner != pluginUuid) {
                continue;
            }

            // Register under controller="Encoder", row=0, column=zoneIndex.
            ActionContext ctx;
            ctx.deviceId = deviceId;
            ctx.pageId = pageId;
            ctx.row = 0;
            ctx.column = static_cast<int>(zoneIdx);
            ctx.controller = QStringLiteral("Encoder");
            ctx.actionUUID = actionId;
            ctx.pluginUuid = owner;

            QString const ctxId = m_registry.registerContext(ctx);

            QJsonObject const coords{
                {QStringLiteral("row"), 0},
                {QStringLiteral("column"), static_cast<int>(zoneIdx)},
            };
            QJsonObject const payload{
                {QStringLiteral("context"), ctxId},
                {QStringLiteral("coordinates"), coords},
                {QStringLiteral("isInMultiAction"), false},
            };
            m_server->sendEvent(owner, QStringLiteral("willAppear"), payload);
        }
    }
}

void PluginDeviceBridge::retirePageContexts(QString const& deviceId,
                                            QString const& pageId,
                                            QString const& pluginUuid) {
    // Use the snapshot() accessor (Rule 2: added to ContextRegistry) to enumerate
    // all currently registered contexts, filter by deviceId + pageId (+ optional
    // pluginUuid), send willDisappear to each, then retire them individually.
    // This avoids mutating the registry while iterating.
    if (m_server == nullptr) {
        return;
    }

    auto const entries = m_registry.snapshot();
    for (auto const& [ctxId, ctx] : entries) {
        if (ctx.deviceId != deviceId || ctx.pageId != pageId) {
            continue;
        }
        if (!pluginUuid.isEmpty() && ctx.pluginUuid != pluginUuid) {
            continue;
        }
        // Send willDisappear — plugin may have already disconnected (socket closed).
        // sendEvent returns false safely (T-19-sock).
        QJsonObject const coords{
            {QStringLiteral("row"), ctx.row},
            {QStringLiteral("column"), ctx.column},
        };
        QJsonObject const payload{
            {QStringLiteral("context"), ctxId},
            {QStringLiteral("coordinates"), coords},
            {QStringLiteral("isInMultiAction"), false},
        };
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("willDisappear"), payload);
        m_registry.retire(ctxId);
    }
}

// ---------------------------------------------------------------------------
// Phase 19-03: Lifecycle slots
// ---------------------------------------------------------------------------

void PluginDeviceBridge::onPluginRegistered(QString const& pluginUuid) {
    m_registeredPlugins.insert(pluginUuid);
    // WR-03: use the actual active device codename maintained by onDeviceConnected /
    // onDeviceDisconnected. Fall back to "akp05e" only when no device has yet
    // connected (test shim path or startup race). This removes the hardcoded
    // "akp05e" that silently broke multi-device setups where the active device
    // is not an AKP05E.
    QString const deviceId =
        m_activeDeviceId.isEmpty() ? QStringLiteral("akp05e") : m_activeDeviceId;
    populateContextsForActivePage(deviceId, pluginUuid);
}

void PluginDeviceBridge::onPluginDisconnected(QString const& pluginUuid) {
    m_registeredPlugins.remove(pluginUuid);
    // WR-03: retire contexts using the actual active device codename so that
    // contexts for a non-akp05e device are not stranded in the registry.
    QString const deviceId =
        m_activeDeviceId.isEmpty() ? QStringLiteral("akp05e") : m_activeDeviceId;
    QString const pageId = QStringLiteral("root");
    retirePageContexts(deviceId, pageId, pluginUuid);
}

void PluginDeviceBridge::onDeviceConnected(QString const& deviceId) {
    // WR-03: track the most-recently-connected device so that subsequent
    // onPluginRegistered / onPluginDisconnected calls use the correct codename.
    // A (re)connect invalidates any per-key title overlays from a prior device
    // (the control service clears its per-key image caches on the same edge).
    if (m_activeDeviceId != deviceId) {
        m_titleByKey.clear();
    }
    m_activeDeviceId = deviceId;

    // Populate contexts for the active page.
    populateContextsForActivePage(deviceId);

    // Send deviceDidConnect to all registered plugins (§4.4).
    if (m_server == nullptr) {
        return;
    }
    QJsonObject const deviceInfo{
        {QStringLiteral("name"), deviceId},
        {QStringLiteral("type"), QStringLiteral("streamdeck")},
        {QStringLiteral("size"),
         QJsonObject{
             {QStringLiteral("columns"), 5},
             {QStringLiteral("rows"), 2},
         }},
    };
    QJsonObject const payload{{QStringLiteral("deviceInfo"), deviceInfo}};
    for (QString const& uuid : m_registeredPlugins) {
        m_server->sendEvent(uuid, QStringLiteral("deviceDidConnect"), payload);
    }
}

void PluginDeviceBridge::onDeviceDisconnected(QString const& deviceId) {
    if (m_server == nullptr) {
        return;
    }
    // WR-03: clear the active device tracker when the active device disconnects.
    if (m_activeDeviceId == deviceId) {
        m_activeDeviceId.clear();
    }
    // WR-01: send willDisappear for every visible action context on this device
    // before retiring them. This matches the Elgato SDK spec (§4.4) requirement
    // that willDisappear is sent per context when a device disappears, and mirrors
    // the onPluginDisconnected path. Failure to send willDisappear causes plugins
    // that track mounted-instance counts to mis-count.
    retirePageContexts(deviceId, QStringLiteral("root"), {});
    // retireDevice() is a safety net for any contexts on non-root pages (Phase 16
    // multi-page). For the Phase 19 root-only scope it is a no-op after the call
    // above, but calling it keeps the retireDevice path in place for future pages.
    m_registry.retireDevice(deviceId);

    // Send deviceDidDisconnect to all registered plugins (§4.4).
    QJsonObject const deviceInfo{
        {QStringLiteral("name"), deviceId},
        {QStringLiteral("type"), QStringLiteral("streamdeck")},
    };
    QJsonObject const payload{{QStringLiteral("deviceInfo"), deviceInfo}};
    for (QString const& uuid : m_registeredPlugins) {
        m_server->sendEvent(uuid, QStringLiteral("deviceDidDisconnect"), payload);
    }
}

void PluginDeviceBridge::onActivePageChanged(QString const& deviceId, QString const& pageId) {
    // Retire old page contexts (willDisappear) then populate the new page (willAppear).
    // A6: root-page only for Phase 19; multi-page navigation is Phase 16's authority.
    retirePageContexts(deviceId, QStringLiteral("root"), {});
    populateContextsForActivePage(deviceId);
    Q_UNUSED(pageId); // multi-page scope deferred to Phase 16
}

} // namespace ajazz::app

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
