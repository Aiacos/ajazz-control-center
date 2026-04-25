// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file logger.cpp
 * @brief Logger implementation: atomic level filter + pluggable sink.
 *
 * The active sink is held in an `std::atomic<std::shared_ptr<LogSink>>`
 * so @ref setLogSink swaps atomically against concurrent @ref log calls;
 * the old sink stays alive past the swap because @ref log holds a
 * shared_ptr copy for the duration of its emit() call. The default sink
 * (@c StderrSink) reproduces the legacy
 * `[<ms-since-epoch>] [<LEVEL>] [<module>] <message>\n` format.
 *
 * gLevel is a separate atomic int so the level filter in log() never
 * needs to dereference the sink (early-return cost ≈ one atomic load).
 */
#include "ajazz/core/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <utility>

namespace ajazz::core {
namespace {

/// Map a LogLevel to its fixed-width 5-character ASCII label. The
/// trailing space on "INFO" and "WARN" makes every label occupy the
/// same column width in the output.
constexpr std::string_view levelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO ";
    case LogLevel::Warn:
        return "WARN ";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Critical:
        return "CRIT ";
    }
    return "?";
}

/**
 * @brief Default sink: serialises lines to stderr via @c std::fprintf.
 *
 * The mutex is intentionally per-sink rather than global so a test
 * that installs a capturing sink doesn't contend with a stray
 * StderrSink instance (there shouldn't be one — but defensively it
 * means each sink owns its own serialisation).
 */
class StderrSink : public LogSink {
public:
    void
    write(LogLevel level, std::string_view module, std::string_view message) noexcept override {
        auto const now = std::chrono::system_clock::now();
        auto const tp =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        std::lock_guard const lock(mutex_);
        (void)std::fprintf(stderr,
                           "[%lld] [%s] [%.*s] %.*s\n",
                           static_cast<long long>(tp),
                           levelName(level).data(),
                           static_cast<int>(module.size()),
                           module.data(),
                           static_cast<int>(message.size()),
                           message.data());
    }

private:
    std::mutex mutex_;
};

/// Active minimum log level; relaxed-order atomic for cheap reads.
std::atomic<int> gLevel{static_cast<int>(LogLevel::Info)};

/**
 * @brief Process-wide active sink.
 *
 * Initialised lazily so we can install our default StderrSink without
 * relying on order-of-static-initialisation across translation units.
 * Thread-safe against concurrent log() calls because we always swap
 * the @c std::shared_ptr atomically; an in-flight emit() holds its own
 * shared_ptr copy and cannot observe a half-replaced sink.
 */
std::atomic<std::shared_ptr<LogSink>>& sinkSlot() noexcept {
    static std::atomic<std::shared_ptr<LogSink>> slot{std::make_shared<StderrSink>()};
    return slot;
}

} // namespace

void setLogSink(std::shared_ptr<LogSink> sink) noexcept {
    if (!sink) {
        // Reset to the default — callers typically use this in a test
        // tearDown to undo a captureSink installation.
        sink = std::make_shared<StderrSink>();
    }
    sinkSlot().store(std::move(sink), std::memory_order_release);
}

void setLogLevel(LogLevel level) noexcept {
    gLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel logLevel() noexcept {
    return static_cast<LogLevel>(gLevel.load(std::memory_order_relaxed));
}

void log(LogLevel level, std::string_view module, std::string_view message) noexcept {
    if (static_cast<int>(level) < gLevel.load(std::memory_order_relaxed)) {
        return;
    }
    // Atomic-load via the slot — this gives us a refcounted handle to the
    // sink that stays alive for the duration of emit() even if a
    // concurrent setLogSink() swap happens.
    auto const sink = sinkSlot().load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->write(level, module, message);
}

} // namespace ajazz::core
