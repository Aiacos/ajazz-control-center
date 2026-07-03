// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_mirabox_github_catalog_fetcher.cpp
 * @brief Pure-parser coverage for MiraboxGithubCatalogFetcher — the GitHub
 *        Contents-API directory listing → CatalogEntry rows, the dir-name
 *        humaniser, and the cached-snapshot round-trip. No network / no event
 *        loop (parseUpstreamJson + humaniseDirName are static + pure).
 */
#include "mirabox_github_catalog_fetcher.hpp"

#include <QByteArray>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::CatalogEntry;
using ajazz::app::MiraboxGithubCatalogFetcher;

namespace {

// A trimmed GitHub Contents response for repos/.../contents/Plugins: a flat
// array of {name, path, type}. Mixes real dir shapes (reverse-DNS bundle dir
// + friendly name) and a stray file that must be ignored.
QByteArray const kContentsJson = R"json([
  {"name":"com.mirabox.streamdock.worldWeather.sdPlugin",
   "path":"Plugins/com.mirabox.streamdock.worldWeather.sdPlugin","type":"dir"},
  {"name":"WorldWeather","path":"Plugins/WorldWeather","type":"dir"},
  {"name":"battery","path":"Plugins/battery","type":"dir"},
  {"name":"README.md","path":"Plugins/README.md","type":"file"}
])json";

} // namespace

TEST_CASE("mirabox-github parses the GitHub Contents directory listing",
          "[plugins][catalog][mirabox-github]") {
    auto const rows = MiraboxGithubCatalogFetcher::parseUpstreamJson(kContentsJson);

    // The lone file (README.md) is dropped; only the 3 directories become rows.
    REQUIRE(rows.size() == 3);

    for (CatalogEntry const& e : rows) {
        CHECK(e.source == QStringLiteral("mirabox-github"));
        CHECK(e.compatibility == QStringLiteral("streamdock"));
        // Browse-only until the install path lands: no archive URL yet, but the
        // repo-relative path is stashed for the future install fetch.
        CHECK(e.downloadUrl.isEmpty());
        CHECK(e.streamdockProductId.startsWith(QStringLiteral("Plugins/")));
        CHECK_FALSE(e.uuid.isEmpty());
        CHECK_FALSE(e.name.isEmpty());
    }

    // A reverse-DNS bundle dir keeps its id (minus .sdPlugin) as the uuid; a
    // friendly dir name is namespaced under com.mirabox.github.*.
    CHECK(rows[0].uuid == QStringLiteral("com.mirabox.streamdock.worldWeather"));
    CHECK(rows[1].uuid == QStringLiteral("com.mirabox.github.worldweather"));
    CHECK(rows[2].uuid == QStringLiteral("com.mirabox.github.battery"));
}

TEST_CASE("mirabox-github humanises directory names", "[plugins][catalog][mirabox-github]") {
    // Reverse-DNS bundle dir → last segment, camelCase split, title-cased.
    CHECK(MiraboxGithubCatalogFetcher::humaniseDirName(QStringLiteral(
              "com.mirabox.streamdock.worldWeather.sdPlugin")) == QStringLiteral("World Weather"));
    // Friendly PascalCase dir.
    CHECK(MiraboxGithubCatalogFetcher::humaniseDirName(QStringLiteral("StreamdockSwitchAudio")) ==
          QStringLiteral("Streamdock Switch Audio"));
    // Single lower-case word.
    CHECK(MiraboxGithubCatalogFetcher::humaniseDirName(QStringLiteral("battery")) ==
          QStringLiteral("Battery"));
    // Separator-delimited.
    CHECK(MiraboxGithubCatalogFetcher::humaniseDirName(QStringLiteral("color-picker")) ==
          QStringLiteral("Color Picker"));
}

TEST_CASE("mirabox-github round-trips a cached snapshot", "[plugins][catalog][mirabox-github]") {
    // The cached-snapshot shape ({rows:[...]}) must parse back to equivalent
    // rows so an offline relaunch repopulates the tab.
    QByteArray const snapshot = R"json({
      "schemaVersion": 1,
      "fetchedAtUnixMs": 123,
      "sourceUrl": "https://api.github.com/...",
      "rows": [
        {"uuid":"com.mirabox.streamdock.weather","name":"Weather",
         "version":"","author":"MiraboxSpace","description":"d",
         "iconUrl":"qrc:/x.svg","category":"Mirabox","tags":[],
         "compatibility":"streamdock","sizeBytes":"-",
         "source":"mirabox-github","repoPath":"Plugins/Weather"}
      ]
    })json";
    auto const rows = MiraboxGithubCatalogFetcher::parseUpstreamJson(snapshot);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].uuid == QStringLiteral("com.mirabox.streamdock.weather"));
    CHECK(rows[0].name == QStringLiteral("Weather"));
    CHECK(rows[0].source == QStringLiteral("mirabox-github"));
    CHECK(rows[0].streamdockProductId == QStringLiteral("Plugins/Weather"));
}

TEST_CASE("mirabox-github tolerates malformed input", "[plugins][catalog][mirabox-github]") {
    CHECK(MiraboxGithubCatalogFetcher::parseUpstreamJson(QByteArray{}).empty());
    CHECK(MiraboxGithubCatalogFetcher::parseUpstreamJson(QByteArray{"not json"}).empty());
    CHECK(MiraboxGithubCatalogFetcher::parseUpstreamJson(QByteArray{"{}"}).empty());
}
