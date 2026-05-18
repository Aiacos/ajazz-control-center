// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_model.cpp
 * @brief DeviceModel QAbstractListModel implementation.
 *
 * refresh() calls DeviceRegistry::enumerate() to rebuild the static catalog
 * (collapsed by codename per D-04) and hid_enumerate() to discover
 * currently-plugged devices, then propagates the diff to QML via per-row
 * dataChanged({ConnectedRole}) signals (D-03). ConnectedRole ORs the live
 * presence of every rebadge (vid, pid) that shares each codename. The
 * full beginResetModel()/endResetModel() reset path is reserved for the
 * defensive case where the row count actually changed (a backend was
 * registered/unregistered between calls — bootstrap-only in v1.1).
 */
#include "device_model.hpp"

#include "ajazz/core/device_registry.hpp"
#include "ajazz/core/logger.hpp"
#include "device_maturity_map.generated.hpp"

#include <QHash>
#include <QQmlEngine>
#include <QString>

#include <algorithm>

namespace ajazz::app {

namespace {

/// Pointer set by DeviceModel::registerInstance, consumed by ::create.
DeviceModel* s_deviceModelInstance = nullptr;

/// Codename → maturity tier mapping. Generated at pre-commit / CI time
/// from `docs/_data/devices.yaml` via `scripts/generate-docs.py` into
/// `device_maturity_map.generated.hpp`. Edit the YAML, not this file
/// (audit 2026-05-18 closed this hidden touch point).
///
/// Unknown codenames default to "scaffolded" via the lookup helper below,
/// so a stale generated header degrades visibly (the QML sidebar shows
/// the device as scaffolded) instead of crashing.
QHash<QString, QString> const& maturityByCodename() {
    static QHash<QString, QString> const kMap = generated::deviceMaturityByCodename();
    return kMap;
}

/// Look up the maturity tier for a codename. Falls back to "scaffolded"
/// for unknown codenames so the QML side always has a valid tier string.
QString maturityFor(std::string const& codename) {
    auto const it = maturityByCodename().constFind(QString::fromStdString(codename));
    return it == maturityByCodename().constEnd() ? QStringLiteral("scaffolded") : it.value();
}

} // namespace

DeviceModel* DeviceModel::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_deviceModelInstance != nullptr,
               "DeviceModel::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_deviceModelInstance, QQmlEngine::CppOwnership);
    return s_deviceModelInstance;
}

void DeviceModel::registerInstance(DeviceModel* instance) noexcept {
    s_deviceModelInstance = instance;
}

DeviceModel::DeviceModel(core::DeviceRegistry& registry, QObject* parent)
    : QAbstractListModel(parent), m_registry(registry) {}

