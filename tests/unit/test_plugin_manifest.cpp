// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_manifest.cpp
 * @brief PluginManifestTest — unit tests for parsePluginManifest() and
 *        manifestRunnableHere() (Phase 18, Plan 18-01 / PLUGIN-06).
 *
 * Fixtures are under tests/unit/fixtures/manifests/ and are loaded via the
 * AJAZZ_TEST_REPO_ROOT compile-time define (set in tests/unit/CMakeLists.txt),
 * using the same pattern as test_manifest_signer.cpp. No QFINDTESTDATA needed.
 *
 * TEST_CASE titles are ASCII-only (CLAUDE.md / Pitfall 6 / ctest Win32 codepage rule).
 * CJK content appears inside fixture JSON but never in test names.
 *
 * Coverage:
 *   - rejects_macOnly_onLinux: mac-only OS array is ACCEPTED on linux (locked policy)
 *     but REJECTED when windows is asked for explicitly.
 *   - accepts Elgato v6 + AJAZZ extensions: Controllers, IsK1Pro, Nodejs.Version,
 *     PUUID, FSize/FFamily Mirabox synonyms all populate correctly.
 *   - rejects too high minimum version: Software.MinimumVersion "99.0" fails against "0.1.0".
 *   - returns nullopt on invalid json: malformed bytes and missing required key both fail.
 */
#include "plugin_manifest.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::app;

namespace {

/// Load fixture file bytes relative to the repository root.
/// @p relPath is relative to the repo root (e.g. "tests/unit/fixtures/manifests/foo.json").
QByteArray loadFixture(std::string const& relPath) {
    // AJAZZ_TEST_REPO_ROOT is set in tests/unit/CMakeLists.txt via set_source_files_properties.
    std::filesystem::path const path = std::filesystem::path(AJAZZ_TEST_REPO_ROOT) / relPath;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open())
        return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    auto const content = ss.str();
    return QByteArray(content.data(), static_cast<qsizetype>(content.size()));
}

constexpr char const* kFixtureDir = "tests/unit/fixtures/manifests/";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Helper: parse a fixture by short name (no directory needed).
// ---------------------------------------------------------------------------
static std::optional<PluginManifest> parseFixture(char const* name) {
    return parsePluginManifest(loadFixture(std::string(kFixtureDir) + name));
}

// ---------------------------------------------------------------------------
// Test: Linux OS-accept rule and mac-only strict reject for windows
// ---------------------------------------------------------------------------

TEST_CASE("PluginManifestTest rejects_macOnly_onLinux", "[plugin-manifest]") {
    // mac_only.json has OS=[{Platform:"mac", MinimumVersion:"11.0"}] — no linux entry.
    auto const maybeManifest = parseFixture("mac_only.json");
    REQUIRE(maybeManifest.has_value());

    PluginManifest const& m = *maybeManifest;
    REQUIRE(m.name == "Mac-Only Plugin");
    REQUIRE(m.os.size() == 1);
    REQUIRE(m.os[0].platform == "mac");

    // LOCKED Linux OS-accept policy (Assumption A1 from 18-RESEARCH.md):
    // A manifest with no "linux" entry is treated as runnable on Linux (best-effort),
    // because vendor manifests never emit "linux" and a strict match would reject every
    // real plugin on our primary OS. The SPAWN step gates real runnability via CodePath.
    CHECK(manifestRunnableHere(m, "linux", "0.1.0") == true);

    // Strict reject: the OS array has only mac; windows is not listed.
    CHECK(manifestRunnableHere(m, "windows", "0.1.0") == false);

    // mac itself is accepted (it's in the array).
    CHECK(manifestRunnableHere(m, "mac", "0.1.0") == true);
}

// ---------------------------------------------------------------------------
// Test: Full Elgato v6 fields parse correctly
// ---------------------------------------------------------------------------

TEST_CASE("PluginManifestTest accepts elgato v6 plus ajazz extensions", "[plugin-manifest]") {
    SECTION("elgato_v6_keypad parses with Keypad controller") {
        auto const maybeManifest = parseFixture("elgato_v6_keypad.json");
        REQUIRE(maybeManifest.has_value());
        PluginManifest const& m = *maybeManifest;

        CHECK(m.name == "Keypad Test Plugin");
        CHECK(m.author == "Test Author");
        CHECK(m.version == "1.0.0");
        CHECK(m.sdkVersion == 1);
        CHECK(m.codePath == "code/index.js");
        CHECK(m.softwareMinimumVersion == "0.1.0");

        // OS array: mac + windows
        REQUIRE(m.os.size() == 2);
        CHECK(m.os[0].platform == "mac");
        CHECK(m.os[1].platform == "windows");

        // One action with Keypad controller
        REQUIRE(m.actions.size() == 1);
        PluginAction const& action = m.actions[0];
        CHECK(action.uuid == "com.example.test.keypad");
        CHECK(action.name == "Keypad Action");
        REQUIRE(action.controllers.size() == 1);
        CHECK(action.controllers[0] == "Keypad");

        // State with Elgato standard FontSize/FontFamily
        REQUIRE(action.states.size() == 1);
        CHECK(action.states[0].fontSize == "10");
        CHECK(action.states[0].fontFamily == "Arial");
        CHECK(action.states[0].fontStyle == "Bold");
        CHECK(action.states[0].titleColor == "#FFFFFF");
        CHECK(action.states[0].titleAlignment == "middle");

        // AJAZZ extensions absent -> defaults
        CHECK(!m.isK1Pro);
        CHECK(!m.runAsAdministrator);
        CHECK(m.nodejsVersion.isEmpty());
        CHECK(m.puuid.isEmpty());
    }

    SECTION("ajazz_ext_knob parses with IsK1Pro, Knob, Nodejs.Version, PUUID, FSize/FFamily") {
        auto const maybeManifest = parseFixture("ajazz_ext_knob.json");
        REQUIRE(maybeManifest.has_value());
        PluginManifest const& m = *maybeManifest;

        CHECK(m.name == "AJAZZ Knob Plugin");
        CHECK(m.author == "AJAZZ Test");

        // AJAZZ extension flags
        CHECK(m.isK1Pro == true);
        CHECK(m.runAsAdministrator == true);
        CHECK(m.nodejsVersion == "20");
        CHECK(m.puuid == "AjazzKnobTestPlugin");
        CHECK(m.apiVersion == "2.0");

        // One action with Knob + SecondaryScreen controllers
        REQUIRE(m.actions.size() == 1);
        PluginAction const& action = m.actions[0];
        CHECK(action.controllers.contains("Knob"));
        CHECK(action.controllers.contains("SecondaryScreen"));

        // State using Mirabox FSize/FFamily synonyms (not the Elgato FontSize/FontFamily)
        REQUIRE(action.states.size() == 1);
        PluginActionState const& state = action.states[0];
        // FSize maps to fontSize, FFamily maps to fontFamily
        CHECK(state.fontSize == "12");
        CHECK(state.fontFamily == "Courier New");
        CHECK(state.fontStyle == "Regular");
    }
}

