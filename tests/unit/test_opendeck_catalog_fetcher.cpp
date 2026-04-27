// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_opendeck_catalog_fetcher.cpp
 * @brief Unit tests for @ref ajazz::app::OpenDeckCatalogFetcher.
 *
 * Sister of @ref test_streamdock_catalog_fetcher.cpp. Exercises the
 * pure parser (`parseUpstreamJson`) against both the live shape (a
 * flat JSON array of plugin entries, the OpenDeck community mirror
 * format) and the cached-snapshot envelope, plus the on-disk cache
 * round-trip and the bundled fallback resource. Avoids
 * @c QNetworkAccessManager so the suite runs in the lightweight
 * `ajazz_unit_tests` binary alongside everything else.
 */
#include "opendeck_catalog_fetcher.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::CatalogEntry;
using ajazz::app::OpenDeckCatalogFetcher;

namespace {

/// Boot a single QCoreApplication for the suite. Defensive against the
/// streamdock test (its own qtApp() also instantiates one); whichever
/// runs first wins, the other reuses the singleton via
/// QCoreApplication::instance().
QCoreApplication& qtApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QCoreApplication app{argc, argv};
    }
    QCoreApplication::setApplicationName(QStringLiteral("ajazz-control-center-tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("Aiacos"));
    return *QCoreApplication::instance();
}

CatalogEntry const* findByUuid(std::vector<CatalogEntry> const& rows, QString const& uuid) {
    auto const it = std::find_if(
        rows.begin(), rows.end(), [&](CatalogEntry const& e) { return e.uuid == uuid; });
    return it == rows.end() ? nullptr : &*it;
}

/// Hand-crafted OpenDeck wire shape: a flat JSON array, no envelope,
/// per the upstream `https://plugins.amankhanna.me/catalogue.json`.
/// Three entries cover (1) full metadata + link, (2) minimal valid
/// (no link), and (3) deliberately invalid (missing required name).
QByteArray sampleLiveResponse() {
    return R"([
      {
        "id": "com.barraider.soundpad",
        "name": "Soundpad Integration",
        "author": "BarRaider",
        "version": "1.6",
        "link": "https://barraider.github.io/",
        "download": "https://appstore.elgato.com/.../soundpad.streamDeckPlugin",
        "icon": "https://appstore.elgato.com/.../soundpad.png"
      },
      {
        "id": "com.example.minimal",
        "name": "Minimal Plugin",
        "version": "0.1.0"
      },
      {
        "id": "",
        "name": "Missing ID — must be dropped",
        "version": "0.0.0"
      },
      {
        "id": "com.example.no-name",
        "version": "0.0.0"
      }
    ])";
}

/// Cached-snapshot shape produced by writeCache(); minimal but valid.
QByteArray sampleCachedSnapshot() {
    return R"({
      "schemaVersion": 1,
      "fetchedAtUnixMs": 1714000000000,
      "sourceUrl": "https://plugins.amankhanna.me/catalogue.json",
      "rows": [
        {
          "uuid": "com.opendeck.cached",
          "name": "Cached Plugin",
          "version": "1.0.0",
          "author": "Cache",
          "description": "From disk",
          "iconUrl": "qrc:/qt/qml/AjazzControlCenter/icons/app.svg",
          "compatibility": "opendeck",
          "source": "opendeck",
          "tags": ["stream-deck", "opendeck", "cached"]
        }
      ]
    })";
}

} // namespace

TEST_CASE("OpenDeck parseUpstreamJson translates the live array shape", "[opendeck][parse]") {
    qtApp();
    auto const rows = OpenDeckCatalogFetcher::parseUpstreamJson(sampleLiveResponse());

    // 4 entries in the fixture, but two are invalid (empty `id` and
    // missing `name`) so they get dropped. Soundpad + Minimal survive.
    REQUIRE(rows.size() == 2);

    auto const* sp = findByUuid(rows, QStringLiteral("com.barraider.soundpad"));
    REQUIRE(sp != nullptr);
    REQUIRE(sp->name == QStringLiteral("Soundpad Integration"));
    REQUIRE(sp->version == QStringLiteral("1.6"));
    REQUIRE(sp->author == QStringLiteral("BarRaider"));
    REQUIRE(sp->source == QStringLiteral("opendeck"));
    REQUIRE(sp->compatibility == QStringLiteral("opendeck"));
    REQUIRE(sp->iconUrl.toString() ==
            QStringLiteral("https://appstore.elgato.com/.../soundpad.png"));
    // Description is synthesised from the `link` field.
    REQUIRE(sp->description ==
            QStringLiteral("Stream Deck plugin from https://barraider.github.io/."));
    // Every translated row gets the canonical OpenDeck tags.
    REQUIRE(sp->tags.contains(QStringLiteral("stream-deck")));
    REQUIRE(sp->tags.contains(QStringLiteral("opendeck")));
    // Devices is empty by design — see translateEntry's comment.
    REQUIRE(sp->devices.isEmpty());
    REQUIRE_FALSE(sp->verified);

    // Minimal entry: no link → fallback description, no icon → empty url.
    auto const* mn = findByUuid(rows, QStringLiteral("com.example.minimal"));
    REQUIRE(mn != nullptr);
    REQUIRE(mn->description == QStringLiteral("Stream Deck plugin from the OpenDeck archive."));
    REQUIRE(mn->iconUrl.isEmpty());
    REQUIRE(mn->author.isEmpty());
}

