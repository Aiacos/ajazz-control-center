// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_catalog_proxy_model.cpp
 * @brief Implementation of @ref ajazz::app::PluginCatalogProxyModel.
 */
#include "plugin_catalog_proxy_model.hpp"

#include "plugin_catalog_model.hpp" // for PluginCatalogModel::Roles

#include <QStringList>
#include <QVariant>

namespace ajazz::app {

PluginCatalogProxyModel::PluginCatalogProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    // `count` Q_PROPERTY needs a NOTIFY signal — wire it to every
    // QAbstractItemModel signal that changes rowCount. The shapes:
    //   * rowsInserted / rowsRemoved — source mutation that passed the
    //     filter (e.g. fetcher snapshot adds rows).
    //   * modelReset — source-wide reset (e.g. setSourceModel).
    //   * layoutChanged — what QSortFilterProxyModel emits when the
    //     filter mapping changes after setActiveTab() / setQuery() call
    //     invalidate(). Without this connection, count wouldn't
    //     repaint after a tab switch (verified via test 87).
    QObject::connect(
        this, &QAbstractItemModel::rowsInserted, this, &PluginCatalogProxyModel::countChanged);
    QObject::connect(
        this, &QAbstractItemModel::rowsRemoved, this, &PluginCatalogProxyModel::countChanged);
    QObject::connect(
        this, &QAbstractItemModel::modelReset, this, &PluginCatalogProxyModel::countChanged);
    QObject::connect(
        this, &QAbstractItemModel::layoutChanged, this, &PluginCatalogProxyModel::countChanged);
}

void PluginCatalogProxyModel::setActiveTab(int tab) {
    if (tab == m_activeTab) {
        return;
    }
    m_activeTab = tab;
    emit activeTabChanged();
    // `invalidate()` is the public, non-deprecated way to ask the
    // proxy to re-evaluate filterAcceptsRow for every row. It clears
    // the internal mapping and emits `layoutChanged`, which our
    // constructor wires to `countChanged`. The deprecated
    // `invalidateFilter()` / `invalidateRowsFilter()` and the
    // protected `begin/endFilterChange()` pair both produced no
    // observable signal in Qt 6.7 (verified via test 87).
    invalidate();
}

void PluginCatalogProxyModel::setQuery(QString const& q) {
    QString const normalized = q.toLower();
    if (normalized == m_query) {
        return;
    }
    m_query = normalized;
    emit queryChanged();
    // `invalidate()` is the public, non-deprecated way to ask the
    // proxy to re-evaluate filterAcceptsRow for every row. It clears
    // the internal mapping and emits `layoutChanged`, which our
    // constructor wires to `countChanged`. The deprecated
    // `invalidateFilter()` / `invalidateRowsFilter()` and the
    // protected `begin/endFilterChange()` pair both produced no
    // observable signal in Qt 6.7 (verified via test 87).
    invalidate();
}

bool PluginCatalogProxyModel::filterAcceptsRow(int sourceRow,
                                               QModelIndex const& sourceParent) const {
    QAbstractItemModel const* src = sourceModel();
    if (src == nullptr) {
        // No source attached yet — accept everything so the proxy
        // behaves as an identity passthrough. The first real call to
        // setSourceModel() will trigger a modelReset that re-evaluates.
        return true;
    }

    QModelIndex const idx = src->index(sourceRow, 0, sourceParent);
    if (!idx.isValid()) {
        return false;
    }

    // 1. Tab filter. Mirrors PluginStore.qml's `rowMatches` switch.
    switch (m_activeTab) {
    case AllTab:
        break; // accept all sources/install states
    case InstalledTab:
        if (!src->data(idx, PluginCatalogModel::InstalledRole).toBool()) {
            return false;
        }
        break;
    case StreamdockTab:
        if (src->data(idx, PluginCatalogModel::SourceRole).toString() !=
            QStringLiteral("streamdock")) {
            return false;
        }
        break;
    case OpenDeckTab:
        if (src->data(idx, PluginCatalogModel::SourceRole).toString() !=
            QStringLiteral("opendeck")) {
            return false;
        }
        break;
    case CommunityTab:
        if (src->data(idx, PluginCatalogModel::SourceRole).toString() !=
            QStringLiteral("community")) {
            return false;
        }
        break;
    default:
        break; // unknown tab — accept; matches QML's `default: break`.
    }

    // 2. Query filter (empty query means: tab decision wins).
    if (m_query.isEmpty()) {
        return true;
    }

    auto containsQuery = [this](QString const& s) {
        return !s.isEmpty() && s.toLower().contains(m_query);
    };

    if (containsQuery(src->data(idx, PluginCatalogModel::NameRole).toString())) {
        return true;
    }
    if (containsQuery(src->data(idx, PluginCatalogModel::DescriptionRole).toString())) {
        return true;
    }
    QStringList const tags = src->data(idx, PluginCatalogModel::TagsRole).toStringList();
    for (QString const& t : tags) {
        if (containsQuery(t)) {
            return true;
        }
    }
    return false;
}

} // namespace ajazz::app
