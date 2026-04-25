// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file logger.hpp
 * @brief Lightweight, thread-safe logging API for the core library.
 *
 * The logger writes timestamped lines to stderr by default. The active
 * log level acts as a filter; messages below it are discarded without
 * formatting. Prefer the AJAZZ_LOG_* convenience macros over calling
 * logf() directly.
 *
 * Output is dispatched through a pluggable @ref LogSink so tests can
 * capture log lines and a future production deployment can route them
 * to journald / syslog / a Qt model without touching the call sites.
 *
 * @note All entry points are marked `noexcept`; logging never throws.
 */
#pragma once

#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace ajazz::core {

/// Severity levels for log messages, in ascending order of importance.
enum class LogLevel : int {
    Trace = 0, ///< Fine-grained tracing; very verbose.
    Debug,     ///< Developer debug output.
    Info,      ///< Normal operational messages (default minimum level).
    Warn,      ///< Recoverable abnormal conditions.
    Error,     ///< Non-fatal errors that require attention.
    Critical,  ///< Fatal conditions; application should terminate.
};

/**
 * @brief Sink interface for log dispatch.
 *
 * The library ships a default @c StderrSink that mirrors the legacy
 * formatting (`[<ms-since-epoch>] [<LEVEL>] [<module>] <message>\n`).
 * Tests can install a capturing sink via @ref setLogSink to assert on
 * what subsystems logged; a production deployment can install a
 * journald / syslog / Qt-model sink without touching call sites.
 *
 * Implementations must be thread-safe — @ref emit can be called from
 * any thread, concurrently. The default sink serialises writes to
 * stderr via an internal mutex.
 */
class LogSink {
public:
    virtual ~LogSink() = default;
    LogSink(LogSink const&) = delete;
    LogSink& operator=(LogSink const&) = delete;
    LogSink(LogSink&&) = delete;
    LogSink& operator=(LogSink&&) = delete;

    /**
     * @brief Receive a single log record.
     *
     * Called only after the level filter has accepted the record, so
     * implementations don't need to re-check.
     *
     * Method name @c write rather than @c emit because @c emit is a Qt
     * macro defined as nothing for signal-emission ergonomics; using it
     * as a member name here would silently turn into `void  (...)` in
     * any translation unit that included a Qt header earlier.
     *
     * @param level   Severity of the message.
     * @param module  Short ASCII identifier of the emitting subsystem.
     * @param message Pre-formatted message text (no trailing newline).
     */
    virtual void
    write(LogLevel level, std::string_view module, std::string_view message) noexcept = 0;

protected:
    LogSink() = default;
};

/**
 * @brief Replace the active log sink.
 *
 * Pass @c nullptr to reset to the default stderr sink. Thread-safe
 * against concurrent @ref log / @ref logf calls — the swap is atomic
 * and the previous sink stays alive long enough for any in-flight
 * @ref emit call to finish (it is held via @c shared_ptr).
 *
 * @param sink Owning shared pointer to the new sink, or nullptr.
 */
void setLogSink(std::shared_ptr<LogSink> sink) noexcept;

/**
 * @brief Set the minimum log level accepted by log() / logf().
 *
 * Messages with a level below `level` are silently discarded. The
 * internal state is stored in an atomic so this call is thread-safe.
 *
 * @param level New minimum severity threshold.
 */
void setLogLevel(LogLevel level) noexcept;

/**
 * @brief Return the current minimum log level.
 * @return The level set by the most recent setLogLevel() call.
 */
[[nodiscard]] LogLevel logLevel() noexcept;

/**
 * @brief Emit a log line to the active sink if `level` meets the threshold.
 *
 * @param level   Severity of the message.
 * @param module  Short ASCII identifier of the emitting subsystem.
 * @param message Pre-formatted message text.
 */
void log(LogLevel level, std::string_view module, std::string_view message) noexcept;

/**
 * @brief Format-string overload of log(); accepts `std::format` arguments.
 *
 * @param level  Severity of the message.
 * @param module Short ASCII identifier of the emitting subsystem.
 * @param fmt    Compile-time format string (std::format_string).
 * @param args   Arguments forwarded to std::vformat.
 */
template <typename... Args>
void logf(LogLevel level,
          std::string_view module,
          std::format_string<Args...> fmt,
          Args&&... args) noexcept {
    log(level, module, std::vformat(fmt.get(), std::make_format_args(args...)));
}

#define AJAZZ_LOG_TRACE(module, ...)                                                               \
    ::ajazz::core::logf(::ajazz::core::LogLevel::Trace, module, __VA_ARGS__)
#define AJAZZ_LOG_DEBUG(module, ...)                                                               \
    ::ajazz::core::logf(::ajazz::core::LogLevel::Debug, module, __VA_ARGS__)
#define AJAZZ_LOG_INFO(module, ...)                                                                \
    ::ajazz::core::logf(::ajazz::core::LogLevel::Info, module, __VA_ARGS__)
#define AJAZZ_LOG_WARN(module, ...)                                                                \
    ::ajazz::core::logf(::ajazz::core::LogLevel::Warn, module, __VA_ARGS__)
#define AJAZZ_LOG_ERROR(module, ...)                                                               \
    ::ajazz::core::logf(::ajazz::core::LogLevel::Error, module, __VA_ARGS__)

} // namespace ajazz::core
