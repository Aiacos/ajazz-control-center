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
#include <shared_mutex>
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
 * @brief Process-wide active sink + the lock that guards swap vs. read.
 *
 * Cannot use @c std::atomic<std::shared_ptr<LogSink>> here because
 * libc++ on macOS still requires the inner type be trivially copyable
 * (and @c std::shared_ptr is not). A small @c std::shared_mutex over a
 * plain @c std::shared_ptr is portable, lets concurrent log() calls
 * read the sink under a shared lock, and only blocks them while
 * setLogSink() is replacing the slot.
 */
struct SinkSlot {
    std::shared_mutex mutex;
    std::shared_ptr<LogSink> sink;
};

SinkSlot& sinkSlot() noexcept {
    static SinkSlot slot{{}, std::make_shared<StderrSink>()};
    return slot;
}

} // namespace

void setLogSink(std::shared_ptr<LogSink> sink) noexcept {
    if (!sink) {
        // Reset to the default — callers typically use this in a test
        // tearDown to undo a capturing-sink installation.
        sink = std::make_shared<StderrSink>();
    }
    auto& slot = sinkSlot();
    std::unique_lock const lock(slot.mutex);
    slot.sink = std::move(sink);
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
    // Take a shared_ptr copy under shared lock, then release the lock
    // before calling write(). The copy keeps the sink alive for the
    // duration of write() even if a concurrent setLogSink() swap
    // happens immediately after.
    std::shared_ptr<LogSink> sink;
    {
        auto& slot = sinkSlot();
        std::shared_lock const lock(slot.mutex);
        sink = slot.sink;
    }
    if (!sink) {
        return;
    }
    sink->write(level, module, message);
}

} // namespace ajazz::core
