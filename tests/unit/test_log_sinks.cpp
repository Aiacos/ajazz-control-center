// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_log_sinks.cpp
 * @brief Unit tests for the reusable LogSink building blocks: TeeSink
 *        fan-out, RingBufferSink retention + snapshot filtering, and
 *        FileSink append/no-op-on-failure behaviour.
 *
 * These sinks back the out-of-process debug log stream, so the contracts
 * pinned here (verbatim level/module/message, bounded retention dropping
 * the oldest, level + since-timestamp filtering, silent degradation on a
 * bad path) are load-bearing for the control channel that reads them.
 */
#include "ajazz/core/log_sinks.hpp"
#include "ajazz/core/logger.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Minimal capturing sink (mirrors the one in test_logger.cpp) so a
/// TeeSink fan-out can be asserted without touching the global logger.
class CapturingSink : public ajazz::core::LogSink {
public:
    struct Record {
        ajazz::core::LogLevel level;
        std::string module;
        std::string message;
    };

    void write(ajazz::core::LogLevel level,
               std::string_view module,
               std::string_view message) noexcept override {
        std::lock_guard const lock(mutex_);
        records_.push_back({level, std::string(module), std::string(message)});
    }

    [[nodiscard]] std::vector<Record> snapshot() const {
        std::lock_guard const lock(mutex_);
        return records_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Record> records_;
};

} // namespace

TEST_CASE("TeeSink fans every record out to all children in order", "[logsinks][tee]") {
    using namespace ajazz::core;

    auto a = std::make_shared<CapturingSink>();
    auto b = std::make_shared<CapturingSink>();
    // A null child must be skipped without crashing.
    TeeSink tee({a, nullptr, b});

    tee.write(LogLevel::Info, "mod", "first");
    tee.write(LogLevel::Error, "mod", "second");

    for (auto const* child : {a.get(), b.get()}) {
        auto const recs = child->snapshot();
        REQUIRE(recs.size() == 2);
        REQUIRE(recs[0].level == LogLevel::Info);
        REQUIRE(recs[0].message == "first");
        REQUIRE(recs[1].level == LogLevel::Error);
        REQUIRE(recs[1].message == "second");
    }
}

TEST_CASE("RingBufferSink retains the newest records up to capacity", "[logsinks][ring]") {
    using namespace ajazz::core;

    RingBufferSink ring(3);
    REQUIRE(ring.capacity() == 3);

    for (int i = 0; i < 5; ++i) {
        ring.write(LogLevel::Info, "m", std::to_string(i));
    }

    auto const snap = ring.snapshot();
    REQUIRE(ring.size() == 3);
    REQUIRE(snap.size() == 3);
    // Oldest two (0, 1) dropped; 2,3,4 retained oldest-first.
    REQUIRE(snap[0].message == "2");
    REQUIRE(snap[1].message == "3");
    REQUIRE(snap[2].message == "4");
}

TEST_CASE("RingBufferSink snapshot filters by minimum level", "[logsinks][ring]") {
    using namespace ajazz::core;

    RingBufferSink ring(16);
    ring.write(LogLevel::Trace, "m", "t");
    ring.write(LogLevel::Info, "m", "i");
    ring.write(LogLevel::Warn, "m", "w");
    ring.write(LogLevel::Error, "m", "e");

    auto const warns = ring.snapshot(LogLevel::Warn);
    REQUIRE(warns.size() == 2);
    REQUIRE(warns[0].message == "w");
    REQUIRE(warns[1].message == "e");
}

TEST_CASE("RingBufferSink snapshot filters by since-timestamp cursor", "[logsinks][ring]") {
    using namespace ajazz::core;

    RingBufferSink ring(16);
    ring.write(LogLevel::Info, "m", "a");
    ring.write(LogLevel::Info, "m", "b");

    auto const all = ring.snapshot();
    REQUIRE(all.size() == 2);

    // Using the first record's own timestamp as the exclusive lower bound
    // must drop it (predicate is epochMs <= since) and keep everything
    // stamped strictly later.
    auto const after = ring.snapshot(LogLevel::Trace, all[0].epochMs);
    REQUIRE(after.size() <= 1);
    for (auto const& rec : after) {
        REQUIRE(rec.epochMs > all[0].epochMs);
    }

    // A cursor at/after the last record yields nothing.
    REQUIRE(ring.snapshot(LogLevel::Trace, all[1].epochMs).empty());
}

TEST_CASE("RingBufferSink clear empties retained records", "[logsinks][ring]") {
    using namespace ajazz::core;

    RingBufferSink ring(8);
    ring.write(LogLevel::Info, "m", "x");
    REQUIRE(ring.size() == 1);
    ring.clear();
    REQUIRE(ring.size() == 0);
    REQUIRE(ring.snapshot().empty());
}

TEST_CASE("FileSink appends records in the legacy format", "[logsinks][file]") {
    using namespace ajazz::core;

    auto const path = std::filesystem::temp_directory_path() / "ajazz_test_log_sink_appends.log";
    std::filesystem::remove(path);

    {
        FileSink sink(path.string());
        REQUIRE(sink.isOpen());
        sink.write(LogLevel::Warn, "hid", "device 7 dropped");
    }

    std::ifstream in(path);
    REQUIRE(in.good());
    std::string line;
    std::getline(in, line);

    // [<epoch>] [WARN ] [hid] device 7 dropped
    REQUIRE(line.find("[WARN ]") != std::string::npos);
    REQUIRE(line.find("[hid]") != std::string::npos);
    REQUIRE(line.find("device 7 dropped") != std::string::npos);

    // Close the reader before removing: on Windows std::filesystem::remove throws
    // "file being used by another process" while any handle (here the ifstream)
    // is still open, unlike POSIX where unlink-while-open succeeds.
    in.close();
    std::filesystem::remove(path);
}

#if !defined(_WIN32)
TEST_CASE("FileSink creates the log file owner-only (0600)", "[logsinks][file]") {
    using namespace ajazz::core;

    auto const path = std::filesystem::temp_directory_path() / "ajazz_test_log_sink_mode.log";
    std::filesystem::remove(path);

    {
        FileSink sink(path.string());
        REQUIRE(sink.isOpen());
    }

    // The sink must pin the create mode itself (0600), independent of the
    // process umask: world/group-writable logs are the CodeQL
    // cpp/world-writable-file-creation finding this guards against.
    auto const perms = std::filesystem::status(path).permissions();
    REQUIRE((perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    REQUIRE((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
    REQUIRE((perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
            std::filesystem::perms::none);

    std::filesystem::remove(path);
}
#endif

TEST_CASE("FileSink degrades to a silent no-op on an unopenable path", "[logsinks][file]") {
    using namespace ajazz::core;

    // A path under a non-existent directory cannot be opened for append.
    FileSink sink("/this/directory/does/not/exist/ajazz.log");
    REQUIRE_FALSE(sink.isOpen());
    REQUIRE_NOTHROW(sink.write(LogLevel::Error, "mod", "must not crash"));
}

TEST_CASE("levelLabel renders fixed-width ASCII labels", "[logsinks]") {
    using namespace ajazz::core;
    REQUIRE(levelLabel(LogLevel::Trace) == "TRACE");
    REQUIRE(levelLabel(LogLevel::Info) == "INFO ");
    REQUIRE(levelLabel(LogLevel::Warn) == "WARN ");
    REQUIRE(levelLabel(LogLevel::Critical) == "CRIT ");
}
