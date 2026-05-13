// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_catalog_proxy_model.cpp
 * @brief Unit tests for @ref ajazz::app::PluginCatalogProxyModel.
 *
 * The tests use a hand-rolled MockCatalogSource (a minimal
 * QAbstractListModel matching the role layout of PluginCatalogModel)
 * so we can populate deterministic rows across every (source,
 * installed) combination without depending on the real mock fixture's
 * exact content.
 */
#include "plugin_catalog_model.hpp" // for PluginCatalogModel::Roles
#include "plugin_catalog_proxy_model.hpp"
#include "qt_app_fixture.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::PluginCatalogModel;
using ajazz::app::PluginCatalogProxyModel;
using ajazz::tests::qtApp;

namespace {

/// Minimal source model mirroring the subset of PluginCatalogModel's role
/// layout that PluginCatalogProxyModel actually reads
/// (Name/Description/Tags/Source/Installed).
class MockCatalogSource : public QAbstractListModel {
public:
    struct Row {
        QString name;
        QString description;
        QStringList tags;
        QString source;
        bool installed = false;
    };

    explicit MockCatalogSource(std::vector<Row> rows, QObject* parent = nullptr)
        : QAbstractListModel(parent), m_rows(std::move(rows)) {}

    int rowCount(QModelIndex const& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    QVariant data(QModelIndex const& idx, int role) const override {
        if (!idx.isValid() || idx.row() < 0 || idx.row() >= rowCount()) {
            return {};
        }
        Row const& r = m_rows[static_cast<std::size_t>(idx.row())];
        switch (role) {
        case PluginCatalogModel::NameRole:
            return r.name;
        case PluginCatalogModel::DescriptionRole:
            return r.description;
        case PluginCatalogModel::TagsRole:
            return r.tags;
        case PluginCatalogModel::SourceRole:
            return r.source;
        case PluginCatalogModel::InstalledRole:
            return r.installed;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {PluginCatalogModel::NameRole, "name"},
            {PluginCatalogModel::DescriptionRole, "description"},
            {PluginCatalogModel::TagsRole, "tags"},
            {PluginCatalogModel::SourceRole, "source"},
            {PluginCatalogModel::InstalledRole, "installed"},
        };
    }

private:
    std::vector<Row> m_rows;
};

/// Seven rows: 2 streamdock, 2 opendeck, 2 community, 1 local. One row
/// in each of streamdock/community is marked installed so we can also
/// test the InstalledTab cross-cut against the source filter.
std::vector<MockCatalogSource::Row> makeFixture() {
    return {
        {"AJAZZ Clock", "Show the time on a key.", {"clock", "time"}, "streamdock", false},
        {"AJAZZ Weather", "Display current weather.", {"weather"}, "streamdock", true},
        {"OBS Mute", "Mute an OBS audio source.", {"obs", "audio"}, "opendeck", false},
        {"Discord PTT", "Push-to-talk for Discord.", {"discord", "voice"}, "opendeck", false},
        {"Hue Scene", "Cycle Philips Hue scenes.", {"hue", "lighting"}, "community", false},
        {"VLC Toggle", "Play/pause VLC playback.", {"vlc", "media"}, "community", true},
        {"System Monitor", "CPU/mem tiles.", {"cpu", "monitor"}, "local", false},
    };
}

} // namespace

TEST_CASE("PluginCatalogProxyModel: identity passthrough with no filter",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    REQUIRE(proxy.activeTab() == PluginCatalogProxyModel::AllTab);
    REQUIRE(proxy.query().isEmpty());
    REQUIRE(proxy.count() == 7);
}

TEST_CASE("PluginCatalogProxyModel: StreamdockTab filters by source", "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    proxy.setActiveTab(PluginCatalogProxyModel::StreamdockTab);

    REQUIRE(proxy.count() == 2);
    for (int i = 0; i < proxy.count(); ++i) {
        REQUIRE(proxy.data(proxy.index(i, 0), PluginCatalogModel::SourceRole).toString() ==
                "streamdock");
    }
}

TEST_CASE("PluginCatalogProxyModel: OpenDeckTab and CommunityTab partition the rest",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    proxy.setActiveTab(PluginCatalogProxyModel::OpenDeckTab);
    REQUIRE(proxy.count() == 2);

    proxy.setActiveTab(PluginCatalogProxyModel::CommunityTab);
    REQUIRE(proxy.count() == 2);
}

TEST_CASE("PluginCatalogProxyModel: InstalledTab filters by installed flag",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    proxy.setActiveTab(PluginCatalogProxyModel::InstalledTab);

    REQUIRE(proxy.count() == 2);
    for (int i = 0; i < proxy.count(); ++i) {
        REQUIRE(proxy.data(proxy.index(i, 0), PluginCatalogModel::InstalledRole).toBool());
    }
}

TEST_CASE("PluginCatalogProxyModel: query matches name case-insensitively",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    proxy.setQuery("CLOCK");

    REQUIRE(proxy.count() == 1);
    REQUIRE(proxy.data(proxy.index(0, 0), PluginCatalogModel::NameRole).toString() ==
            "AJAZZ Clock");
}

TEST_CASE("PluginCatalogProxyModel: query matches description and tags too",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    // "audio" appears in OBS Mute's tags list, not in any name or description.
    proxy.setQuery("audio");
    REQUIRE(proxy.count() == 1);
    REQUIRE(proxy.data(proxy.index(0, 0), PluginCatalogModel::NameRole).toString() == "OBS Mute");

    // "playback" appears in VLC's description.
    proxy.setQuery("playback");
    REQUIRE(proxy.count() == 1);
    REQUIRE(proxy.data(proxy.index(0, 0), PluginCatalogModel::NameRole).toString() == "VLC Toggle");
}

TEST_CASE("PluginCatalogProxyModel: tab and query compose by intersection",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    // Two rows have source="streamdock"; only one matches "weather".
    proxy.setActiveTab(PluginCatalogProxyModel::StreamdockTab);
    proxy.setQuery("weather");

    REQUIRE(proxy.count() == 1);
    REQUIRE(proxy.data(proxy.index(0, 0), PluginCatalogModel::NameRole).toString() ==
            "AJAZZ Weather");

    // OpenDeck tab combined with "weather" matches zero rows.
    proxy.setActiveTab(PluginCatalogProxyModel::OpenDeckTab);
    REQUIRE(proxy.count() == 0);
}

TEST_CASE("PluginCatalogProxyModel: countChanged fires when filter changes",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    QSignalSpy spy(&proxy, &PluginCatalogProxyModel::countChanged);
    proxy.setActiveTab(PluginCatalogProxyModel::StreamdockTab);
    // The proxy emits rowsInserted/rowsRemoved as side effects of the
    // filter change; both are connected to countChanged. We just need
    // at least one fire to know the property updates.
    REQUIRE(spy.count() >= 1);
}

TEST_CASE("PluginCatalogProxyModel: idempotent setQuery emits no spurious signal",
          "[plugin_catalog_proxy]") {
    qtApp();
    MockCatalogSource src(makeFixture());
    PluginCatalogProxyModel proxy;
    proxy.setSourceModel(&src);

    proxy.setQuery("vlc");
    QSignalSpy querySpy(&proxy, &PluginCatalogProxyModel::queryChanged);

    // setQuery normalises to lowercase before comparing, so "VLC" maps
    // to the existing "vlc" — no change, no signal.
    proxy.setQuery("VLC");
    REQUIRE(querySpy.count() == 0);
}

TEST_CASE("PluginCatalogProxyModel: no source attached behaves as identity",
          "[plugin_catalog_proxy]") {
    qtApp();
    PluginCatalogProxyModel proxy;
    // Even with active filter, no rows means no count.
    proxy.setActiveTab(PluginCatalogProxyModel::StreamdockTab);
    proxy.setQuery("nope");
    REQUIRE(proxy.count() == 0);
}