TEST_CASE("OpenDeck parseUpstreamJson accepts the cached-snapshot envelope shape",
          "[opendeck][parse]") {
    qtApp();
    auto const rows = OpenDeckCatalogFetcher::parseUpstreamJson(sampleCachedSnapshot());
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().uuid == QStringLiteral("com.opendeck.cached"));
    REQUIRE(rows.front().compatibility == QStringLiteral("opendeck"));
    REQUIRE(rows.front().tags.size() == 3);
}

TEST_CASE("OpenDeck parseUpstreamJson returns empty on malformed input", "[opendeck][parse]") {
    qtApp();
    REQUIRE(OpenDeckCatalogFetcher::parseUpstreamJson(QByteArray("not json")).empty());
    REQUIRE(OpenDeckCatalogFetcher::parseUpstreamJson(QByteArray("{}")).empty());
    REQUIRE(OpenDeckCatalogFetcher::parseUpstreamJson(QByteArray("[]")).empty());
    REQUIRE(OpenDeckCatalogFetcher::parseUpstreamJson(QByteArray()).empty());
    // An array of non-objects (the live shape requires plugin objects)
    // must yield zero rows, not a parse error.
    REQUIRE(OpenDeckCatalogFetcher::parseUpstreamJson(QByteArray("[1, 2, 3]")).empty());
}

TEST_CASE("OpenDeck defaultCatalogUrl points at the upstream community mirror",
          "[opendeck][config]") {
    qtApp();
    auto const url = OpenDeckCatalogFetcher::defaultCatalogUrl();
    REQUIRE(url.isValid());
    REQUIRE(url.scheme() == QStringLiteral("https"));
    REQUIRE(url.host() == QStringLiteral("plugins.amankhanna.me"));
    REQUIRE(url.path() == QStringLiteral("/catalogue.json"));
}

TEST_CASE("OpenDeck cacheFilePath honours the directory override", "[opendeck][cache]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    OpenDeckCatalogFetcher fetcher;
    fetcher.setCacheDirOverride(tmp.path());
    auto const path = fetcher.cacheFilePath();
    REQUIRE(path.startsWith(tmp.path()));
    REQUIRE(path.endsWith(QStringLiteral("opendeck-catalog.json")));
}

TEST_CASE("OpenDeck snapshot survives a round-trip through the on-disk cache",
          "[opendeck][cache]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    OpenDeckCatalogFetcher fetcher;
    fetcher.setCacheDirOverride(tmp.path());

    // Seed the cache by writing the snapshot envelope directly. The
    // parser accepts both shapes (flat array AND envelope), but
    // `loadFromCache` specifically requires the envelope so it can
    // recover `fetchedAtUnixMs` / `sourceUrl`.
    QFile out{fetcher.cacheFilePath()};
    QDir().mkpath(tmp.path());
    REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(sampleCachedSnapshot());
    out.close();

    auto const snap = fetcher.loadFromCache();
    REQUIRE(snap.rows.size() == 1);
    REQUIRE(snap.rows.front().uuid == QStringLiteral("com.opendeck.cached"));
    REQUIRE(snap.state == OpenDeckCatalogFetcher::State::Cached);
    REQUIRE(snap.fetchedAtUnixMs == 1714000000000LL);
    REQUIRE(snap.sourceUrl == QStringLiteral("https://plugins.amankhanna.me/catalogue.json"));
}

TEST_CASE("OpenDeck loadFromCache returns an empty snapshot when the file is missing",
          "[opendeck][cache]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    OpenDeckCatalogFetcher fetcher;
    fetcher.setCacheDirOverride(tmp.path());

    auto const snap = fetcher.loadFromCache();
    REQUIRE(snap.rows.empty());
    REQUIRE(snap.state == OpenDeckCatalogFetcher::State::Idle);
}

TEST_CASE("OpenDeck bundled fallback resource yields a non-empty snapshot",
          "[opendeck][fallback]") {
    qtApp();
    auto const fallback = OpenDeckCatalogFetcher::loadBundledFallback();
    REQUIRE_FALSE(fallback.rows.empty());
    REQUIRE(fallback.state == OpenDeckCatalogFetcher::State::Offline);
    // Every fallback row must declare itself as an opendeck-source row
    // so PluginCatalogModel routes it under the OpenDeck tab.
    for (auto const& row : fallback.rows) {
        REQUIRE(row.source == QStringLiteral("opendeck"));
        REQUIRE(row.compatibility == QStringLiteral("opendeck"));
        REQUIRE_FALSE(row.uuid.isEmpty());
        REQUIRE_FALSE(row.name.isEmpty());
    }
}