int DeviceModel::rowCount(QModelIndex const& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant DeviceModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    auto const& d = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ModelRole:
        return QString::fromStdString(d.model);
    case CodenameRole:
        return QString::fromStdString(d.codename);
    case FamilyRole:
        return static_cast<int>(d.family);
    case VidRole:
        return d.vendorId;
    case PidRole:
        return d.productId;
    case ConnectedRole: {
        // Per D-04 rebadge contract: row identity is `codename`, so
        // connected-state must OR across every rebadge (vid, pid) that
        // shares this codename. The set is computed at refresh() time
        // (`m_codename_keys`) so this lookup is O(rebadge_count) rather
        // than O(registry_size). A codename row is connected iff ANY
        // of its rebadge keys is in `m_connected`.
        auto const it = m_codename_keys.find(d.codename);
        if (it == m_codename_keys.end()) {
            // No rebadge group recorded — fall back to the legacy
            // single-key check (defensive; refresh() always populates
            // m_codename_keys, so this path is unreachable in practice).
            auto const key = std::make_pair(d.vendorId, d.productId);
            return m_connected.find(key) != m_connected.end();
        }
        for (auto const& key : it->second) {
            if (m_connected.find(key) != m_connected.end()) {
                return true;
            }
        }
        return false;
    }
    case KeyCountRole:
        return static_cast<int>(d.keyCount);
    case GridColumnsRole:
        return static_cast<int>(d.gridColumns);
    case EncoderCountRole:
        return static_cast<int>(d.encoderCount);
    case DpiStageCountRole:
        return static_cast<int>(d.dpiStageCount);
    case HasRgbRole:
        return d.hasRgb;
    case HasTouchStripRole:
        return d.hasTouchStrip;
    case HasClockRole:
        return d.hasClock;
    case MaturityRole:
        return maturityFor(d.codename);
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const {
    return {
        {ModelRole, "model"},
        {CodenameRole, "codename"},
        {FamilyRole, "family"},
        {VidRole, "vid"},
        {PidRole, "pid"},
        {ConnectedRole, "connected"},
        {KeyCountRole, "keyCount"},
        {GridColumnsRole, "gridColumns"},
        {EncoderCountRole, "encoderCount"},
        {DpiStageCountRole, "dpiStageCount"},
        {HasRgbRole, "hasRgb"},
        {HasTouchStripRole, "hasTouchStrip"},
        {HasClockRole, "deviceHasClock"},
        {MaturityRole, "maturity"},
    };
}

void DeviceModel::refresh() {
    // Diff-driven refresh per D-03 + HOTPLUG-02/03/04. Selection and
    // scroll position survive automatically because no row index moves
    // in the common path — we only emit per-row dataChanged for rows
    // whose ConnectedRole flipped between previous refresh and now.

    // Pull the full registry list — every (vid, pid) descriptor.
    auto const all_descriptors = m_registry.enumerate();

    // Step 1 — Build the new codename → keys map (D-04 rebadge collapse).
    std::map<std::string, std::set<std::pair<std::uint16_t, std::uint16_t>>> next_codename_keys;
    for (auto const& d : all_descriptors) {
        next_codename_keys[d.codename].emplace(d.vendorId, d.productId);
    }

    // Step 2 — Collapse to one descriptor per codename (first-encountered
    // wins as the representative row; the model name shown in the UI is
    // therefore deterministic across runs because registerDevice() insertion
    // order is deterministic).
    std::vector<core::DeviceDescriptor> collapsed;
    collapsed.reserve(next_codename_keys.size());
    {
        std::set<std::string> seen_codenames;
        for (auto const& d : all_descriptors) {
            if (seen_codenames.insert(d.codename).second) {
                collapsed.push_back(d);
            }
        }
    }

    // Step 3 — Lex sort by (family, codename) per HOTPLUG-04. Stable
    // across arrival/departure/re-arrival because both keys are
    // attributes of the descriptor, not of the live presence state.
    std::sort(collapsed.begin(),
              collapsed.end(),
              [](core::DeviceDescriptor const& a, core::DeviceDescriptor const& b) {
                  if (a.family != b.family) {
                      return static_cast<int>(a.family) < static_cast<int>(b.family);
                  }
                  return a.codename < b.codename;
              });

    // Step 4 — Compute connected-state-then for the OLD row layout
    // (pre-update) using the old m_connected + m_codename_keys.
    std::vector<bool> connected_then;
    connected_then.reserve(m_rows.size());
    for (auto const& d : m_rows) {
        bool any = false;
        if (auto const it = m_codename_keys.find(d.codename); it != m_codename_keys.end()) {
            for (auto const& key : it->second) {
                if (m_connected.find(key) != m_connected.end()) {
                    any = true;
                    break;
                }
            }
        }
        connected_then.push_back(any);
    }

    // Step 5 — Decide between common-path (sizes equal) and reset
    // fallback (row count grew/shrank: a backend was registered or
    // unregistered between calls — bootstrap-only in v1.1, but
    // defensively handled).
    bool const sizes_match = (collapsed.size() == m_rows.size());
    bool codenames_match = sizes_match;
    if (sizes_match) {
        for (std::size_t i = 0; i < collapsed.size(); ++i) {
            if (collapsed[i].codename != m_rows[i].codename) {
                codenames_match = false;
                break;
            }
        }
    }

    if (!codenames_match) {
        AJAZZ_LOG_INFO("device-model",
                       "row layout changed (was {} rows, now {}) — performing reset",
                       static_cast<int>(m_rows.size()),
                       static_cast<int>(collapsed.size()));
        beginResetModel();
        m_rows = std::move(collapsed);
        m_codename_keys = std::move(next_codename_keys);
        refreshLiveEnumeration();
        endResetModel();
        return;
    }

    // Common path — sizes + codename order are stable.
    m_rows = std::move(collapsed);
    m_codename_keys = std::move(next_codename_keys);
    refreshLiveEnumeration(); // updates m_connected

    // Step 6 — Compute connected-state-now for the new layout (which
    // matches the old layout codename-for-codename) and emit per-row
    // dataChanged for any row whose connected-state flipped.
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        auto const& d = m_rows[row];
        bool now = false;
        if (auto const it = m_codename_keys.find(d.codename); it != m_codename_keys.end()) {
            for (auto const& key : it->second) {
                if (m_connected.find(key) != m_connected.end()) {
                    now = true;
                    break;
                }
            }
        }
        if (now != connected_then[row]) {
            QModelIndex const idx = createIndex(static_cast<int>(row), 0);
            emit dataChanged(idx, idx, {ConnectedRole});
        }
    }
}

