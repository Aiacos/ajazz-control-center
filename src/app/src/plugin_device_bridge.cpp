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
#include "encoder_layout_renderer.hpp"
#include "plugin_settings_store.hpp"
#include "sd_plugin_server.hpp"
#include "stream_dock_control_service.hpp"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QPolygon>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <QUrl>

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
    // ONLY for the same action, though — the context id is coordinate-based, so
    // an in-place rebind (a different action dropped on the same key) reuses the
    // id; inheriting the OLD action's stateIndex would start the new action in
    // an arbitrary state (review finding 2026-06-10).
    auto const it = m_byContext.constFind(ctxId);
    if (it != m_byContext.constEnd() && it->actionUUID == stored.actionUUID) {
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

bool ContextRegistry::updateSettings(QString const& context, QString const& settingsJson) {
    auto it = m_byContext.find(context);
    if (it == m_byContext.end()) {
        return false; // stale / unknown context
    }
    it->settingsJson = settingsJson;
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

    // Find the first comma: everything after it is the body.
    // If no comma is present, treat the whole string as a base64 body
    // (tolerates a raw body without the "data:" prefix).
    qsizetype const commaPos = dataUri.indexOf(QLatin1Char(','));
    QString const header = (commaPos >= 0) ? dataUri.left(commaPos) : QString{};
    QString const bodyStr = (commaPos >= 0) ? dataUri.mid(commaPos + 1) : dataUri;

    if (bodyStr.isEmpty()) {
        return {false, {}};
    }

    // T-19-img: cap body length before any allocation (ARCH-04 / security
    // checklist). 512 KB of base64 encodes at most ~384 KB of raw bytes, which is
    // comfortably above any real key icon (85x85 RGBA = 28,900 bytes). A malicious
    // plugin on the loopback interface cannot allocate more than this via a data: URI.
    constexpr qsizetype kMaxBase64Bytes = 512 * 1024; // 512 KB
    if (bodyStr.size() > kMaxBase64Bytes) {
        return {false, {}};
    }

    // Elgato setImage also accepts a NON-base64 body — the documented SVG form is
    // `data:image/svg+xml;charset=utf8,<svg …>` (plain or percent-encoded text;
    // MiraBox SDVueSDK plugins emit exactly this). A header without ";base64"
    // therefore carries the body verbatim: percent-decode and hand it to
    // loadFromData (the qsvg imageformat plugin renders it).
    bool const isBase64 =
        header.isEmpty() || header.contains(QLatin1String(";base64"), Qt::CaseInsensitive);

    // Base64 path: fromBase64 with the default IgnoreBase64DecodingErrors flag
    // silently ignores non-base64 characters rather than returning empty bytes.
    // An empty result indicates an all-whitespace or zero-length input.
    // loadFromData is the actual rejection gate for any garbage payload.
    QByteArray const raw = isBase64 ? QByteArray::fromBase64(bodyStr.toUtf8())
                                    : QByteArray::fromPercentEncoding(bodyStr.toUtf8());
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

QString mirrorSafeDataUri(QString const& dataUri) {
    qsizetype const commaPos = dataUri.indexOf(QLatin1Char(','));
    if (commaPos < 0) {
        return dataUri; // raw base64 body without a header — nothing to escape
    }
    QString const header = dataUri.left(commaPos);
    if (header.contains(QLatin1String(";base64"), Qt::CaseInsensitive)) {
        return dataUri; // base64 alphabet never collides with URI delimiters
    }
    // Non-base64 body (the Elgato SVG form): raw '#' (e.g. stroke:#cccccc) is a
    // fragment delimiter, so Chromium truncates the URI there and the SPA canvas
    // falls back to its alert placeholder. Percent-decode first (tolerates an
    // already-encoded body), then re-encode the whole body.
    QByteArray body = QByteArray::fromPercentEncoding(dataUri.mid(commaPos + 1).toUtf8());

    // Chromium (unlike Qt SVG) REJECTS a standalone image/svg+xml document whose
    // root tag lacks the SVG namespace — and MiraBox plugins emit exactly that
    // (`<svg height="128" …>`, no xmlns; live-verified 2026-07-02: Image() onerror
    // without, onload with). Inject it into the root tag when missing.
    QByteArray const trimmed = body.trimmed();
    if (trimmed.startsWith("<svg")) {
        qsizetype const svgPos = body.indexOf("<svg");
        qsizetype const tagEnd = body.indexOf('>', svgPos);
        // Probe for the DEFAULT namespace declaration specifically: a bare
        // "xmlns" also matches xmlns:xlink and would skip the injection
        // Chromium still needs (audit 6.11).
        if (tagEnd > svgPos && !body.mid(svgPos, tagEnd - svgPos).contains("xmlns=")) {
            body.insert(svgPos + 4, " xmlns=\"http://www.w3.org/2000/svg\"");
        }
    }
    return header + QLatin1Char(',') + QString::fromUtf8(body.toPercentEncoding());
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
// Pure helpers: Elgato/OpenDeck event envelope construction
// ---------------------------------------------------------------------------

namespace {

/// Build the GenericInstancePayload (Elgato §4.4 / OpenDeck GenericInstancePayload):
/// `{settings, coordinates:{row,column}, controller, state, isInMultiAction}`.
/// settings is parsed from ctx.settingsJson (empty/invalid => {}).
QJsonObject instancePayload(ActionContext const& ctx, bool isInMultiAction = false) {
    QJsonObject settings;
    if (!ctx.settingsJson.isEmpty()) {
        QJsonParseError perr{};
        auto const doc = QJsonDocument::fromJson(ctx.settingsJson.toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            settings = doc.object();
        }
    }
    return QJsonObject{
        {QStringLiteral("settings"), settings},
        {QStringLiteral("coordinates"),
         QJsonObject{{QStringLiteral("row"), ctx.row}, {QStringLiteral("column"), ctx.column}}},
        {QStringLiteral("controller"), ctx.controller},
        {QStringLiteral("state"), ctx.stateIndex},
        {QStringLiteral("isInMultiAction"), isInMultiAction},
    };
}

/// Build the SDK-2 `titleParametersDidChange` payload (Elgato §titleParametersDidChange):
/// `{settings, coordinates:{row,column}, controller, state, title, titleParameters}`.
/// Mirrors instancePayload() for the shared instance keys (settings/coordinates/
/// controller/state) and ADDS the title surface a plugin renders from at appear time.
///
/// `title` is the user-set label carried on ctx.title (sourced from the binding's
/// current state at populate time — audit 6.8; "" when unset is a valid SDK-2
/// value). The titleParameters defaults are the SDK-2 shape — [ASSUMED] exact
/// values (research A1/A2); the Catch2 completeness test LOCKS the chosen shape
/// and the end-of-phase human-verify confirms the values if a real plugin reads
/// them.
QJsonObject titlePayload(ActionContext const& ctx) {
    QJsonObject settings;
    if (!ctx.settingsJson.isEmpty()) {
        QJsonParseError perr{};
        auto const doc = QJsonDocument::fromJson(ctx.settingsJson.toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            settings = doc.object();
        }
    }
    return QJsonObject{
        {QStringLiteral("settings"), settings},
        {QStringLiteral("coordinates"),
         QJsonObject{{QStringLiteral("row"), ctx.row}, {QStringLiteral("column"), ctx.column}}},
        {QStringLiteral("controller"), ctx.controller},
        {QStringLiteral("state"), ctx.stateIndex},
        {QStringLiteral("title"), ctx.title},
        {QStringLiteral("titleParameters"),
         QJsonObject{
             {QStringLiteral("fontFamily"), QString{}},
             {QStringLiteral("fontSize"), 12},
             {QStringLiteral("fontStyle"), QString{}},
             {QStringLiteral("fontUnderline"), false},
             {QStringLiteral("showTitle"), true},
             {QStringLiteral("titleAlignment"), QStringLiteral("middle")},
             {QStringLiteral("titleColor"), QStringLiteral("#ffffff")},
         }},
    };
}

/// Build the full Elgato/OpenDeck event envelope with top-level action/context/
/// device siblings to event (the shape real Elgato SDK plugins parse). The
/// context id is reconstructed from ctx so a byCoord-resolved context needs no
/// second registry lookup.
QJsonObject
eventEnvelope(QString const& event, ActionContext const& ctx, QJsonObject const& payload) {
    return QJsonObject{
        {QStringLiteral("event"), event},
        {QStringLiteral("action"), ctx.actionUUID},
        {QStringLiteral("context"), ContextRegistry::deriveContextId(ctx)},
        {QStringLiteral("device"), ctx.deviceId},
        {QStringLiteral("payload"), payload},
    };
}

/// Resolve the settings JSON for a context at willAppear time: the persisted
/// store record (set by the plugin or its PI) wins over the binding default,
/// which wins over the manifest action's declared `Settings` defaults (the
/// vendor StreamDock host seeds a NEW instance with those; MiraBox SDVueSDK
/// draw code dereferences the default keys unguarded, so a first-run instance
/// appearing with {} throws before its first setImage — timeClock 2026-07-02).
QString settingsForContext(QString const& pluginUuid,
                           QString const& contextId,
                           QString const& bindingDefault,
                           QString const& manifestDefault = {}) {
    // audit 6.10: an EXPLICIT stored "{}" is an intentional clear (a plugin/PI
    // setSettings({})) and must win over the defaults — readContext returns
    // "" (not "{}") when no record exists, so the two cases are distinct.
    QString const stored = plugin_settings_store::readContext(pluginUuid, contextId);
    if (!stored.isEmpty()) {
        return stored;
    }
    if (!bindingDefault.isEmpty() && bindingDefault != QStringLiteral("{}")) {
        return bindingDefault;
    }
    return manifestDefault.isEmpty() ? bindingDefault : manifestDefault;
}

} // namespace

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
           // F4: `setBackground` is the documented vendor alias for `setBG`
           // (sd_plugin_server.cpp routes both); `clearIcon` resets a key to blank.
           event == QStringLiteral("setBackground") || event == QStringLiteral("clearIcon") ||
           event == QStringLiteral("setFeedback") || event == QStringLiteral("setFeedbackLayout") ||
           event == QStringLiteral("setText") || event == QStringLiteral("showAlert") ||
           event == QStringLiteral("showOk");
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

bool PluginDeviceBridge::handleSettingsAction(QString const& pluginUuid,
                                              QString const& event,
                                              QJsonObject const& action) {
    if (m_server == nullptr) {
        return false;
    }

    // --- Plugin-wide (global) settings: keyed by the OWNING plugin uuid. ---
    // A WS Property Inspector registers with its instance CONTEXT string as
    // its uuid; keying the store on the raw sender uuid parked PI-saved
    // globals in a context-named bucket the plugin never reads (audit 2.2).
    // Resolve the effective owner: sender-is-PI => the context's plugin.
    if (event == QStringLiteral("setGlobalSettings") ||
        event == QStringLiteral("getGlobalSettings")) {
        QString globalOwner = pluginUuid;
        bool senderIsPi = false;
        if (auto const senderCtx = lookupContext(pluginUuid);
            senderCtx.has_value() && !senderCtx->pluginUuid.isEmpty()) {
            globalOwner = senderCtx->pluginUuid;
            senderIsPi = (globalOwner != pluginUuid);
        }
        if (event == QStringLiteral("setGlobalSettings")) {
            QJsonObject const settings = action.value(QStringLiteral("payload")).toObject();
            QString const json =
                QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
            if (!plugin_settings_store::writeGlobal(globalOwner, json)) {
                AJAZZ_LOG_WARN("plugin-bridge",
                               "setGlobalSettings: write rejected for '{}'",
                               globalOwner.toStdString());
                return true;
            }
            // Spec §5.2 / settings.rs: notify the OPPOSITE party. Sender is the
            // plugin -> all of its open PIs; sender is a PI -> the plugin
            // (audit 2.3 — nothing was ever notified).
            QJsonObject const note{
                {QStringLiteral("event"), QStringLiteral("didReceiveGlobalSettings")},
                {QStringLiteral("payload"), QJsonObject{{QStringLiteral("settings"), settings}}}};
            if (senderIsPi) {
                m_server->sendEvent(globalOwner, note);
            } else {
                for (QString const& piUuid : m_server->propertyInspectorUuids()) {
                    auto const piCtx = lookupContext(piUuid);
                    if (piCtx.has_value() && piCtx->pluginUuid == globalOwner) {
                        m_server->sendEvent(piUuid, note);
                    }
                }
            }
            return true;
        }
        // getGlobalSettings: reply to the REQUESTER (spec §4 — the reply goes
        // back on the requesting connection, PI or plugin). Echo the optional
        // `id` correlator (audit 2.5 — the modern SDK promise helper keys on it).
        QString const stored = plugin_settings_store::readGlobal(globalOwner);
        QJsonObject const settings = QJsonDocument::fromJson(stored.toUtf8()).object();
        QJsonObject reply{
            {QStringLiteral("event"), QStringLiteral("didReceiveGlobalSettings")},
            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("settings"), settings}}}};
        if (action.contains(QStringLiteral("id"))) {
            reply.insert(QStringLiteral("id"), action.value(QStringLiteral("id")));
        }
        m_server->sendEvent(pluginUuid, reply);
        return true;
    }

    // --- Per-context (per-action-instance) settings: keyed by the wire context. ---
    if (event == QStringLiteral("setSettings") || event == QStringLiteral("getSettings") ||
        event == QStringLiteral("sendToPropertyInspector")) {
        QString const contextId = action.value(QStringLiteral("context")).toString();
        // Resolve the context accepting both the wire id and the SPA dot form (a
        // Property Inspector addresses its instance by the SPA context). Without
        // this, every PI setSettings/getSettings missed the registry and was
        // dropped, so dropped actions never received their configured settings.
        auto const ctxOpt = lookupContext(contextId);
        if (!ctxOpt.has_value()) {
            return true; // consumed: stale/unknown context — drop quietly
        }
        ActionContext ctx = *ctxOpt;
        // Authorisation: the sender is allowed if it is the owning plugin, OR the
        // Property Inspector for THIS context (a PI registers with the instance
        // context string as its uuid, so it resolves to the same coordinates).
        bool authorised = (ctx.pluginUuid == pluginUuid);
        if (!authorised) {
            auto const senderCtx = lookupContext(pluginUuid);
            authorised = senderCtx.has_value() && ContextRegistry::deriveContextId(*senderCtx) ==
                                                      ContextRegistry::deriveContextId(ctx);
        }
        if (!authorised) {
            return true; // T-19-xplugin: cross-plugin denial
        }
        // Storage + relay key on the REAL owner + canonical wire id (the sender
        // may be a PI whose uuid/context is the SPA form), so willAppear's
        // settingsForContext(owner, wireId) reads back what the PI just wrote.
        QString const owner = ctx.pluginUuid.isEmpty() ? pluginUuid : ctx.pluginUuid;
        QString const wireId = ContextRegistry::deriveContextId(ctx);

        if (event == QStringLiteral("sendToPropertyInspector")) {
            // Relay to the open Property Inspector (Application wires this to the
            // active PIBridge). The bridge does not own the PI surface.
            emit relayToPropertyInspector(
                owner, contextId, action.value(QStringLiteral("payload")).toObject());
            return true;
        }

        // Sender identity: the second authorisation branch fired iff the sender
        // uuid resolved to this context but is not the owning plugin — i.e. the
        // instance's Property Inspector.
        bool const senderIsPi = (owner != pluginUuid);

        if (event == QStringLiteral("setSettings")) {
            QJsonObject const settings = action.value(QStringLiteral("payload")).toObject();
            QString const json =
                QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
            bool const changed = (ctx.settingsJson != json);
            if (plugin_settings_store::writeContext(owner, wireId, json)) {
                ctx.settingsJson = json;
                m_registry.updateSettings(wireId, json); // keep keyDown/willAppear fresh
                if (changed) {
                    // Mirror into the profile binding only on a REAL change —
                    // a plugin re-asserting identical settings must not cost a
                    // profile save + repaint per call.
                    emitBindingSettingsPersisted(ctx, json);
                }
            }
            // Spec / settings.rs: setSettings notifies the OPPOSITE party — a
            // self-echo made plugins that setSettings inside their
            // didReceiveSettings handler loop forever (audit 2.1).
            if (senderIsPi) {
                m_server->sendEvent(
                    owner,
                    eventEnvelope(QStringLiteral("didReceiveSettings"), ctx, instancePayload(ctx)));
            } else {
                for (QString const& piUuid : m_server->propertyInspectorUuids()) {
                    auto const piCtx = lookupContext(piUuid);
                    if (piCtx.has_value() && ContextRegistry::deriveContextId(*piCtx) == wireId) {
                        m_server->sendEvent(piUuid,
                                            eventEnvelope(QStringLiteral("didReceiveSettings"),
                                                          ctx,
                                                          instancePayload(ctx)));
                    }
                }
            }
            return true;
        }
        // getSettings: reflect the persisted record (falls back to the in-ctx
        // value) and reply to the REQUESTER — the reply used to go to the
        // plugin even when the PI asked, leaving PI forms blank (audit 2.1).
        // audit 6.10: a persisted "{}" is an intentional clear and is
        // reflected as-is; only a MISSING record ("") keeps the in-ctx value.
        QString const stored = plugin_settings_store::readContext(owner, wireId);
        if (!stored.isEmpty()) {
            ctx.settingsJson = stored;
        }
        QJsonObject reply =
            eventEnvelope(QStringLiteral("didReceiveSettings"), ctx, instancePayload(ctx));
        if (action.contains(QStringLiteral("id"))) {
            reply.insert(QStringLiteral("id"),
                         action.value(QStringLiteral("id"))); // audit 2.5 correlator
        }
        m_server->sendEvent(pluginUuid, reply);
        return true;
    }

    return false; // not a settings/relay event
}

