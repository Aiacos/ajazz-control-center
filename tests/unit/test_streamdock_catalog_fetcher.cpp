// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_streamdock_catalog_fetcher.cpp
 * @brief Unit tests for @ref ajazz::app::StreamdockCatalogFetcher.
 *
 * Exercises the pure helpers (parseUpstreamJson, mapUpstreamDevices,
 * humaniseSize, deriveUuid) plus the on-disk cache round-trip and the
 * bundled offline fallback. The tests deliberately avoid spinning up
 * @c QNetworkAccessManager so they can run in the same lightweight
 * `ajazz_unit_tests` binary as the other unit suites.
 */
#include "qt_app_fixture.hpp"
#include "streamdock_catalog_fetcher.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::CatalogEntry;
using ajazz::app::StreamdockCatalogFetcher;
using ajazz::tests::qtApp;

namespace {

/// Look up a row by UUID; returns @c nullptr when absent.
CatalogEntry const* findByUuid(std::vector<CatalogEntry> const& rows, QString const& uuid) {
    auto const it = std::find_if(
        rows.begin(), rows.end(), [&](CatalogEntry const& e) { return e.uuid == uuid; });
    return it == rows.end() ? nullptr : &*it;
}

/// Hand-crafted live-shape sample: three records covering supported,
/// unsupported and partially-supported device sets.
QByteArray sampleLiveResponse() {
    return R"({
      "code": 200,
      "msg": "OK",
      "data": {
        "pageNum": 1,
        "totalPage": 1,
        "list": [
          {
            "id": 101,
            "name": "Spotify Now Playing",
            "author": "MiraBox",
            "version": "2.0.0",
            "overview": "Track now playing.\n\nMore details follow.",
            "headUrl": "https://cdn.example.com/spot.png",
            "size": 1572864,
            "download": "https://cdn.example.com/spot.zip",
            "types": [{"nameen": "Music"}, {"nameen": "Media"}],
            "devices": [{"deviceUuid": "293,293E"}, {"deviceUuid": "N4"}]
          },
          {
            "id": 102,
            "name": "Unsupported Toy",
            "author": "anon",
            "version": "0.1.0",
            "overview": "",
            "headUrl": "http://example.com/insecure.png",
            "size": 4096,
            "download": "https://cdn.example.com/toy.zip",
            "types": [],
            "devices": [{"deviceUuid": "ZZZ"}]
          },
          {
            "id": 103,
            "name": "Mixer Dial",
            "author": "",
            "version": "",
            "overview": "Per-app volume.",
            "headUrl": "https://cdn.example.com/mix.png",
            "size": 0,
            "download": "https://cdn.example.com/mix.zip",
            "types": [{"nameen": "Audio"}],
            "devices": [{"deviceUuid": "N4"}]
          }
        ]
      }
    })";
}

/// Cached-snapshot shape produced by writeCache(); minimal but valid.
QByteArray sampleCachedSnapshot() {
    return R"({
      "schemaVersion": 1,
      "fetchedAtUnixMs": 1714000000000,
      "sourceUrl": "https://space.key123.vip/interface/user/productInfo/list",
      "rows": [
        {
          "uuid": "com.streamdock.foo.42",
          "name": "Cached Foo",
          "version": "1.0.0",
          "author": "Cache",
          "description": "From disk",
          "iconUrl": "qrc:/qt/qml/AjazzControlCenter/icons/app.svg",
          "category": "Streaming",
          "tags": ["a", "b"],
          "devices": ["akp153"],
          "compatibility": "streamdock",
          "sizeBytes": "1.0 MB",
          "verified": false,
          "source": "streamdock",
          "streamdockProductId": "42"
        }
      ]
    })";
}

} // namespace

