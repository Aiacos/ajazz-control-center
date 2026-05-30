// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file log_sinks.cpp
 * @brief Implementations of the fan-out, ring-buffer, and file log sinks.
 *
 * Each sink owns its serialisation (per-sink mutex) so installing a
 * capturing/ring sink in a test never contends with a stray stderr sink.
 * @ref TeeSink is the exception: its child list is immutable after
 * construction, so it forwards lock-free and leans on each child's own
 * thread-safety guarantee.
 */
#include "ajazz/core/log_sinks.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace ajazz::core {
namespace {

/// Milliseconds since the Unix epoch, matching the StderrSink stamp.
long long nowEpochMs() noexcept {
    auto const now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

} // namespace

std::string_view levelLabel(LogLevel level) noexcept {
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

// ----------------------------------------------------------------------------
// TeeSink
// ----------------------------------------------------------------------------

TeeSink::TeeSink(std::vector<std::shared_ptr<LogSink>> sinks) : sinks_(std::move(sinks)) {}

void TeeSink::write(LogLevel level, std::string_view module, std::string_view message) noexcept {
    for (auto const& sink : sinks_) {
        if (sink) {
            sink->write(level, module, message);
        }
    }
}

// ----------------------------------------------------------------------------
// RingBufferSink
// ----------------------------------------------------------------------------

RingBufferSink::RingBufferSink(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

void RingBufferSink::write(LogLevel level,
                           std::string_view module,
                           std::string_view message) noexcept {
    std::lock_guard const lock(mutex_);
    if (records_.size() >= capacity_) {
        records_.pop_front();
    }
    records_.push_back(LogRecord{level, std::string(module), std::string(message), nowEpochMs()});
}

std::vector<LogRecord> RingBufferSink::snapshot(LogLevel minLevel, long long sinceEpochMs) const {
    std::lock_guard const lock(mutex_);
    std::vector<LogRecord> out;
    out.reserve(records_.size());
    for (auto const& rec : records_) {
        if (static_cast<int>(rec.level) < static_cast<int>(minLevel)) {
            continue;
        }
        if (rec.epochMs <= sinceEpochMs) {
            continue;
        }
        out.push_back(rec);
    }
    return out;
}

std::size_t RingBufferSink::size() const noexcept {
    std::lock_guard const lock(mutex_);
    return records_.size();
}

void RingBufferSink::clear() noexcept {
    std::lock_guard const lock(mutex_);
    records_.clear();
}

// ----------------------------------------------------------------------------
// FileSink
// ----------------------------------------------------------------------------

FileSink::FileSink(std::string path) : path_(std::move(path)) {
    // Append mode so successive runs accumulate into one log; the caller is
    // responsible for rotation/truncation policy. A failed open leaves
    // file_ == nullptr and write() degrades to a no-op.
#if defined(_MSC_VER)
    // MSVC treats std::fopen as deprecated (C4996 -> /WX hard error); use the
    // bounds-checked fopen_s. The glibc-only "e" (O_CLOEXEC) mode flag does not
    // exist on Windows, so the mode is plain "a" here.
    if (::fopen_s(&file_, path_.c_str(), "a") != 0) {
        file_ = nullptr;
    }
#else
    // POSIX: keep the "e" (O_CLOEXEC) flag so the log fd is not inherited by the
    // forked/bwrap'd plugin host child.
    file_ = std::fopen(path_.c_str(), "ae");
#endif
}

FileSink::~FileSink() {
    std::lock_guard const lock(mutex_);
    if (file_ != nullptr) {
        (void)std::fclose(file_);
        file_ = nullptr;
    }
}

void FileSink::write(LogLevel level, std::string_view module, std::string_view message) noexcept {
    std::lock_guard const lock(mutex_);
    if (file_ == nullptr) {
        return;
    }
    (void)std::fprintf(file_,
                       "[%lld] [%s] [%.*s] %.*s\n",
                       nowEpochMs(),
                       levelLabel(level).data(),
                       static_cast<int>(module.size()),
                       module.data(),
                       static_cast<int>(message.size()),
                       message.data());
    (void)std::fflush(file_);
}

bool FileSink::isOpen() const noexcept {
    return file_ != nullptr;
}

} // namespace ajazz::core