void PluginDeviceBridge::onAction(QString const& pluginUuid, QJsonObject const& action) {
    // T-19-input: read defensively — toString returns "" on missing/wrong type.
    QString const event = action.value(QStringLiteral("event")).toString();

    // Settings + PI-relay family (non-visual). Handled before the visual gate so
    // setSettings/getSettings/global/sendToPropertyInspector are no longer dropped.
    if (handleSettingsAction(pluginUuid, event, action)) {
        return;
    }

    // T025/D3: setTriggerDescription (SD+ dial hints) — non-visual. Enforce
    // context ownership like a visual action, then surface the hints via a
    // signal for the encoder UI. No key repaint, so it sits before the visual
    // gate (which would otherwise drop it).
    if (event == QStringLiteral("setTriggerDescription")) {
        QString const contextId = action.value(QStringLiteral("context")).toString();
        auto const ctxOpt = m_registry.byContext(contextId);
        if (!ctxOpt.has_value() || ctxOpt->pluginUuid != pluginUuid) {
            return; // stale/unknown context or cross-plugin denial (no emit)
        }
        emit triggerDescriptionChanged(
            ctxOpt->deviceId, contextId, action.value(QStringLiteral("payload")).toObject());
        return;
    }

    // OpenDeck `deviceBrightness` extension (audit 4.9): {action, value} at the
    // envelope top level (inbound/misc.rs DeviceBrightnessEvent). Mirror-only:
    // the SPA adjusts its settings slider and the hardware write follows via
    // the settings round-trip, exactly like upstream.
    if (event == QStringLiteral("deviceBrightness")) {
        QString brightnessAction = action.value(QStringLiteral("action")).toString();
        int const value = action.value(QStringLiteral("value")).toInt(0);
        if (brightnessAction.isEmpty()) {
            brightnessAction = QStringLiteral("set");
        }
        emit deviceBrightnessRequested(brightnessAction, value);
        return;
    }

    if (!isVisualAction(event)) {
        return; // no-op for other non-visual actions (handled in later phases)
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

    // audit 4.9: mirror transient feedback to the SPA canvas (Key.svelte
    // show_alert/show_ok) BEFORE the control-service guard, so the on-screen
    // key flashes even with no physical device attached.
    if (event == QStringLiteral("showAlert") || event == QStringLiteral("showOk")) {
        auto const geo = geometryForDevice(ctx.deviceId);
        QString spaController = ctx.controller;
        int spaPos = 0;
        if (ctx.controller == QLatin1String("Encoder") && ctx.row == 1) {
            // audit 6.2/6.3: a touch-zone context (row 1) maps to the SPA
            // "Keypad" slot appended after the key grid.
            int const keyCount = geo.keyCount > 0 ? geo.keyCount : geo.keyCols * geo.keyRows;
            spaController = QStringLiteral("Keypad");
            spaPos = keyCount + ctx.column;
        } else if (ctx.controller == QLatin1String("Encoder")) {
            spaPos = ctx.column;
        } else {
            spaPos = ctx.row * geo.keyCols + ctx.column;
        }
        emit instanceFeedback(
            ctx.deviceId, spaController, spaPos, event == QStringLiteral("showOk"));
    }

    // Guard: if m_control is null (test shim without a real control service) we
    // cannot drive a device, so skip — but still do NOT crash.
    if (m_control == nullptr) {
        return;
    }

    // Source keyCols from the context's own device via the injected geometry
    // resolver (F2): converts 0-based Elgato {row,column} to a 1-based keyIndex
    // for the device the context is actually bound to, not a hardcoded AKP05E
    // 5×2. Unset resolver / unknown codename => AKP05E default (prior behaviour).
    std::uint8_t const keyCols = geometryForDevice(ctx.deviceId).keyCols;

    if (event == QStringLiteral("setImage")) {
        onSetImage(pluginUuid, action, ctx, keyCols);
    } else if (event == QStringLiteral("setTitle")) {
        onSetTitle(pluginUuid, action, ctx, keyCols);
    } else if (event == QStringLiteral("setBG") || event == QStringLiteral("setBackground")) {
        // F4: `setBackground` is the vendor alias for `setBG` — same {color}
        // payload, same fill behaviour. Previously `setBackground` was routed by
        // the server but never matched here, so it was a silent no-op.
        onSetBG(pluginUuid, action, ctx, keyCols);
    } else if (event == QStringLiteral("clearIcon")) {
        // F4: reset the key to a blank surface (vendor extension; OpenDeck has no
        // analogue). Paint a solid black key and drop any cached title overlay so
        // the cleared key does not keep a stale label on the next reapplyTitle.
        if (ctx.controller == QStringLiteral("Keypad")) {
            constexpr int kKeySize = 85;
            QImage blank(kKeySize, kKeySize, QImage::Format_RGBA8888);
            blank.fill(QColor(0, 0, 0));
            std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
            m_titleByKey.erase(keyIndex);
            try {
                m_control->assignKeyImage(keyIndex, blank);
            } catch (std::exception const& e) {
                AJAZZ_LOG_WARN("plugin-bridge",
                               "clearIcon: assignKeyImage threw for key {}: {}",
                               static_cast<int>(keyIndex),
                               e.what());
            }
        }
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
        // or the action declares no image for this state. A follow-up
        // titleParametersDidChange carries the new state (OpenDeck sends one
        // after every state change — states.rs:38).
        if (ok) {
            ActionContext painted = ctx;
            painted.stateIndex = newState;
            paintDeclaredStateImage(painted, keyCols);
            m_server->sendEvent(painted.pluginUuid,
                                eventEnvelope(QStringLiteral("titleParametersDidChange"),
                                              painted,
                                              titlePayload(painted)));
        }
    } else if (event == QStringLiteral("showAlert") || event == QStringLiteral("showOk")) {
        // Transient feedback flash (Elgato): paint a warning/ok glyph, then
        // revert to the cached key image after a short dwell. Keypad only.
        if (ctx.controller == QStringLiteral("Keypad")) {
            bool const okGlyph = (event == QStringLiteral("showOk"));
            std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
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
    } else if (event == QStringLiteral("setFeedback")) {
        // Merge the item bag into the per-context feedback state, then re-render
        // the active layout (Elgato Dials guide: setFeedback updates items of
        // the CURRENT layout; unknown keys are ignored by the renderer).
        QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
        QJsonObject& bag = m_encoderFeedback[contextId];
        for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
            bag.insert(it.key(), it.value());
        }
        renderEncoderFeedback(ctx);
    } else if (event == QStringLiteral("setFeedbackLayout")) {
        // Switch the built-in layout for this context; feedback items persist
        // across the switch (Elgato behaviour) and re-apply on the new layout.
        QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
        QString const layout = payload.value(QStringLiteral("layout")).toString();
        if (!layout.isEmpty()) {
            m_encoderLayoutOverride[contextId] = layout;
        }
        renderEncoderFeedback(ctx);
    } else if (event == QStringLiteral("setText")) {
        // AJAZZ legacy alias: treat the text as the layout's title item.
        QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
        m_encoderFeedback[contextId].insert(QStringLiteral("title"),
                                            payload.value(QStringLiteral("text")));
        renderEncoderFeedback(ctx);
    }
}

void PluginDeviceBridge::onSetImage(QString const& /*pluginUuid*/,
                                    QJsonObject const& action,
                                    ActionContext const& ctx,
                                    std::uint8_t keyCols) {
    // T-19-input: read payload keys defensively.
    QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
    QString const dataUri = payload.value(QStringLiteral("image")).toString();
    // Elgato semantics (production audit blocker 3, 2026-07-03):
    // - `state`: the image belongs to THAT state only. Stash it in the
    //   per-state override map; paint only when it is the context's CURRENT
    //   state (state changes repaint from the stash via
    //   paintDeclaredStateImage's override lookup). A stateless setImage
    //   clears the stash — the image then covers every state.
    // - `target`: 0=hw+sw (default), 1=hw-only, 2=sw-only. Gate the hardware
    //   paint on target != 2 and the SPA mirror on target != 1.
    int const target = payload.value(QStringLiteral("target")).toInt(0);
    QString const ctxId = ContextRegistry::deriveContextId(ctx);
    if (payload.contains(QStringLiteral("state"))) {
        int const forState = payload.value(QStringLiteral("state")).toInt(0);
        m_stateImageOverride[ctxId][forState] = dataUri;
        if (forState != ctx.stateIndex) {
            return; // stashed for a non-live state — nothing to paint now
        }
    } else {
        m_stateImageOverride.erase(ctxId);
    }

    // Decode the data-URI (T-19-img: returns {ok:false} on any failure — no crash).
    DecodedImage const decoded = decodeDataUriImage(dataUri);
    if (!decoded.ok) {
        // Spec §5: paint crash-free placeholder, send NO failure event back.
        paintPlaceholder(ctx, keyCols);
        return;
    }

    // Encoder context: setImage paints the dial's touch-strip zone (vendor
    // extension mirroring the SPA's update_image Encoder routing). openaction
    // plugins have no set_feedback API as of v2.6, so a self-rendered strip
    // tile via setImage is their only feedback channel; ctx.column is the
    // 0-based encoder index. The keyIndexForCoords math below is keypad-only —
    // running it for an encoder would paint a wrong key.
    if (ctx.controller == QLatin1String("Encoder")) {
        if (target != 2) {
            try {
                m_control->assignEncoderImage(static_cast<std::uint8_t>(ctx.column), decoded.image);
            } catch (std::exception const& e) {
                AJAZZ_LOG_WARN("plugin-bridge",
                               "onSetImage: assignEncoderImage threw for encoder {}: {}",
                               ctx.column,
                               e.what());
            }
        }
        if (target == 1) {
            return; // hw-only: skip the SPA mirror below
        }
        // audit 6.3: a touch-zone context (row 1) mirrors to its SPA slot —
        // the "Keypad" position appended after the key grid — NOT the dial
        // slot, which it used to overwrite.
        if (ctx.row == 1) {
            auto const geo = geometryForDevice(ctx.deviceId);
            int const keyCount = geo.keyCount > 0 ? geo.keyCount : geo.keyCols * geo.keyRows;
            emit liveInstanceVisual(ctx.deviceId,
                                    QStringLiteral("Keypad"),
                                    keyCount + ctx.column,
                                    mirrorSafeDataUri(dataUri),
                                    QString(),
                                    false);
        } else {
            emit liveInstanceVisual(ctx.deviceId,
                                    QStringLiteral("Encoder"),
                                    ctx.column,
                                    mirrorSafeDataUri(dataUri),
                                    QString(),
                                    false);
        }
        return;
    }

    // Compute the 1-based device keyIndex from the 0-based Elgato coordinates.
    // keyIndexForCoords: Pitfall 2 — single named converter from 0-based to 1-based.
    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);

    if (target != 2) {
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
    if (target == 1) {
        return; // hw-only: skip the SPA mirror
    }

    // Mirror the live frame into the OpenDeck web UI (update_state). Emitted
    // after the device paint so the SPA never runs ahead of the hardware.
    emit liveInstanceVisual(ctx.deviceId,
                            QStringLiteral("Keypad"),
                            ctx.row * keyCols + ctx.column,
                            mirrorSafeDataUri(dataUri),
                            QString(),
                            false);
}

void PluginDeviceBridge::onSetTitle(QString const& /*pluginUuid*/,
                                    QJsonObject const& action,
                                    ActionContext const& ctx,
                                    std::uint8_t keyCols) {
    QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
    QString const title = payload.value(QStringLiteral("title")).toString();
    // Same Elgato `state`/`target` semantics as onSetImage (audit blocker 3):
    // a per-state title is stashed and applied only when its state is live;
    // target 2 skips the hardware composite, target 1 skips the SPA mirror.
    int const target = payload.value(QStringLiteral("target")).toInt(0);
    QString const ctxId = ContextRegistry::deriveContextId(ctx);
    if (payload.contains(QStringLiteral("state"))) {
        int const forState = payload.value(QStringLiteral("state")).toInt(0);
        if (title.isEmpty()) {
            m_stateTitleOverride[ctxId].erase(forState);
        } else {
            m_stateTitleOverride[ctxId][forState] = title;
        }
        if (forState != ctx.stateIndex) {
            return; // stashed for a non-live state
        }
    } else {
        m_stateTitleOverride.erase(ctxId);
    }

    // Encoder context: there is no per-key title layer for strip zones — the
    // feedback layout (or the plugin's own strip tile) owns all text. Mirror
    // the title to the SPA slot and stop; the keypad keyIndex/composite math
    // below would target a wrong key.
    if (ctx.controller == QLatin1String("Encoder")) {
        if (target == 1) {
            return; // hw-only: the encoder title path is mirror-only
        }
        // audit 6.3: touch-zone titles (row 1) mirror to the SPA touch slot.
        if (ctx.row == 1) {
            auto const geo = geometryForDevice(ctx.deviceId);
            int const keyCount = geo.keyCount > 0 ? geo.keyCount : geo.keyCols * geo.keyRows;
            emit liveInstanceVisual(ctx.deviceId,
                                    QStringLiteral("Keypad"),
                                    keyCount + ctx.column,
                                    QString(),
                                    title,
                                    true);
        } else {
            emit liveInstanceVisual(
                ctx.deviceId, QStringLiteral("Encoder"), ctx.column, QString(), title, true);
        }
        return;
    }

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

    if (target != 2) {
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
    if (target == 1) {
        return; // hw-only: skip the SPA mirror
    }

    // Mirror the title change into the OpenDeck web UI (update_state).
    emit liveInstanceVisual(ctx.deviceId,
                            QStringLiteral("Keypad"),
                            ctx.row * keyCols + ctx.column,
                            QString(),
                            title,
                            true);
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

    // Encoder/touch context: same guard as onSetImage/onSetTitle — the keypad
    // index math would paint the wrong physical key (review find 2026-07-02).
    if (ctx.controller == QLatin1String("Encoder")) {
        try {
            m_control->assignEncoderImage(static_cast<std::uint8_t>(ctx.column), img);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("plugin-bridge",
                           "onSetBG: assignEncoderImage threw for zone {}: {}",
                           ctx.column,
                           e.what());
        }
        return;
    }

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

    // Encoder/touch context: the keypad row/column math below would target a
    // WRONG physical key (row 1 touch -> bottom key row). Paint the strip
    // zone instead (review find 2026-07-02).
    if (ctx.controller == QLatin1String("Encoder")) {
        try {
            m_control->assignEncoderImage(static_cast<std::uint8_t>(ctx.column), placeholder);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("plugin-bridge",
                           "paintPlaceholder: assignEncoderImage threw for zone {}: {}",
                           ctx.column,
                           e.what());
        }
        return;
    }

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

void PluginDeviceBridge::setActionOwnerResolver(std::function<QString(QString const&)> resolver) {
    m_actionOwnerResolver = std::move(resolver);
}

void PluginDeviceBridge::setDefaultSettingsResolver(
    std::function<QString(QString const&)> resolver) {
    m_defaultSettingsResolver = std::move(resolver);
}

void PluginDeviceBridge::setDeviceGeometryResolver(
    std::function<DeviceGeometry(QString const&)> resolver) {
    m_deviceGeometryResolver = std::move(resolver);
}

DeviceGeometry PluginDeviceBridge::geometryForDevice(QString const& deviceId) const {
    // No resolver wired (unit fixtures) or an empty id => the AKP05E default,
    // which is the geometry the bridge hardcoded before F2. A resolver that does
    // not know the codename also returns its own default for the same reason.
    if (!m_deviceGeometryResolver || deviceId.isEmpty()) {
        return DeviceGeometry{};
    }
    return m_deviceGeometryResolver(deviceId);
}

QJsonObject PluginDeviceBridge::deviceInfoFor(QString const& deviceId) const {
    // Canonical Elgato deviceInfo (§4.4). `size` is the action-slot grid; `type`
    // is the Elgato DeviceType (SD+ = 7). Sourced per-device from the geometry
    // resolver (F2). Reused by deviceDidConnect AND passHello.deviceInfo (B7).
    DeviceGeometry const geom = geometryForDevice(deviceId);
    return QJsonObject{
        {QStringLiteral("name"), geom.model.isEmpty() ? deviceId : geom.model},
        {QStringLiteral("type"), geom.elgatoType},
        {QStringLiteral("size"),
         QJsonObject{
             {QStringLiteral("columns"), geom.keyCols},
             {QStringLiteral("rows"), geom.keyRows},
         }},
        {QStringLiteral("columns"), geom.keyCols},
        {QStringLiteral("rows"), geom.keyRows},
        {QStringLiteral("encoders"), geom.encoderCount},
    };
}

void PluginDeviceBridge::setActionStateMetaResolver(
    std::function<std::pair<int, bool>(QString const&)> resolver) {
    m_actionStateMetaResolver = std::move(resolver);
}

void PluginDeviceBridge::setEncoderLayoutResolver(
    std::function<std::pair<QString, QString>(QString const&)> resolver) {
    m_encoderLayoutResolver = std::move(resolver);
}

void PluginDeviceBridge::renderEncoderFeedback(ActionContext const& ctx) {
    if (m_control == nullptr || ctx.controller != QStringLiteral("Encoder")) {
        return;
    }
    QString const ctxId = ContextRegistry::deriveContextId(ctx);

    // Layout precedence: runtime setFeedbackLayout > manifest Encoder.layout > $X1.
    QString layoutId;
    QString manifestLayout;
    QString manifestIcon;
    if (m_encoderLayoutResolver) {
        auto const [mlayout, micon] = m_encoderLayoutResolver(ctx.actionUUID);
        layoutId = mlayout;
        manifestLayout = mlayout;
        manifestIcon = micon;
    }
    if (auto const it = m_encoderLayoutOverride.find(ctxId); it != m_encoderLayoutOverride.end()) {
        layoutId = it->second;
    }

    // Feedback bag; seed the icon item from the manifest Encoder.Icon when the
    // plugin has not pushed one (mirrors the key path's mount-time default).
    QJsonObject fb;
    if (auto const it = m_encoderFeedback.find(ctxId); it != m_encoderFeedback.end()) {
        fb = it->second;
    }
    if (!fb.contains(QStringLiteral("icon")) && !manifestIcon.isEmpty()) {
        fb.insert(QStringLiteral("icon"), manifestIcon);
    }

    // AKP05 strip zone is square 128x128 (hardware-pinned 2026-05-31); the
    // renderer lays out proportionally so other geometries can be passed later.
    //
    // Custom JSON layouts (production audit blocker 5): a non-"$" layout id is
    // a layout FILE. The manifest path arrives absolute (encoderLayoutInfo
    // resolves it); a setFeedbackLayout override may be plugin-relative and is
    // resolved against the manifest layout/icon directory. Parsed files are
    // cached; a missing/invalid file degrades to the $X1 built-in as before.
    QImage img;
    if (!layoutId.isEmpty() && !layoutId.startsWith(QLatin1Char('$'))) {
        QString path = layoutId;
        if (QFileInfo(path).isRelative()) {
            QString const anchor =
                (!manifestLayout.isEmpty() && !manifestLayout.startsWith(QLatin1Char('$')) &&
                 QFileInfo(manifestLayout).isAbsolute())
                    ? manifestLayout
                    : manifestIcon;
            if (!anchor.isEmpty()) {
                path = QFileInfo(anchor).dir().absoluteFilePath(path);
            }
        }
        auto cacheIt = m_customLayoutCache.find(path);
        if (cacheIt == m_customLayoutCache.end()) {
            QJsonObject def;
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                def = QJsonDocument::fromJson(f.readAll()).object();
            } else {
                AJAZZ_LOG_WARN("plugin-bridge",
                               "renderEncoderFeedback: custom layout '{}' unreadable — "
                               "falling back to $X1",
                               path.toStdString());
            }
            cacheIt = m_customLayoutCache.emplace(path, def).first;
        }
        if (!cacheIt->second.value(QStringLiteral("items")).toArray().isEmpty()) {
            img = renderCustomEncoderLayout(cacheIt->second, fb, QSize(128, 128));
        }
    }
    if (img.isNull()) {
        img = renderEncoderLayout(layoutId, fb, QSize(128, 128));
    }
    try {
        m_control->assignEncoderImage(static_cast<std::uint8_t>(ctx.column), img);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("plugin-bridge",
                       "renderEncoderFeedback: assignEncoderImage threw for zone {}: {}",
                       ctx.column,
                       e.what());
    }
}

void PluginDeviceBridge::paintDeclaredStateImage(ActionContext const& ctx, std::uint8_t keyCols) {
    // Keypad keys and encoder strip-zones both carry a plugin-declared state
    // image; only those two controllers have a renderable surface here. (On the
    // AKP05E the 4 "Encoder" zones ARE the BAT strip displays — same path
    // renderEncoderFeedback() uses.) Other controllers (e.g. bare touch) no-op.
    bool const isKeypad = ctx.controller == QStringLiteral("Keypad");
    bool const isEncoder = ctx.controller == QStringLiteral("Encoder");
    if (m_control == nullptr || (!isKeypad && !isEncoder)) {
        return;
    }
    QString const ctxId = ContextRegistry::deriveContextId(ctx);
    // A per-state title pushed via setTitle with `state` follows the state:
    // update the tracked title layer BEFORE the paint so the reapply below
    // composites the new state's text (audit blocker 3).
    if (isKeypad) {
        auto const tOv = m_stateTitleOverride.find(ctxId);
        if (tOv != m_stateTitleOverride.end()) {
            std::uint8_t const keyIdx = keyIndexForCoords(ctx.row, ctx.column, keyCols);
            auto const tIt = tOv->second.find(ctx.stateIndex);
            if (tIt != tOv->second.end()) {
                m_titleByKey[keyIdx] = tIt->second;
            } else {
                m_titleByKey.erase(keyIdx);
            }
        }
    }
    // Image source precedence: a plugin-pushed per-state image (setImage with
    // `state` — audit blocker 3) outranks the manifest-declared States[].Image.
    QImage stateImg;
    if (auto const iOv = m_stateImageOverride.find(ctxId); iOv != m_stateImageOverride.end()) {
        if (auto const iIt = iOv->second.find(ctx.stateIndex); iIt != iOv->second.end()) {
            DecodedImage const decoded = decodeDataUriImage(iIt->second);
            if (decoded.ok) {
                stateImg = decoded.image;
            }
        }
    }
    if (stateImg.isNull()) {
        if (!m_stateImageResolver) {
            return;
        }
        QString const imgPath = m_stateImageResolver(ctx.actionUUID, ctx.stateIndex);
        if (imgPath.isEmpty()) {
            AJAZZ_LOG_DEBUG("plugin-bridge",
                            "paintDeclaredStateImage: no declared image for action '{}' state {}",
                            ctx.actionUUID.toStdString(),
                            ctx.stateIndex);
            return;
        }
        AJAZZ_LOG_INFO("plugin-bridge",
                       "paintDeclaredStateImage: painting '{}' for action '{}' state {}",
                       imgPath.toStdString(),
                       ctx.actionUUID.toStdString(),
                       ctx.stateIndex);
        stateImg = QImage(imgPath);
        if (stateImg.isNull()) {
            AJAZZ_LOG_WARN("plugin-bridge",
                           "paintDeclaredStateImage: image failed to load: {}",
                           imgPath.toStdString());
            return;
        }
    }
    if (isEncoder) {
        // ctx.column is the 0-based encoder/strip-zone index; encoders carry no
        // title overlay so there is nothing to reapply afterwards.
        try {
            m_control->assignEncoderImage(static_cast<std::uint8_t>(ctx.column), stateImg);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("plugin-bridge",
                           "paintDeclaredStateImage: assignEncoderImage threw for zone {}: {}",
                           static_cast<int>(ctx.column),
                           e.what());
        }
        return;
    }
    std::uint8_t const keyIndex = keyIndexForCoords(ctx.row, ctx.column, keyCols);
    try {
        m_control->assignKeyImage(keyIndex, stateImg);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("plugin-bridge",
                       "paintDeclaredStateImage: assignKeyImage threw for key {}: {}",
                       static_cast<int>(keyIndex),
                       e.what());
        return;
    }
    reapplyTitle(keyIndex); // keep any plugin-set title over the new base image
}

namespace {
/// Synthetic owner uuid for builtin-action contexts (no WS client ever
/// registers with it — sendEvent to it is a silent DEBUG-level no-op).
/// Registering builtin bindings in the context registry is what lets their
/// in-tree Property Inspectors resolve the context for get/setSettings.
constexpr QLatin1StringView kBuiltinOwnerUuid("opendeck.builtin");

/// True for the builtin action namespaces (SPA aliases + canonical ids).
[[nodiscard]] bool isBuiltinActionId(QString const& id) {
    return id.startsWith(QLatin1String("opendeck.")) ||
           id.startsWith(QLatin1String("com.hotspot.streamdock."));
}
} // namespace

QString PluginDeviceBridge::resolveOwner(QString const& actionUuid) const {
    // Stored-owner map first (OpenDeck model): the manifest that declares this
    // action UUID names its owner explicitly, so the action UUID need not be a
    // dotted prefix of the plugin UUID. Fall back to the legacy longest-prefix
    // match (still correct for Elgato-style com.x.plugin / com.x.plugin.action).
    if (m_actionOwnerResolver) {
        QString const owner = m_actionOwnerResolver(actionUuid);
        if (!owner.isEmpty()) {
            return owner;
        }
    }
    QString const owner = ownerForActionUuid(actionUuid, m_registeredPlugins);
    if (!owner.isEmpty()) {
        return owner;
    }
    // Builtin actions have no plugin owner — register them under the synthetic
    // builtin owner so their contexts exist in the registry and the in-tree
    // Property Inspectors can address them (get/setSettings resolution).
    if (isBuiltinActionId(actionUuid)) {
        return QString{kBuiltinOwnerUuid};
    }
    return {};
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
        // Source keyCols from the emitting device's geometry (F2), not a hardcoded
        // AKP05E 5; a 3-column AKP153 key index must map to its own row/column.
        std::uint8_t const keyCols = geometryForDevice(deviceId).keyCols;
        auto const gc = coordsForKeyIndex(static_cast<std::uint8_t>(ev.index), keyCols);
        // CR-02 / WR-04: pass deviceId to byCoord so events from one device cannot
        // route to a plugin context registered for a different device.
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Keypad"), gc.row, gc.column);
        if (!ctxOpt.has_value()) {
            return; // unbound coordinate — silent drop (T-19-leak)
        }
        ActionContext ctx = *ctxOpt;
        bool const isRelease = (ev.kind == Kind::KeyReleased);

        // Automatic state cycle (Elgato/OpenDeck keyUp semantics, keypad.rs:154):
        // an action declaring EXACTLY two states advances state on key RELEASE
        // unless its manifest sets DisableAutomaticStates. The keyUp envelope
        // carries the NEW state, the new state's declared image is auto-
        // rendered, and a titleParametersDidChange follows. Plugins that manage
        // state themselves (setState) either declare one state or set the
        // disable flag — both leave this path inert.
        bool cycled = false;
        if (isRelease && m_actionStateMetaResolver) {
            auto const [stateCount, disableAuto] = m_actionStateMetaResolver(ctx.actionUUID);
            if (stateCount == 2 && !disableAuto) {
                int const nextState = (ctx.stateIndex + 1) % 2;
                QString const ctxId = ContextRegistry::deriveContextId(ctx);
                if (m_registry.setState(ctxId, nextState)) {
                    ctx.stateIndex = nextState;
                    paintDeclaredStateImage(ctx, keyCols);
                    cycled = true;
                }
            }
        }

        QString const eventName = isRelease ? QStringLiteral("keyUp") : QStringLiteral("keyDown");
        // audit 4.9: mirror the press state to the SPA ("key_moved") so the
        // on-screen key renders pressed/released like upstream keypad.rs.
        emit keyPressMirror(
            deviceId, QStringLiteral("Keypad"), gc.row * keyCols + gc.column, !isRelease);
        // Full Elgato envelope: top-level action/context/device + GenericInstancePayload.
        // sendEvent returns false safely if socket is closed (T-19-sock).
        m_server->sendEvent(ctx.pluginUuid, eventEnvelope(eventName, ctx, instancePayload(ctx)));
        if (cycled) {
            // The state just cycled — let the plugin observe the new title
            // parameters/state, mirroring OpenDeck's post-cycle notification.
            // (Single source of truth: no second resolver call, and no
            // notification when setState did not actually advance — review
            // finding 2026-06-10.)
            m_server->sendEvent(
                ctx.pluginUuid,
                eventEnvelope(QStringLiteral("titleParametersDidChange"), ctx, titlePayload(ctx)));
        }
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
        QJsonObject payload = instancePayload(ctx);
        payload.insert(QStringLiteral("ticks"), ev.value); // signed (int32) preserved
        // audit 6.6: real pressed state (press-and-turn actions) tracked from
        // EncoderPressed/Released instead of a hardcoded false.
        payload.insert(QStringLiteral("pressed"),
                       m_pressedEncoders.count(static_cast<int>(ev.index)) > 0);
        m_server->sendEvent(ctx.pluginUuid,
                            eventEnvelope(QStringLiteral("dialRotate"), ctx, payload));
        break;
    }

    // ------------------------------------------------------------------
    // Encoder pressed -> dialDown + legacy keyDownCord alias
    // ------------------------------------------------------------------
    case Kind::EncoderPressed: {
        m_pressedEncoders.insert(static_cast<int>(ev.index));
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, static_cast<int>(ev.index));
        if (!ctxOpt.has_value()) {
            return;
        }
        ActionContext const& ctx = *ctxOpt;
        // audit 4.9: upstream mirrors dialDown/dialUp as key_moved too
        // (encoder.rs:85).
        emit keyPressMirror(deviceId, QStringLiteral("Encoder"), static_cast<int>(ev.index), true);
        QJsonObject const payload = instancePayload(ctx);
        m_server->sendEvent(ctx.pluginUuid,
                            eventEnvelope(QStringLiteral("dialDown"), ctx, payload));
        // AJAZZ legacy alias (§4.4 keyDownCord — AJAZZ-only).
        m_server->sendEvent(ctx.pluginUuid,
                            eventEnvelope(QStringLiteral("keyDownCord"), ctx, payload));
        break;
    }

    // ------------------------------------------------------------------
    // Encoder released -> dialUp + legacy keyUpCord alias
    // ------------------------------------------------------------------
    case Kind::EncoderReleased: {
        m_pressedEncoders.erase(static_cast<int>(ev.index));
        // Synthesised release from Phase 15's synthesiseEncoderRelease hook.
        auto const ctxOpt =
            m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, static_cast<int>(ev.index));
        if (!ctxOpt.has_value()) {
            return;
        }
        ActionContext const& ctx = *ctxOpt;
        emit keyPressMirror(deviceId, QStringLiteral("Encoder"), static_cast<int>(ev.index), false);
        QJsonObject const payload = instancePayload(ctx);
        m_server->sendEvent(ctx.pluginUuid, eventEnvelope(QStringLiteral("dialUp"), ctx, payload));
        m_server->sendEvent(ctx.pluginUuid,
                            eventEnvelope(QStringLiteral("keyUpCord"), ctx, payload));
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

        // CR-02 / WR-04: scope lookup to this device. audit 6.2: a dedicated
        // touch-zone binding registers at row 1; fall back to the dial context
        // (row 0) on AKP05-class devices where the dial owns its strip segment.
        auto ctxOpt = m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 1, zone);
        if (!ctxOpt.has_value()) {
            ctxOpt = m_registry.byCoord(deviceId, QStringLiteral("Encoder"), 0, zone);
        }
        if (!ctxOpt.has_value()) {
            return;
        }
        ActionContext const& ctx = *ctxOpt;
        QJsonObject payload = instancePayload(ctx);
        payload.insert(QStringLiteral("tapPos"), QJsonArray{x, 0}); // Elgato touchTap: tapPos [x,y]
        payload.insert(QStringLiteral("hold"), false);
        m_server->sendEvent(ctx.pluginUuid,
                            eventEnvelope(QStringLiteral("touchTap"), ctx, payload));
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