TEST_CASE("parseUpstreamJson translates the live response shape", "[streamdock][parse]") {
    qtApp();
    auto const rows = StreamdockCatalogFetcher::parseUpstreamJson(sampleLiveResponse());

    // Record 102 ("ZZZ") must be dropped — no supported devices.
    REQUIRE(rows.size() == 2);

    // Spotify: id 101 → uuid "com.streamdock.spotify-now-playing.101".
    auto const* spot = findByUuid(rows, QStringLiteral("com.streamdock.spotify-now-playing.101"));
    REQUIRE(spot != nullptr);
    REQUIRE(spot->name == QStringLiteral("Spotify Now Playing"));
    REQUIRE(spot->version == QStringLiteral("2.0.0"));
    REQUIRE(spot->author == QStringLiteral("MiraBox"));
    REQUIRE(spot->compatibility == QStringLiteral("streamdock"));
    REQUIRE(spot->source == QStringLiteral("streamdock"));
    REQUIRE(spot->streamdockProductId == QStringLiteral("101"));
    // Devices: 293→akp153, 293E→akp153e, N4→akp815.
    REQUIRE(spot->devices.contains(QStringLiteral("akp153")));
    REQUIRE(spot->devices.contains(QStringLiteral("akp153e")));
    REQUIRE(spot->devices.contains(QStringLiteral("akp815")));
    // Description is the first paragraph of overview.
    REQUIRE(spot->description == QStringLiteral("Track now playing."));
    // Category falls back to Streaming, then becomes the first `types[]` entry.
    REQUIRE(spot->category == QStringLiteral("Music"));

    // Mixer Dial: empty author falls back to the brand placeholder; size 0
    // becomes the em-dash.
    auto const* mix = findByUuid(rows, QStringLiteral("com.streamdock.mixer-dial.103"));
    REQUIRE(mix != nullptr);
    REQUIRE(mix->author == QStringLiteral("AJAZZ Streamdock"));
    REQUIRE(mix->sizeBytes == QString(QChar(0x2014)));
    REQUIRE(mix->devices == QStringList{QStringLiteral("akp815")});
}

TEST_CASE("parseUpstreamJson accepts the cached-snapshot shape", "[streamdock][parse]") {
    qtApp();
    auto const rows = StreamdockCatalogFetcher::parseUpstreamJson(sampleCachedSnapshot());
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().uuid == QStringLiteral("com.streamdock.foo.42"));
    REQUIRE(rows.front().streamdockProductId == QStringLiteral("42"));
    REQUIRE(rows.front().devices == QStringList{QStringLiteral("akp153")});
    REQUIRE(rows.front().tags.size() == 2);
}

TEST_CASE("parseUpstreamJson returns empty on malformed input", "[streamdock][parse]") {
    qtApp();
    REQUIRE(StreamdockCatalogFetcher::parseUpstreamJson(QByteArray("not json")).empty());
    REQUIRE(StreamdockCatalogFetcher::parseUpstreamJson(QByteArray("[]")).empty());
    REQUIRE(StreamdockCatalogFetcher::parseUpstreamJson(QByteArray()).empty());
}

TEST_CASE("mapUpstreamDevices splits comma-joined ids and drops unknowns",
          "[streamdock][devices]") {
    qtApp();
    auto const mapped = StreamdockCatalogFetcher::mapUpstreamDevices(
        {QStringLiteral("293,293E"), QStringLiteral("ZZZ"), QStringLiteral("N3")});
    REQUIRE(mapped.contains(QStringLiteral("akp153")));
    REQUIRE(mapped.contains(QStringLiteral("akp153e")));
    REQUIRE(mapped.contains(QStringLiteral("akp03")));
    REQUIRE_FALSE(mapped.contains(QStringLiteral("ZZZ")));
    REQUIRE(mapped.size() == 3); // de-duplicated

    // All-unknown → empty.
    auto const none = StreamdockCatalogFetcher::mapUpstreamDevices(
        {QStringLiteral("XYZ"), QStringLiteral("foo, bar")});
    REQUIRE(none.isEmpty());
}

