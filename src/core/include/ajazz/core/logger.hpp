// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace ajazz::core {

enum class LogLevel : int {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};

void setLogLevel(LogLevel level) noexcept;
[[nodiscard]] LogLevel logLevel() noexcept;

void log(LogLevel level, std::string_view module, std::string_view message) noexcept;

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