void PluginDeviceBridge::renderToggleState(QString const& controller,
                                           int index,
                                           ajazz::core::ActionInstance const& instance) {
    // BIND-07 render hook. Two effects, both reusing existing machinery:
    //   (1) repaint the control with states[currentState].visual (the SAME
    //       assignKeyImage / assignEncoderImage path the plugin setState handler
    //       uses -- no new render mechanism);
    //   (2) if a plugin owns the context at this coordinate, advance the registry
    //       stateIndex and re-send a state-change willAppear for that one context
    //       (cross-plugin ownership guard preserved). A pure built-in toggle has
    //       no owning plugin -> willAppear is a clean no-op.
    if (instance.states.empty()) {
        return;
    }
    auto const stateIdx = instance.currentState < instance.states.size()
                              ? instance.currentState
                              : 0u; // defensive clamp (model also clamps on read)
    auto const& visual = instance.states[stateIdx].visual;

    std::uint8_t const keyCols = geometryForDevice(m_activeDeviceId).keyCols; // F2

    // --- (1) Repaint -------------------------------------------------------
    // Source the image from the per-state imagePath (URL/path -> filesystem via
    // the same QUrl::toLocalFile boundary as StreamDockControlService).
    if (m_control != nullptr) {
        QImage img;
        if (visual.imagePath && !visual.imagePath->empty()) {
            QString const s = QString::fromStdString(*visual.imagePath);
            QString const local = s.startsWith(QStringLiteral("file:")) ? QUrl(s).toLocalFile() : s;
            img = QImage(local);
            if (img.isNull()) {
                AJAZZ_LOG_WARN("plugin-bridge",
                               "renderToggleState: state image failed to load: {}",
                               local.toStdString());
            }
        }
        // Composite the per-state title over the (possibly null) base; compositeTitle
        // degrades to a neutral fill when the base is null, and skips text in a
        // headless app (no font backend) so it stays crash-safe.
        QString const title = visual.text ? QString::fromStdString(*visual.text) : QString{};
        QImage const composited = compositeTitle(img, title);

        try {
            if (controller == QStringLiteral("Keypad")) {
                std::uint8_t const keyIndex =
                    static_cast<std::uint8_t>(index + 1); // 0-based -> 1-based device index
                m_control->assignKeyImage(keyIndex, composited);
                reapplyTitle(keyIndex);
            } else if (controller == QStringLiteral("Encoder")) {
                // assignEncoderImage uses the 0-based encoder index directly.
                m_control->assignEncoderImage(static_cast<std::uint8_t>(index), composited);
            }
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("plugin-bridge",
                           "renderToggleState: assign image threw for {} {}: {}",
                           controller.toStdString(),
                           index,
                           e.what());
        }
    }

    // --- (2) state-change willAppear (only if a plugin owns this context) ---
    if (m_server == nullptr || m_activeDeviceId.isEmpty()) {
        return;
    }
    // Resolve the registered context at this control coordinate. Keypad maps the
    // 0-based key index to {row,column}; Encoder/touch-zone uses row=0,col=index.
    std::optional<ActionContext> ctxOpt;
    if (controller == QStringLiteral("Keypad")) {
        auto const gc = coordsForKeyIndex(static_cast<std::uint8_t>(index + 1), keyCols);
        ctxOpt = m_registry.byCoord(m_activeDeviceId, controller, gc.row, gc.column);
    } else if (controller == QStringLiteral("Encoder")) {
        ctxOpt = m_registry.byCoord(m_activeDeviceId, controller, 0, index);
    }
    if (!ctxOpt.has_value()) {
        return; // pure built-in toggle: no plugin context to notify -> no-op
    }
    ActionContext ctx = *ctxOpt;
    // T-32-07: never drive a context owned by a different plugin. resolveOwner is
    // the same owner the willAppear is addressed to; the registered ctx.pluginUuid
    // is authoritative -- only notify the owning plugin.
    if (ctx.pluginUuid.isEmpty()) {
        return;
    }
    QString const ctxId = ContextRegistry::deriveContextId(ctx);
    m_registry.setState(ctxId, static_cast<int>(stateIdx));
    ctx.stateIndex = static_cast<int>(stateIdx);
    m_server->sendEvent(ctx.pluginUuid,
                        eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));
}

