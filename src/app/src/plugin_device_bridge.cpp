// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_device_bridge.cpp
 * @brief PluginDeviceBridge implementation — ContextRegistry + pure helpers.
 *
 * Phase 19-01 delivers:
 *   - ContextRegistry method bodies (pure map operations, no seams).
 *   - coordsForKeyIndex / keyIndexForCoords (Pitfall 2 — single named pair).
 *   - decodeDataUriImage (Don't Hand-Roll — Qt's fromBase64 + loadFromData).
 *   - ownerForActionUuid (Pitfall 4 — longest-prefix dotted-component match).
 *   - PluginDeviceBridge constructor/destructor stub (Phase 19-02 wires the
 *     QObject::connect calls).
 *
 * COD-031: app-layer QJson/QImage only; never nlohmann.
 */
#include "plugin_device_bridge.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImageReader>

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
QString ContextRegistry::coordKey(QString const& controller, int row, int column) {
    return controller + QChar('#') + QString::number(row) + QChar('#') + QString::number(column);
}

QString ContextRegistry::registerContext(ActionContext const& ctx) {
    QString const ctxId = deriveContextId(ctx);
    m_byContext.insert(ctxId, ctx);
    QString const ck = coordKey(ctx.controller, ctx.row, ctx.column);
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

std::optional<ActionContext>
ContextRegistry::byCoord(QString const& controller, int row, int column) const {
    QString const ck = coordKey(controller, row, column);
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
    m_byCoord.remove(coordKey(ctx.controller, ctx.row, ctx.column));
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

    // Decode base64. fromBase64 never throws; an empty/garbage input yields
    // empty bytes (which loadFromData will reject).
    QByteArray const raw = QByteArray::fromBase64(bodyStr.toUtf8());
    if (raw.isEmpty()) {
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
// PluginDeviceBridge constructor / destructor (Phase 19-01 stub)
// ---------------------------------------------------------------------------

PluginDeviceBridge::PluginDeviceBridge(SdPluginServer* server,
                                       StreamDockControlService* control,
                                       StreamDockInputService* input,
                                       QObject* parent)
    : QObject(parent), m_server(server), m_control(control), m_input(input) {
    // Phase 19-02 wires QObject::connect calls here.
    // Phase 19-03 wires the DeviceEvent -> sendEvent path here.
}

PluginDeviceBridge::~PluginDeviceBridge() = default;

ContextRegistry& PluginDeviceBridge::registry() noexcept {
    return m_registry;
}

ContextRegistry const& PluginDeviceBridge::registry() const noexcept {
    return m_registry;
}

} // namespace ajazz::app
