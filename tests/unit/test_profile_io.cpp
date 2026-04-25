// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_profile_io.cpp
 * @brief Unit tests for the atomic profile I/O layer (readProfileFromDisk /
 *        writeProfileToDisk / validateProfileJson).
 */
#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_io.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Build a unique scratch directory under the temp dir; returns its path.
std::filesystem::path scratchDir(std::string_view tag) {
    auto base =
        std::filesystem::temp_directory_path() /
        ("ajazz-profile-io-" + std::string{tag} + "-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(base);
    return base;
}

/// Read an entire text file into a string. Uses a local scope so the underlying
/// std::ifstream is closed before the caller may delete the file (Windows holds an
/// exclusive lock on open handles).
std::string slurpFile(std::filesystem::path const& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ajazz::core::Profile sampleProfile() {
    ajazz::core::Profile p{};
    p.id = "11112222-3333-4444-5555-666677778888";
    p.name = "Test Profile";
    p.deviceCodename = "akp03";
    return p;
}

} // namespace

TEST_CASE("validateProfileJson rejects empty / malformed input", "[profile_io]") {
    using ajazz::core::validateProfileJson;
    REQUIRE_FALSE(validateProfileJson("").empty());
    REQUIRE_FALSE(validateProfileJson("   ").empty());
    REQUIRE_FALSE(validateProfileJson("[]").empty());
    REQUIRE_FALSE(validateProfileJson(R"({"name":"x"})").empty());
}

TEST_CASE("validateProfileJson accepts a profileToJson() document", "[profile_io]") {
    auto const json = ajazz::core::profileToJson(sampleProfile());
    REQUIRE(ajazz::core::validateProfileJson(json).empty());
}

TEST_CASE("writeProfileToDisk + readProfileFromDisk round-trip", "[profile_io]") {
    auto const dir = scratchDir("rt");
    auto const path = dir / "profile.json";

    auto const p = sampleProfile();
    ajazz::core::writeProfileToDisk(path, p);
    REQUIRE(std::filesystem::exists(path));

    // The file must be a complete, parseable profile JSON.
    std::string const content = slurpFile(path);
    REQUIRE(ajazz::core::validateProfileJson(content).empty());
    REQUIRE(content.find("Test Profile") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("writeProfileToDisk does not leave a partial file on rename", "[profile_io]") {
    auto const dir = scratchDir("partial");
    auto const path = dir / "profile.json";

    // Pre-populate with a "previous" valid profile.
    {
        auto p = sampleProfile();
        p.name = "OLD";
        ajazz::core::writeProfileToDisk(path, p);
    }
    // Now write a new profile with a different name.
    {
        auto p = sampleProfile();
        p.name = "NEW";
        ajazz::core::writeProfileToDisk(path, p);
    }

    std::string const content = slurpFile(path);
    REQUIRE(content.find("NEW") != std::string::npos);
    REQUIRE(content.find("OLD") == std::string::npos);

    // No leftover *.tmp.* siblings.
    int leftover = 0;
    for (auto const& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().filename().string().find(".tmp.") != std::string::npos) {
            ++leftover;
        }
    }
    REQUIRE(leftover == 0);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("concurrent writers always leave a valid file", "[profile_io][concurrency]") {
    auto const dir = scratchDir("concur");
    auto const path = dir / "profile.json";

    constexpr int kThreads = 8;
    constexpr int kIters = 32;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    std::atomic<int> failures{0};
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < kIters; ++i) {
                try {
                    auto p = sampleProfile();
                    p.name = "T" + std::to_string(t) + "-" + std::to_string(i);
                    ajazz::core::writeProfileToDisk(path, p);
                } catch (...) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    REQUIRE(failures.load() == 0);

    std::string const content = slurpFile(path);
    REQUIRE(ajazz::core::validateProfileJson(content).empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
