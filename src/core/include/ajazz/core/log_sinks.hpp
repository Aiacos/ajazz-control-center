// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file log_sinks.hpp
 * @brief Reusable @ref LogSink implementations: fan-out, in-memory ring,
 *        and append-to-file.
 *
 * The core logger (@ref logger.hpp) dispatches every accepted record to a
 * single active @ref LogSink. These building blocks let a deployment route
 * the same stream to several destinations at once:
 *
 *   - @ref TeeSink fans one record out to N child sinks (e.g. keep the
 *     legacy stderr behaviour AND persist to a file AND retain a queryable
 *     in-memory tail).
 *   - @ref RingBufferSink keeps the most recent records in a bounded ring
 *     and exposes a thread-safe @ref RingBufferSink::snapshot — the backing
 *     store for an in-app / out-of-process log viewer.
 *   - @ref FileSink appends timestamped lines to a file using the same
 *     `[<ms-since-epoch>] [<LEVEL>] [<module>] <message>` format as the
 *     default stderr sink.
 *
 * Like every sink, all three are thread-safe and never throw — logging on
 * the hot path must not become a failure mode. A @ref FileSink that cannot
 * open its target degrades to a silent no-op rather than aborting.
 *
 * @note This header pulls in no Qt and no JSON dependency, so it is safe to
 *       include from @c ajazz_core and any installed public consumer
 *       (the COD-031 boundary).
 */
#pragma once

#include "ajazz/core/logger.hpp"

#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::core {

/**
 * @brief A captured log record with its acceptance timestamp.
 *
 * Produced by @ref RingBufferSink::snapshot. @p module and @p message are
 * owning copies so a snapshot outlives the originating @c string_view.
 */
struct LogRecord {
    LogLevel level{LogLevel::Info};
    std::string module;
    std::string message;
    long long epochMs{0}; ///< Milliseconds since the Unix epoch at write time.
};

/// Map a level to its fixed-width 5-character ASCII label
/// ("INFO " / "WARN " padded). Shared by the file sink and any consumer
/// that needs to render a record; mirrors the labels the stderr sink uses.
[[nodiscard]] std::string_view levelLabel(LogLevel level) noexcept;

/**
 * @brief Fan-out sink: forwards each record to every child sink in order.
 *
 * The child list is fixed at construction, so @ref write needs no lock of
 * its own — it relies on each child being independently thread-safe (the
 * @ref LogSink contract). Null children are skipped.
 */
class TeeSink : public LogSink {
public:
    explicit TeeSink(std::vector<std::shared_ptr<LogSink>> sinks);

    void write(LogLevel level, std::string_view module, std::string_view message) noexcept override;

private:
    std::vector<std::shared_ptr<LogSink>> sinks_;
};

/**
 * @brief Bounded in-memory ring of the most recent records.
 *
 * Retains up to @c capacity records; the oldest is dropped once full.
 * @ref snapshot returns a copy (oldest-first) optionally filtered by a
 * minimum level and a lower-bound timestamp, so a poller can ask only for
 * "warnings and above since the last cursor".
 */
class RingBufferSink : public LogSink {
public:
    explicit RingBufferSink(std::size_t capacity = 4096);

    void write(LogLevel level, std::string_view module, std::string_view message) noexcept override;

    /**
     * @brief Copy of the retained records, oldest-first.
     * @param minLevel     Drop records below this severity.
     * @param sinceEpochMs Drop records with @c epochMs <= this value
     *                     (pass 0 for "everything still retained").
     */
    [[nodiscard]] std::vector<LogRecord> snapshot(LogLevel minLevel = LogLevel::Trace,
                                                  long long sinceEpochMs = 0) const;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;

private:
    mutable std::mutex mutex_;
    std::size_t capacity_;
    std::deque<LogRecord> records_;
};

/**
 * @brief Appends timestamped lines to a file in the legacy stderr format.
 *
 * Opens @p path in append mode at construction. If the open fails (bad
 * path, permissions) the sink stays valid but @ref write is a no-op and
 * @ref isOpen reports false — logging never aborts the process.
 */
class FileSink : public LogSink {
public:
    explicit FileSink(std::string path);
    ~FileSink() override;

    FileSink(FileSink const&) = delete;
    FileSink& operator=(FileSink const&) = delete;
    FileSink(FileSink&&) = delete;
    FileSink& operator=(FileSink&&) = delete;

    void write(LogLevel level, std::string_view module, std::string_view message) noexcept override;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::string const& path() const noexcept { return path_; }

private:
    std::mutex mutex_;
    std::string path_;
    std::FILE* file_{nullptr};
};

} // namespace ajazz::core
