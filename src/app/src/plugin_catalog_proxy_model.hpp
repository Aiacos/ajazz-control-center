// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_catalog_proxy_model.hpp
 * @brief Filtered view of @ref PluginCatalogModel exposed to QML.
 *
 * PluginStore.qml used to apply the tab + search filter inside the
 * GridView delegate by setting non-matching tiles to `visible: false;
 * width: 0; height: 0`. That approach has two problems:
 *
 *   * GridView's `cellWidth`/`cellHeight` are fixed grid coordinates,
 *     so zero-sized non-matching tiles still occupy a slot — visible
 *     tiles appear in sparse positions with phantom gaps.
 *   * The empty-state predicate (`grid.visibleCount === 0`) was
 *     maintained by per-delegate increment/decrement of a QML int,
 *     which drifts when GridView recycles delegates.
 *
 * Pushing the filter into a `QSortFilterProxyModel` collapses both
 * problems into "the proxy emits only matching rows," and `count`
 * becomes the single source of truth.
 *
 * Filter inputs:
 *   * @c activeTab — int matching the page tab order
 *     (0=All, 1=Installed, 2=Streamdock, 3=OpenDeck, 4=Community).
 *   * @c query — case-insensitive substring matched against
 *     @c NameRole, @c DescriptionRole and any string in @c TagsRole.
 *
 * The proxy is declared as a QML named element so PluginStore.qml can
 * instantiate it inline scoped to the page — the filter state is
 * page-local (the singleton @ref PluginCatalogModel is app-wide).
 */
#pragma once

#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQmlIntegration>

namespace ajazz::app {

class PluginCatalogProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(PluginCatalogProxy)

    /// Active tab index. Mirrors PluginStore.qml's `root.activeTab`.
    Q_PROPERTY(int activeTab READ activeTab WRITE setActiveTab NOTIFY activeTabChanged)

    /// Case-insensitive search string. Lower-cased on set so the
    /// per-row comparison in @ref filterAcceptsRow is a plain
    /// substring lookup, not a case-folded compare per check.
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)

    /// Row count after filtering. Drives the page's empty-state
    /// predicate; updates whenever the proxy emits @c rowsInserted,
    /// @c rowsRemoved or @c modelReset.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    /// Tab vocabulary aligned with PluginStore.qml's TabBar order.
    enum Tab {
        AllTab = 0,
        InstalledTab = 1,
        StreamdockTab = 2,
        OpenDeckTab = 3,
        CommunityTab = 4,
    };
    Q_ENUM(Tab)

    explicit PluginCatalogProxyModel(QObject* parent = nullptr);

    [[nodiscard]] int activeTab() const noexcept { return m_activeTab; }
    void setActiveTab(int tab);

    [[nodiscard]] QString query() const noexcept { return m_query; }
    void setQuery(QString const& q);

    [[nodiscard]] int count() const { return rowCount(); }

signals:
    void activeTabChanged();
    void queryChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const override;

private:
    int m_activeTab = AllTab;
    QString m_query; ///< Already-lowercased so filterAcceptsRow is fast.
};

} // namespace ajazz::app