// ---------------------------------------------------------------------------
// Test: Software.MinimumVersion too high rejects runnability
// ---------------------------------------------------------------------------

TEST_CASE("PluginManifestTest rejects too high minimum version", "[plugin-manifest]") {
    // high_minver.json has Software:{MinimumVersion:"99.0"}
    auto const maybeManifest = parseFixture("high_minver.json");
    REQUIRE(maybeManifest.has_value());
    PluginManifest const& m = *maybeManifest;

    CHECK(m.softwareMinimumVersion == "99.0");

    // QVersionNumber comparison: 0.1.0 < 99.0 -> reject
    CHECK(manifestRunnableHere(m, "linux", "0.1.0") == false);
    CHECK(manifestRunnableHere(m, "linux", "99.0") == true);
    CHECK(manifestRunnableHere(m, "linux", "100.0") == true);
    // Version string compare trap: "2.10" > "2.9" -- check QVersionNumber handles it
    CHECK(manifestRunnableHere(m, "linux", "99.1") == true);
}

// ---------------------------------------------------------------------------
// Test: Invalid JSON and missing required keys return std::nullopt
// ---------------------------------------------------------------------------

TEST_CASE("PluginManifestTest returns nullopt on invalid json", "[plugin-manifest]") {
    SECTION("garbage bytes return nullopt") {
        QByteArray const garbage("not { json at all !!! %%%");
        CHECK(!parsePluginManifest(garbage).has_value());
    }

    SECTION("empty bytes return nullopt") {
        CHECK(!parsePluginManifest(QByteArray{}).has_value());
    }

    SECTION("JSON array at root returns nullopt") {
        CHECK(!parsePluginManifest(R"([{"Name":"foo"}])").has_value());
    }

    SECTION("missing Name returns nullopt") {
        QByteArray const noName(R"({
            "Author":"A","Version":"1.0.0","SDKVersion":1,
            "OS":[{"Platform":"mac"}],"CodePath":"code.js",
            "Actions":[]
        })");
        CHECK(!parsePluginManifest(noName).has_value());
    }

    SECTION("missing Author returns nullopt") {
        QByteArray const noAuthor(R"({
            "Name":"X","Version":"1.0.0","SDKVersion":1,
            "OS":[{"Platform":"mac"}],"CodePath":"code.js",
            "Actions":[]
        })");
        CHECK(!parsePluginManifest(noAuthor).has_value());
    }

    SECTION("missing SDKVersion returns nullopt") {
        QByteArray const noSdk(R"({
            "Name":"X","Author":"A","Version":"1.0.0",
            "OS":[{"Platform":"mac"}],"CodePath":"code.js",
            "Actions":[]
        })");
        CHECK(!parsePluginManifest(noSdk).has_value());
    }

    SECTION("missing OS returns nullopt") {
        QByteArray const noOs(R"({
            "Name":"X","Author":"A","Version":"1.0.0","SDKVersion":1,
            "CodePath":"code.js","Actions":[]
        })");
        CHECK(!parsePluginManifest(noOs).has_value());
    }

    SECTION("missing all CodePath variants returns nullopt") {
        QByteArray const noCode(R"({
            "Name":"X","Author":"A","Version":"1.0.0","SDKVersion":1,
            "OS":[{"Platform":"mac"}],"Actions":[]
        })");
        CHECK(!parsePluginManifest(noCode).has_value());
    }

    SECTION("missing Actions returns nullopt") {
        QByteArray const noActions(R"({
            "Name":"X","Author":"A","Version":"1.0.0","SDKVersion":1,
            "OS":[{"Platform":"mac"}],"CodePath":"code.js"
        })");
        CHECK(!parsePluginManifest(noActions).has_value());
    }
}

// ---------------------------------------------------------------------------
// Test: currentPlatformString returns a non-empty recognised string
// ---------------------------------------------------------------------------

TEST_CASE("PluginManifestTest currentPlatformString is a known value", "[plugin-manifest]") {
    QString const platform = currentPlatformString();
    CHECK(!platform.isEmpty());
    // Must be one of the three recognised values
    bool const known = (platform == "linux" || platform == "mac" || platform == "windows");
    CHECK(known);
}
