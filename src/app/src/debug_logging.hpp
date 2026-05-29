// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_logging.hpp
 * @brief App-tier wiring that unifies every log source into one queryable
 *        stream.
 *
 * The core logger (@ref ajazz/core/logger.hpp) already dispatches the
 * project's own @c AJAZZ_LOG_* records through a pluggable sink. Qt's own
 * diagnostics (@c qDebug / @c qWarning / the @c qCDebug category loggers
 * used by the catalog fetchers) bypass it entirely and land on Qt's default
 * stderr handler. @ref DebugLogging::install closes that gap:
 *
 *   1. Installs a @ref ajazz::core::TeeSink that fans every accepted record
 *      out to stderr (legacy behaviour), an append-only log file, and a
 *      bounded in-memory ring.
 *   2. Installs a @c qInstallMessageHandler that funnels Qt's messages into
 *      @ref ajazz::core::log, so they flow through the very same tee.
 *
 * The returned ring buffer is the backing store the out-of-process debug
 * control channel reads to serve "tail the whole system log".
 */
#pragma once

#include "ajazz/core/log_sinks.hpp"
#include "ajazz/core/logger.hpp"

#include <QtGlobal>

#include <cstddef>
#include <memory>

class QString;

namespace ajazz::app {

/// Map a Qt message severity to the nearest core @ref LogLevel.
/// Pure and dependency-light so the mapping contract can be unit-tested.
[[nodiscard]] core::LogLevel qtMsgTypeToLevel(QtMsgType type) noexcept;

/// Parse a level name ("trace".."critical", case-insensitive) as used by the
/// @c AJAZZ_LOG_LEVEL environment override. Returns @p fallback when the name
/// is empty or unrecognised.
[[nodiscard]] core::LogLevel parseLogLevel(QString const& name, core::LogLevel fallback) noexcept;

/// Read the @c AJAZZ_LOG_LEVEL environment variable, falling back to
/// @p fallback when unset or unrecognised.
[[nodiscard]] core::LogLevel logLevelFromEnv(core::LogLevel fallback) noexcept;

struct DebugLogging {
    /**
     * @brief Install the unified stderr+file+ring tee and the Qt bridge.
     *
     * @param logFilePath  Absolute path for the append-only log file; its
     *                     parent directory is created if missing. Pass an
     *                     empty string to skip the file leg (stderr + ring
     *                     only).
     * @param level        Minimum severity retained (also applied via
     *                     @ref ajazz::core::setLogLevel).
     * @param ringCapacity Number of records the in-memory ring retains.
     * @return The ring sink, so callers can expose its @c snapshot to a log
     *         viewer / control channel. Never null.
     */
    static std::shared_ptr<core::RingBufferSink>
    install(QString const& logFilePath, core::LogLevel level, std::size_t ringCapacity = 4096);

    /// Default log file: `<AppDataLocation>/logs/ajazz-control-center.log`.
    [[nodiscard]] static QString defaultLogFilePath();
};

} // namespace ajazz::app
