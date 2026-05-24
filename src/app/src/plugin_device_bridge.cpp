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
#include "plugin_device_bridge.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"
#include "sd_plugin_server.hpp"
#include "stream_dock_control_service.hpp"

#include <QByteArray>
#include <QColor>

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
    m_byContext.insert(ctxId, ctx);
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

/// Visual action family: the six events that target a device key.
/// All other routed events (getSettings, openUrl, logMessage, etc.) are handled
/// in later phases and are a no-op here.
bool isVisualAction(QString const& event) {
    return event == QStringLiteral("setImage") || event == QStringLiteral("setTitle") ||
           event == QStringLiteral("setState") || event == QStringLiteral("setBG") ||
           event == QStringLiteral("setFeedback") || event == QStringLiteral("setText");
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
        // setState changes the action state index (triggers a different image/title).
        // Phase 19: resolve the key index but defer full multi-state rendering to
        // Phase 23 UI binding. Log + no-op for the paint step.
        AJAZZ_LOG_INFO("plugin-bridge",
                       "setState: deferred to Phase 23 multi-state rendering for context '{}'",
                       contextId.toStdString());
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
}

void PluginDeviceBridge::onSetTitle(QString const& /*pluginUuid*/,
                                    QJsonObject const& action,
                                    ActionContext const& ctx,
                                    std::uint8_t keyCols) {
    QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
    [[maybe_unused]] QString const title = payload.value(QStringLiteral("title")).toString();

    // Phase 19: render the key with a solid neutral background to acknowledge the
    // setTitle command. Full text rendering (QPainter overlay) is deferred to
    // Phase 23 where a real GUI context is guaranteed; this avoids QPainter on
    // a QCoreApplication environment (no screen backend, potential SIGABRT).
    // The title string is acknowledged-but-deferred for text rendering (noted in SUMMARY).
    constexpr int kKeySize = 85;
    QImage img(kKeySize, kKeySize, QImage::Format_RGBA8888);
    img.fill(QColor(30, 30, 30)); // dark neutral background (title visible in Phase 23)

    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
    try {
        m_control->assignKeyImage(keyIndex, img);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("plugin-bridge",
                       "onSetTitle: assignKeyImage threw for key {}: {}",
                       static_cast<int>(keyIndex),
                       e.what());
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
        };
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("dialUp"), payload);
        m_server->sendEvent(ctx.pluginUuid, QStringLiteral("keyUpCord"), payload);
        break;
    }

    // ------------------------------------------------------------------
    // Touch strip -> touchTap for gesture 0 (tap); swipes dropped
    // ------------------------------------------------------------------
    case Kind::TouchStrip: {
        // Touch value packing: (gesture<<16)|X (same as stream_dock_input_service.cpp:219).
        auto const gesture = static_cast<std::uint32_t>(ev.value) >> 16u;
        auto const x = static_cast<int>(static_cast<std::uint32_t>(ev.value) & 0xFFFFu);

        // Only gesture 0 (tap) is a plugin event.
        // Gestures 1 (swipe-left) and 2 (swipe-right) are page-nav intents
        // owned by Phase 16 — dropped here (not plugin events).
        if (gesture != 0u) {
            return;
        }

        // Derive the encoder zone from X position (mirrors zoneForX in input service).
        // The zone index is the 0-based encoder index; look up the encoder context.
        // PROVISIONAL zone map (akp05.md §5) — hardware-reconciled in Phase 25.
        constexpr int kEncoderCount = 4;
        constexpr int kTouchStripRangeX = 640;
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

            // Send willAppear (§4.4) — carries context + coordinates.
            QJsonObject const coords{
                {QStringLiteral("row"), gc.row},
                {QStringLiteral("column"), gc.column},
            };
            QJsonObject const payload{
                {QStringLiteral("context"), ctxId},
                {QStringLiteral("coordinates"), coords},
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

            QJsonObject const coords{
                {QStringLiteral("row"), 0},
                {QStringLiteral("column"), static_cast<int>(encIdx)},
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