void PluginDeviceBridge::populateContextsForActivePage(QString const& deviceId,
                                                       QString const& pluginUuid) {
    // A6 simplification: scope to root page only (multi-page navigation is Phase 16).
    // willAppear is sent per bound ActionKind::Plugin action on the root page.
    if (!m_profileAccessor || m_server == nullptr) {
        return;
    }

    auto const& prof = m_profileAccessor();
    std::uint8_t const keyCols = geometryForDevice(deviceId).keyCols; // F2
    QString const pageId = QStringLiteral("root");

    // audit 6.8: resolve the user-set label for a binding so
    // titleParametersDidChange delivers it (ctx.title). The OpenDeck-shaped
    // instance's current state wins; the legacy KeyState label is the fallback.
    auto const titleFromBinding = [](std::optional<core::ActionInstance> const& inst,
                                     core::KeyState const& legacy) -> QString {
        if (inst.has_value() && !inst->states.empty()) {
            std::size_t const idx =
                std::min<std::size_t>(inst->currentState, inst->states.size() - 1);
            if (inst->states[idx].visual.text.has_value()) {
                return QString::fromStdString(*inst->states[idx].visual.text);
            }
        }
        return legacy.text.has_value() ? QString::fromStdString(*legacy.text) : QString{};
    };

    // RECONCILE (PLUGIN-move parity, mirrors OpenDeck move_instance): collect the
    // set of context ids that SHOULD be live for this device/page given the current
    // profile. After the willAppear pass we diff this against the registry snapshot
    // and send willDisappear + retire for any context that is registered but no
    // longer desired (e.g. the key a binding was just moved away from). Without this
    // pass the vacated key keeps a stale context and the plugin never learns the
    // action left it, so a moved plugin action renders in two places / never on the
    // new key. The id form here is exactly the registry key (registerContext return).
    QSet<QString> desired;

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
            QString const owner = resolveOwner(actionId);
            if (owner.isEmpty()) {
                continue;
            }
            // Filter: if pluginUuid is non-empty, only emit for that plugin.
            if (!pluginUuid.isEmpty() && owner != pluginUuid) {
                continue;
            }
            // Profile::keys use 0-based uint16_t index; device uses 1-based.
            // audit 6.14: the uint8_t cast wraps at 255 — skip out-of-range
            // slots instead of registering/painting the wrong key.
            if (keyIdx0 >= 255) {
                continue;
            }
            std::uint8_t const keyIdx1 = static_cast<std::uint8_t>(keyIdx0 + 1);
            auto const gc = coordsForKeyIndex(keyIdx1, keyCols);

            ActionContext ctx;
            ctx.deviceId = deviceId;
            ctx.pageId = pageId;
            ctx.row = gc.row;
            ctx.column = gc.column;
            ctx.controller = QStringLiteral("Keypad");
            ctx.actionUUID = actionId;
            ctx.pluginUuid = owner;
            // Settings precedence: persisted store record (set by the plugin/PI)
            // wins over the binding's default settingsJson, so willAppear delivers
            // the live config. Falls back to the binding default on first run.
            ctx.settingsJson = settingsForContext(
                owner,
                ContextRegistry::deriveContextId(ctx),
                QString::fromStdString(action.settingsJson),
                m_defaultSettingsResolver ? m_defaultSettingsResolver(actionId) : QString{});
            ctx.title = titleFromBinding(binding.instance, binding.state); // audit 6.8

            // Track newness BEFORE registering: a brand-new context gets the
            // manifest default render below; an idempotent re-registration must
            // NOT clobber an image the plugin may have pushed since. An IN-PLACE
            // REBIND (different action, same key — the context id is coordinate-
            // based) counts as new too: without this, the new action gets no
            // default render and the OLD action's last frame lingers on the key
            // (review finding 2026-06-10).
            auto const prior = m_registry.byContext(ContextRegistry::deriveContextId(ctx));
            bool const actionChanged = prior.has_value() && prior->actionUUID != ctx.actionUUID;
            bool const isNewContext = !prior.has_value() || actionChanged;
            QString const ctxId = m_registry.registerContext(ctx);
            desired.insert(ctxId);
            auto const regOpt = m_registry.byContext(ctxId);
            // registerContext preserves a prior stateIndex; reflect it in the ctx
            // we serialise so willAppear carries the live state, not the default 0.
            if (regOpt.has_value()) {
                ctx.stateIndex = regOpt->stateIndex;
            }

            // Mount-time default render (OpenDeck/Elgato parity): paint the
            // manifest's declared image for the current state (state Image,
            // else the action Icon) the moment the action lands on a key, so a
            // plugin that never pushes setImage (e.g. com.jk.weather) still
            // shows its icon instead of a blank key. The plugin's own
            // setImage/setTitle, when it comes, overwrites this base.
            if (isNewContext) {
                if (actionChanged) {
                    // The OLD action's instance is going away. Tell its plugin
                    // (OpenDeck move_instance parity — without this the old
                    // plugin keeps streaming setTitle/setImage for a context it
                    // no longer owns), drop its stale title overlay (otherwise
                    // reapplyTitle() re-paints e.g. "CPU 11%" over the NEW
                    // action's default image), and clear the lingering frame
                    // (the new action's default paint below may be a legitimate
                    // no-op when it declares no image).
                    m_server->sendEvent(prior->pluginUuid,
                                        eventEnvelope(QStringLiteral("willDisappear"),
                                                      *prior,
                                                      instancePayload(*prior)));
                    m_titleByKey.erase(keyIdx1);
                    if (m_control != nullptr) {
                        m_control->clearKeyImage(keyIdx1);
                    }
                }
                paintDeclaredStateImage(ctx, keyCols);
            }

            // Full Elgato envelope: top-level action/context/device + payload
            // {settings, coordinates, controller, state, isInMultiAction}.
            m_server->sendEvent(
                owner, eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));
            // PI-04: titleParametersDidChange follows willAppear inline for the SAME
            // ctx so ordering is guaranteed with no extra plumbing (research Pitfall 1).
            m_server->sendEvent(
                owner,
                eventEnvelope(QStringLiteral("titleParametersDidChange"), ctx, titlePayload(ctx)));
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
            QString const owner = resolveOwner(actionId);
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
            ctx.settingsJson = settingsForContext(
                owner,
                ContextRegistry::deriveContextId(ctx),
                QString::fromStdString(action.settingsJson),
                m_defaultSettingsResolver ? m_defaultSettingsResolver(actionId) : QString{});
            ctx.title = titleFromBinding(encBinding.instance, encBinding.state); // audit 6.8

            // Same newness/in-place-rebind semantics as the Keypad loop above.
            auto const priorEnc = m_registry.byContext(ContextRegistry::deriveContextId(ctx));
            bool const encActionChanged =
                priorEnc.has_value() && priorEnc->actionUUID != ctx.actionUUID;
            QString const ctxId = m_registry.registerContext(ctx);
            desired.insert(ctxId);
            auto const regOpt = m_registry.byContext(ctxId);
            if (regOpt.has_value()) {
                ctx.stateIndex = regOpt->stateIndex;
            }

            // Mount-time dial layout render: paint the manifest layout/icon the
            // moment the dial action lands (the plugin's setFeedback, when it
            // comes, updates items on top). On an in-place rebind drop the old
            // action's accumulated feedback first, and tell the DISPLACED
            // plugin its instance is gone (audit 6.13 — keypad parity;
            // without willDisappear the old plugin keeps streaming
            // setFeedback/setImage for a dial it no longer owns).
            if (encActionChanged) {
                m_server->sendEvent(priorEnc->pluginUuid,
                                    eventEnvelope(QStringLiteral("willDisappear"),
                                                  *priorEnc,
                                                  instancePayload(*priorEnc)));
                m_encoderFeedback.erase(ctxId);
                m_encoderLayoutOverride.erase(ctxId);
            }
            if (!priorEnc.has_value() || encActionChanged) {
                renderEncoderFeedback(ctx);
            }

            m_server->sendEvent(
                owner, eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));
            // PI-04: titleParametersDidChange follows willAppear inline (encoder ctx).
            m_server->sendEvent(
                owner,
                eventEnvelope(QStringLiteral("titleParametersDidChange"), ctx, titlePayload(ctx)));
        }
    }

    // Enumerate touch-zone bindings (0-based zone index in Profile::touchZones).
    //
    // CONVENTION LOCK (A2 / Pitfall 5, revised for audit 6.2): touch-zone
    // contexts are registered under controller="Encoder", row=1,
    // column=zoneIndex — row 1 so they no longer COLLIDE with encoder i
    // (controller="Encoder", row=0, column=i): with both bound, the identical
    // tuple made dial events carry the touch action's uuid and vice versa.
    // This MUST match the TouchUp lookup in onDeviceEvent, which tries
    //     m_registry.byCoord(deviceId, "Encoder", 1, zone)   [touch binding]
    // and falls back to row 0 (the dial owns its strip segment on AKP05-class
    // devices). Do NOT change either side without the other.
    for (auto const& [zoneIdx, tzBinding] : prof.touchZones) {
        for (auto const& action : tzBinding.onTap) {
            if (action.kind != core::ActionKind::Plugin) {
                continue;
            }
            QString const actionId = QString::fromStdString(action.id);
            if (actionId.isEmpty()) {
                continue;
            }
            QString const owner = resolveOwner(actionId);
            if (owner.isEmpty()) {
                continue;
            }
            if (!pluginUuid.isEmpty() && owner != pluginUuid) {
                continue;
            }

            // Register under controller="Encoder", row=1, column=zoneIndex
            // (row 1 = touch zone; row 0 = the dial itself — audit 6.2).
            ActionContext ctx;
            ctx.deviceId = deviceId;
            ctx.pageId = pageId;
            ctx.row = 1;
            ctx.column = static_cast<int>(zoneIdx);
            ctx.controller = QStringLiteral("Encoder");
            ctx.actionUUID = actionId;
            ctx.pluginUuid = owner;
            ctx.settingsJson = settingsForContext(
                owner,
                ContextRegistry::deriveContextId(ctx),
                QString::fromStdString(action.settingsJson),
                m_defaultSettingsResolver ? m_defaultSettingsResolver(actionId) : QString{});
            // audit 6.8 (TouchZoneBinding carries no OpenDeck-shaped instance).
            ctx.title = titleFromBinding(std::nullopt, tzBinding.state);

            // audit 6.13: in-place rebind parity — tell the displaced plugin
            // its touch instance is gone (same as the keypad/encoder loops).
            auto const priorTz = m_registry.byContext(ContextRegistry::deriveContextId(ctx));
            if (priorTz.has_value() && priorTz->actionUUID != ctx.actionUUID) {
                m_server->sendEvent(priorTz->pluginUuid,
                                    eventEnvelope(QStringLiteral("willDisappear"),
                                                  *priorTz,
                                                  instancePayload(*priorTz)));
            }

            QString const ctxId = m_registry.registerContext(ctx);
            desired.insert(ctxId);
            auto const regOpt = m_registry.byContext(ctxId);
            if (regOpt.has_value()) {
                ctx.stateIndex = regOpt->stateIndex;
            }

            m_server->sendEvent(
                owner, eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));
            // PI-04: titleParametersDidChange follows willAppear inline (touch-zone ctx).
            m_server->sendEvent(
                owner,
                eventEnvelope(QStringLiteral("titleParametersDidChange"), ctx, titlePayload(ctx)));
        }
    }

    // RECONCILE retire pass: send willDisappear + retire for every context that is
    // currently registered for this device's root page (respecting the optional
    // pluginUuid filter) but is NOT in the desired set we just rebuilt from the
    // profile. This is what makes "move a binding to another control" correct: the
    // source control's context is no longer desired, so the plugin receives
    // willDisappear for it (matching OpenDeck move_instance's willDisappear(old)).
    // Scoped to pageId=="root" because that is the only page populate registers.
    for (auto const& [ctxId, ctx] : m_registry.snapshot()) {
        if (ctx.deviceId != deviceId || ctx.pageId != pageId) {
            continue;
        }
        if (!pluginUuid.isEmpty() && ctx.pluginUuid != pluginUuid) {
            continue;
        }
        if (desired.contains(ctxId)) {
            continue;
        }
        m_server->sendEvent(
            ctx.pluginUuid,
            eventEnvelope(QStringLiteral("willDisappear"), ctx, instancePayload(ctx)));
        purgeContextVisualCaches(ctx, ctxId);
        m_registry.retire(ctxId);
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
        // sendEvent returns false safely (T-19-sock). Full Elgato envelope.
        m_server->sendEvent(
            ctx.pluginUuid,
            eventEnvelope(QStringLiteral("willDisappear"), ctx, instancePayload(ctx)));
        purgeContextVisualCaches(ctx, ctxId);
        m_registry.retire(ctxId);
    }
}

