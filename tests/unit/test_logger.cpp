// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_logger.cpp
 * @brief Unit tests for the application logger.
 *
 * Covers level control round-trip, basic logf() formatting, and the
 * pluggable LogSink contract: a capturing sink installed via
 * setLogSink() must receive every record the level filter accepts
 * (level, module and message preserved verbatim, in call order) and
 * must NOT receive records below the active filter — the level check
 * sits in log() before write() is invoked, so a custom sink relies on
 * that invariant being honored.
 */
#include "ajazz/core/logger.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

/// setLogLevel() must be immediately reflected by logLevel() with no intermediate state.
TEST_CASE("logger level round-trips", "[logger]") {
    using namespace ajazz::core;

    auto const previous = logLevel();
    setLogLevel(LogLevel::Debug);
    REQUIRE(logLevel() == LogLevel::Debug);
    setLogLevel(LogLevel::Error);
    REQUIRE(logLevel() == LogLevel::Error);
    setLogLevel(previous);
}

/// logf() must not throw for a valid format string and an integer argument.
TEST_CASE("logger format does not throw", "[logger]") {
    using namespace ajazz::core;
    REQUIRE_NOTHROW(logf(LogLevel::Info, "test", "value = {}", 42));
}

namespace {

/// Capturing sink for tests: records every accepted log call as a tuple.
///
/// LogSink::write is documented as thread-safe (logger.hpp:46-47) so the
/// helper takes its mutex even when the test that uses it is single-
/// threaded — keeps the helper reusable in future tests that exercise
/// concurrent loggers without re-implementing the lock.
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

/// RAII installer: takes ownership of the active sink and the level for
/// the lifetime of the test, restores both on destruction. Passing
/// nullptr to setLogSink() resets the global slot to the default
/// StderrSink (logger.cpp:107-110), so the next test sees pristine
/// state regardless of how this one exits.
class ScopedSink {
public:
    explicit ScopedSink(std::shared_ptr<ajazz::core::LogSink> sink)
        : previousLevel_(ajazz::core::logLevel()) {
        ajazz::core::setLogSink(std::move(sink));
    }
    ~ScopedSink() {
        ajazz::core::setLogLevel(previousLevel_);
        ajazz::core::setLogSink(nullptr);
    }
    ScopedSink(ScopedSink const&) = delete;
    ScopedSink& operator=(ScopedSink const&) = delete;
    ScopedSink(ScopedSink&&) = delete;
    ScopedSink& operator=(ScopedSink&&) = delete;

private:
    ajazz::core::LogLevel previousLevel_;
};

} // namespace

/// An installed CapturingSink receives every record at or above the
/// active level filter, with level / module / message preserved verbatim
/// and the call order intact. Exercises both the macro path
/// (AJAZZ_LOG_INFO) and the std::format substitution in logf().
TEST_CASE("logger sink captures accepted records", "[logger][sink]") {
    using namespace ajazz::core;

    auto sink = std::make_shared<CapturingSink>();
    ScopedSink const guard(sink);
    setLogLevel(LogLevel::Trace);

    AJAZZ_LOG_INFO("net", "hello {}", "world");
    AJAZZ_LOG_WARN("hid", "code {}", 42);

    auto const records = sink->snapshot();
    REQUIRE(records.size() == 2);
    REQUIRE(records[0].level == LogLevel::Info);
    REQUIRE(records[0].module == "net");
    REQUIRE(records[0].message == "hello world");
    REQUIRE(records[1].level == LogLevel::Warn);
    REQUIRE(records[1].module == "hid");
    REQUIRE(records[1].message == "code 42");
}

/// Records below the active level filter must NOT reach the sink — the
/// filter is enforced in log() before write() runs (logger.cpp:126), so
/// custom sinks never see filtered-out records and don't need to
/// re-implement the filter themselves.
TEST_CASE("logger sink respects level filter", "[logger][sink]") {
    using namespace ajazz::core;

    auto sink = std::make_shared<CapturingSink>();
    ScopedSink const guard(sink);
    setLogLevel(LogLevel::Warn);

    AJAZZ_LOG_TRACE("ignored", "trace {}", 1);
    AJAZZ_LOG_DEBUG("ignored", "debug {}", 2);
    AJAZZ_LOG_INFO("ignored", "info {}", 3);
    AJAZZ_LOG_WARN("kept", "warn {}", 4);
    AJAZZ_LOG_ERROR("kept", "error {}", 5);

    auto const records = sink->snapshot();
    REQUIRE(records.size() == 2);
    REQUIRE(records[0].level == LogLevel::Warn);
    REQUIRE(records[0].module == "kept");
    REQUIRE(records[0].message == "warn 4");
    REQUIRE(records[1].level == LogLevel::Error);
    REQUIRE(records[1].module == "kept");
    REQUIRE(records[1].message == "error 5");
}
