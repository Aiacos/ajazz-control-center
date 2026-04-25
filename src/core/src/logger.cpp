// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file logger.cpp
 * @brief Logger implementation: atomic level filter + stderr sink.
 *
 * gLevel is an atomic int so setLogLevel() and the filter in log() are
 * always race-free. gMutex serialises fprintf() calls so log lines from
 * multiple threads do not interleave.
 */
#include "ajazz/core/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace ajazz::core {
namespace {

/// Active minimum log level; relaxed-order atomic for cheap reads.
std::atomic<int> gLevel{static_cast<int>(LogLevel::Info)};
/// Serialises stderr writes across threads.
std::mutex gMutex;

/**
 * @brief Map a LogLevel to its fixed-width 5-character ASCII label.
 *
 * The trailing space on "INFO" and "WARN" ensures that all labels
 * occupy the same column width in the output.
 *
 * @param level Log severity.
 * @return String view into a string literal; always valid.
 */
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

} // namespace

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
    auto const now = std::chrono::system_clock::now();
    auto const tp =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::lock_guard const lock(gMutex);
    (void)std::fprintf(stderr,
                       "[%lld] [%s] [%.*s] %.*s\n",
                       static_cast<long long>(tp),
                       levelName(level).data(),
                       static_cast<int>(module.size()),
                       module.data(),
                       static_cast<int>(message.size()),
                       message.data());
}

} // namespace ajazz::core
