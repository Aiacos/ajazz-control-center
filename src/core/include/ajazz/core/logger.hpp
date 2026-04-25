// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file logger.hpp
 * @brief Lightweight, thread-safe logging API for the core library.
 *
 * The logger writes timestamped lines to stderr. The active log level acts
 * as a filter; messages below it are discarded without formatting. Prefer
 * the AJAZZ_LOG_* convenience macros over calling logf() directly.
 *
 * @note All entry points are marked `noexcept`; logging never throws.
 */
#pragma once

#include <format>
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
 * @brief Emit a log line to stderr if `level` meets the threshold.
 *
 * The output format is: `[<ms-since-epoch>] [<LEVEL>] [<module>] <message>\n`
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