void DeviceModel::refreshLiveEnumeration() {
    // The actual hidapi call lives in core/DeviceRegistry so the app target
    // does not need to link hidapi directly. On Linux the result reflects the
    // /dev/hidraw* nodes the calling user can open (uaccess ACL); on Windows
    // and macOS it reflects every device the OS has enumerated.
    m_connected = m_registry.enumerateConnectedHidKeys();
    int matching = 0;
    for (auto const& d : m_rows) {
        // Per D-04: a codename row is "online" iff ANY of its rebadge
        // (vid, pid) keys is in m_connected. Walk the rebadge group
        // recorded in m_codename_keys rather than the representative
        // descriptor's single (vid, pid) pair.
        if (auto const it = m_codename_keys.find(d.codename); it != m_codename_keys.end()) {
            for (auto const& key : it->second) {
                if (m_connected.count(key) > 0) {
                    ++matching;
                    break;
                }
            }
        }
    }
    AJAZZ_LOG_INFO("device-model",
                   "live enumeration: {} hid device(s) total, {} of {} supported model(s) online",
                   static_cast<int>(m_connected.size()),
                   matching,
                   static_cast<int>(m_rows.size()));
}

QVariantMap DeviceModel::capabilitiesFor(QString const& codename) const {
    auto const target = codename.toStdString();
    auto it = std::find_if(m_rows.begin(), m_rows.end(), [&](core::DeviceDescriptor const& d) {
        return d.codename == target;
    });
    if (it == m_rows.end()) {
        return {};
    }
    QVariantMap m;
    m.insert(QStringLiteral("model"), QString::fromStdString(it->model));
    m.insert(QStringLiteral("codename"), QString::fromStdString(it->codename));
    m.insert(QStringLiteral("family"), static_cast<int>(it->family));
    m.insert(QStringLiteral("keyCount"), static_cast<int>(it->keyCount));
    m.insert(QStringLiteral("gridColumns"), static_cast<int>(it->gridColumns));
    m.insert(QStringLiteral("encoderCount"), static_cast<int>(it->encoderCount));
    m.insert(QStringLiteral("dpiStageCount"), static_cast<int>(it->dpiStageCount));
    m.insert(QStringLiteral("hasRgb"), it->hasRgb);
    m.insert(QStringLiteral("hasTouchStrip"), it->hasTouchStrip);
    m.insert(QStringLiteral("hasClock"), it->hasClock);
    return m;
}

std::vector<QString> DeviceModel::connectedCodenames() const {
    // O(rows) walk over the row vector — each row's connected-state is
    // already memoised in m_codename_keys + m_connected (D-04 OR-across-
    // rebadge), so we just need to filter. Mirrors the logic in the
    // ConnectedRole branch of data().
    std::vector<QString> out;
    out.reserve(m_rows.size());
    for (auto const& row : m_rows) {
        auto const it = m_codename_keys.find(row.codename);
        if (it == m_codename_keys.end()) {
            continue;
        }
        bool connected = false;
        for (auto const& key : it->second) {
            if (m_connected.find(key) != m_connected.end()) {
                connected = true;
                break;
            }
        }
        if (connected) {
            out.emplace_back(QString::fromStdString(row.codename));
        }
    }
    return out;
}

} // namespace ajazz::app