void PluginDeviceBridge::purgeContextVisualCaches(ActionContext const& ctx, QString const& ctxId) {
    // A retired context must take its cached visual layers with it: the title
    // overlay and encoder feedback bag otherwise survive and composite the OLD
    // action's title/icon onto the NEXT action bound to the same control
    // (audit 6.1 -- "CPU 11%" painted over Weather's icon).
    m_encoderFeedback.erase(ctxId);
    m_encoderLayoutOverride.erase(ctxId);
    m_stateImageOverride.erase(ctxId);
    m_stateTitleOverride.erase(ctxId);
    if (ctx.controller == QLatin1String("Keypad")) {
        auto const keyCols = geometryForDevice(ctx.deviceId).keyCols;
        m_titleByKey.erase(keyIndexForCoords(ctx.row, ctx.column, keyCols));
    }
}

// ---------------------------------------------------------------------------
// Phase 19-03: Lifecycle slots
// ---------------------------------------------------------------------------

void PluginDeviceBridge::onPluginRegistered(QString const& pluginUuid) {
    m_registeredPlugins.insert(pluginUuid);
    // audit 6.9: replay deviceDidConnect for every device that connected BEFORE
    // this plugin finished its WS handshake — without it a late-registering
    // plugin never learns a device exists (Elgato plugins commonly wait for
    // deviceDidConnect before doing any work).
    if (m_server != nullptr) {
        for (QString const& connected : m_connectedDevices) {
            m_server->sendEvent(pluginUuid,
                                QJsonObject{
                                    {QStringLiteral("event"), QStringLiteral("deviceDidConnect")},
                                    {QStringLiteral("device"), connected},
                                    {QStringLiteral("deviceInfo"), deviceInfoFor(connected)},
                                });
        }
    }
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
    m_connectedDevices.insert(deviceId); // audit 6.9: replay set

    // Populate contexts for the active page.
    populateContextsForActivePage(deviceId);

    // Send deviceDidConnect to all registered plugins (§4.4).
    if (m_server == nullptr) {
        return;
    }
    // Elgato deviceDidConnect: top-level `device` + `deviceInfo` siblings (NOT
    // wrapped in payload). The deviceInfo shape is built once in deviceInfoFor()
    // and reused for the vendor passHello.deviceInfo (B7).
    QJsonObject const event{
        {QStringLiteral("event"), QStringLiteral("deviceDidConnect")},
        {QStringLiteral("device"), deviceId},
        {QStringLiteral("deviceInfo"), deviceInfoFor(deviceId)},
    };
    for (QString const& uuid : m_registeredPlugins) {
        m_server->sendEvent(uuid, event);
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
    m_connectedDevices.remove(deviceId); // audit 6.9: replay set
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

    // Send deviceDidDisconnect to all registered plugins (Elgato: top-level device).
    QJsonObject const event{
        {QStringLiteral("event"), QStringLiteral("deviceDidDisconnect")},
        {QStringLiteral("device"), deviceId},
    };
    for (QString const& uuid : m_registeredPlugins) {
        m_server->sendEvent(uuid, event);
    }
}

void PluginDeviceBridge::onActivePageChanged(QString const& deviceId, QString const& pageId) {
    // Retire old page contexts (willDisappear) then populate the new page (willAppear).
    // A6: root-page only for Phase 19; multi-page navigation is Phase 16's authority.
    retirePageContexts(deviceId, QStringLiteral("root"), {});
    populateContextsForActivePage(deviceId);
    Q_UNUSED(pageId); // multi-page scope deferred to Phase 16
}

void PluginDeviceBridge::onPropertyInspectorSettings(QString const& pluginUuid,
                                                     QString const& contextId,
                                                     QString const& json) {
    if (m_server == nullptr) {
        return;
    }
    // Resolve the context to the live ActionContext. The PI addresses the action
    // by the SPA context string ("device.profile.controller.position"), which is
    // NOT the bridge wire id ("device#page#controller#row#column"); try the wire
    // id first (callers/tests may pass it directly), then translate the SPA form
    // via coordinates. If nothing resolves the action has no mounted instance
    // (e.g. PI open for a control on an inactive page) — a safe no-op. The PI
    // still persisted to disk, and the next willAppear carries the value via
    // settingsForContext(). Before this translation, real-PI setSettings silently
    // never reached the plugin (context mismatch), so dropped actions never ran.
    auto ctxOpt = lookupContext(contextId);
    if (!ctxOpt.has_value()) {
        return;
    }
    ActionContext ctx = *ctxOpt;
    // Update by the canonical wire id (the resolved context), not the raw PI id.
    QString const wireId = ContextRegistry::deriveContextId(ctx);
    // Keep the registry's cached settings in sync so a subsequent willAppear /
    // keyDown for this context carries the just-edited value, not the stale one.
    bool const piChanged = (ctx.settingsJson != json);
    m_registry.updateSettings(wireId, json);
    ctx.settingsJson = json;
    if (piChanged) {
        emitBindingSettingsPersisted(ctx, json); // mirror into the profile binding
    }
    // Prefer the bridge-known owner; fall back to the PI-supplied uuid if the
    // registry entry has none (defensive — registration always sets it).
    QString const owner = ctx.pluginUuid.isEmpty() ? pluginUuid : ctx.pluginUuid;
    m_server->sendEvent(
        owner, eventEnvelope(QStringLiteral("didReceiveSettings"), ctx, instancePayload(ctx)));
}

std::optional<ActionContext> PluginDeviceBridge::lookupContext(QString const& contextId) const {
    auto exact = m_registry.byContext(contextId);
    if (exact.has_value()) {
        return exact;
    }
    return resolvePropertyInspectorContext(contextId);
}

QString PluginDeviceBridge::canonicalContextId(QString const& contextId) const {
    auto const ctx = lookupContext(contextId);
    return ctx.has_value() ? ContextRegistry::deriveContextId(*ctx) : contextId;
}

QString PluginDeviceBridge::settingsJsonForContext(QString const& contextId) const {
    auto const ctx = lookupContext(contextId);
    return ctx.has_value() ? ctx->settingsJson : QString{};
}

void PluginDeviceBridge::emitBindingSettingsPersisted(ActionContext const& ctx,
                                                      QString const& json) {
    QString controller = ctx.controller;
    int index = ctx.column;
    if (ctx.controller == QLatin1String("Keypad")) {
        index = ctx.row * geometryForDevice(ctx.deviceId).keyCols + ctx.column;
    } else if (ctx.row == 1) {
        controller = QStringLiteral("TouchZone"); // audit 6.2 convention
    }
    emit bindingSettingsPersisted(ctx.deviceId, controller, index, json);
}

std::optional<ActionContext>
PluginDeviceBridge::resolvePropertyInspectorContext(QString const& contextId) const {
    // SPA context: "device.profile.controller.position" (profile may contain
    // dots). Parse from the right: position is last, controller second-last,
    // device first; the middle (profile) is irrelevant here because byCoord keys
    // on coordinates, not page id.
    QStringList const parts = contextId.split(QLatin1Char('.'));
    if (parts.size() < 4) {
        return std::nullopt;
    }
    QString const device = parts.first();
    QString controller = parts.at(parts.size() - 2);
    bool ok = false;
    int position = parts.at(parts.size() - 1).toInt(&ok);
    // Upstream 5-segment form "device.profile.controller.position.index": when
    // the segment in controller position is itself numeric, the trailing
    // segment is the Multi Action child index — shift left (same
    // disambiguation as opendeck_detail::parseCtxString, audit 4.3). Without
    // it every PI registered with a 5-segment context resolved to
    // controller="<position>" and was silently unresolved (live find,
    // 2026-07-02: PI owner=<unresolved> for akp05e.Default.Keypad.2.0).
    bool controllerIsNumeric = false;
    controller.toInt(&controllerIsNumeric);
    if (parts.size() >= 5 && ok && controllerIsNumeric) {
        position = controller.toInt(&ok);
        controller = parts.at(parts.size() - 3);
    }
    if (!ok || position < 0 || device.isEmpty()) {
        return std::nullopt;
    }
    int row = 0;
    int column = 0;
    QString effectiveController = controller;
    if (controller == QStringLiteral("Encoder")) {
        column = position; // encoders are a single row indexed by column
    } else {
        auto const geo = geometryForDevice(device);
        std::uint8_t keyCols = geo.keyCols;
        if (keyCols == 0) {
            keyCols = 5; // AKP05E default, matches geometryForDevice's fallback
        }
        // SPA touch slots are the "Keypad" row appended after the key grid, but
        // the bridge registers touch contexts under ("Encoder", 1, zone) — the
        // Keypad row/col math missed them and every PI set/getSettings for a
        // touch action was silently dropped (audit 6.5; row moved 0 -> 1 by
        // audit 6.2 to stop colliding with the dial context).
        int const keyCount = geo.keyCount > 0 ? geo.keyCount : keyCols * geo.keyRows;
        if (keyCount > 0 && position >= keyCount) {
            // Dedicated touch-zone binding first; dial-owns-segment fallback.
            auto const touchCtx =
                m_registry.byCoord(device, QStringLiteral("Encoder"), 1, position - keyCount);
            if (touchCtx.has_value()) {
                return touchCtx;
            }
            effectiveController = QStringLiteral("Encoder");
            row = 0;
            column = position - keyCount;
        } else {
            row = position / keyCols;
            column = position % keyCols;
        }
    }
    return m_registry.byCoord(device, effectiveController, row, column);
}

} // namespace ajazz::app

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