TEST_CASE("humaniseSize crosses the byte / KB / MB / GB boundaries", "[streamdock][humanise]") {
    qtApp();
    // Zero / negative → em-dash.
    REQUIRE(StreamdockCatalogFetcher::humaniseSize(0.0) == QString(QChar(0x2014)));
    REQUIRE(StreamdockCatalogFetcher::humaniseSize(-1.0) == QString(QChar(0x2014)));
    // Bytes.
    REQUIRE(StreamdockCatalogFetcher::humaniseSize(512.0) == QStringLiteral("512 B"));
    // KB (>=1024).
    auto const kb = StreamdockCatalogFetcher::humaniseSize(2048.0);
    REQUIRE(kb.endsWith(QStringLiteral("KB")));
    // MB (>=1024^2).
    auto const mb = StreamdockCatalogFetcher::humaniseSize(5.0 * 1024.0 * 1024.0);
    REQUIRE(mb.endsWith(QStringLiteral("MB")));
    // GB (>=1024^3) — capped at GB.
    auto const gb = StreamdockCatalogFetcher::humaniseSize(2.5 * 1024.0 * 1024.0 * 1024.0);
    REQUIRE(gb.endsWith(QStringLiteral("GB")));
}

TEST_CASE("deriveUuid produces deterministic, slug-safe identifiers", "[streamdock][derive]") {
    qtApp();
    auto const a =
        StreamdockCatalogFetcher::deriveUuid(QStringLiteral("42"), QStringLiteral("Hello World!"));
    auto const b =
        StreamdockCatalogFetcher::deriveUuid(QStringLiteral("42"), QStringLiteral("Hello World!"));
    REQUIRE(a == b);
    REQUIRE(a == QStringLiteral("com.streamdock.hello-world.42"));

    // Special characters collapse cleanly without trailing dashes.
    auto const c = StreamdockCatalogFetcher::deriveUuid(QStringLiteral("7"),
                                                        QStringLiteral("Foo --- Bar / Baz"));
    REQUIRE(c == QStringLiteral("com.streamdock.foo-bar-baz.7"));

    // Empty / whitespace-only name falls back to the id-based shape.
    auto const d =
        StreamdockCatalogFetcher::deriveUuid(QStringLiteral("13"), QStringLiteral("   "));
    REQUIRE(d == QStringLiteral("com.streamdock.id.13"));
}

TEST_CASE("snapshot survives a round-trip through the on-disk cache", "[streamdock][cache]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    StreamdockCatalogFetcher fetcher;
    fetcher.setCacheDirOverride(tmp.path());
    REQUIRE(fetcher.cacheFilePath().startsWith(tmp.path()));

    // Seed the cache by writing the cached-snapshot shape directly. The
    // parser accepts both shapes, so this exercises the `loadFromCache()`
    // happy path without invoking the network.
    QFile out{fetcher.cacheFilePath()};
    QDir().mkpath(tmp.path());
    REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(sampleCachedSnapshot());
    out.close();

    auto const snap = fetcher.loadFromCache();
    REQUIRE(snap.rows.size() == 1);
    REQUIRE(snap.rows.front().uuid == QStringLiteral("com.streamdock.foo.42"));
    REQUIRE(snap.state == StreamdockCatalogFetcher::State::Cached);
    REQUIRE(snap.fetchedAtUnixMs == 1714000000000LL);
}

TEST_CASE("bundled fallback resource yields a non-empty snapshot", "[streamdock][fallback]") {
    qtApp();
    auto const fallback = StreamdockCatalogFetcher::loadBundledFallback();
    REQUIRE_FALSE(fallback.rows.empty());
    REQUIRE(fallback.state == StreamdockCatalogFetcher::State::Offline);
    // Every fallback row must declare itself as a streamdock-source row so
    // PluginCatalogModel routes it under the AJAZZ Streamdock tab.
    for (auto const& row : fallback.rows) {
        REQUIRE(row.source == QStringLiteral("streamdock"));
        REQUIRE(row.compatibility == QStringLiteral("streamdock"));
        REQUIRE_FALSE(row.devices.empty());
    }
}
