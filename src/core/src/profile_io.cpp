// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_io.cpp
 * @brief Atomic, thread-safe profile persistence — implementation.
 *
 * The implementation is intentionally written without Qt so it can be unit-
 * tested in pure C++ and reused from non-GUI contexts (CLI tools, the plugin
 * host). The file format is the same JSON produced by profileToJson().
 */
#include "ajazz/core/profile_io.hpp"

#include "ajazz/core/profile.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ajazz::core {

namespace {

/**
 * @brief Generate a temporary sibling filename for atomic rename.
 *
 * Format: `<path>.tmp.<pid>.<seq>`. The sequence counter is process-wide
 * and atomic, so two writes from concurrent threads cannot collide.
 */
std::filesystem::path makeTempPath(std::filesystem::path const& dest) {
    static std::atomic<std::uint64_t> seq{0};
    std::uint64_t const n = seq.fetch_add(1, std::memory_order_relaxed);

#if defined(_WIN32)
    auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    auto pid = static_cast<unsigned long>(::getpid());
#endif

    std::ostringstream oss;
    oss << dest.filename().string() << ".tmp." << pid << "." << n;
    return dest.parent_path() / oss.str();
}

/**
 * @brief Flush + fsync the file at @p fd / @p path so contents survive a crash.
 */
void syncFile(std::ostream& stream, std::filesystem::path const& path) {
    stream.flush();
    if (!stream) {
        throw ProfileIoError{"profile_io: stream flush failed for " + path.string()};
    }
#if !defined(_WIN32)
    // Re-open read-only just to fsync the data; the original ofstream's
    // FILE* is buried inside the standard library so this is the simplest
    // portable way.
    int const fd = ::open(path.string().c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

/**
 * @brief Cross-platform atomic rename. Replaces an existing destination.
 *
 * On Windows, MoveFileExW can transiently return ERROR_ACCESS_DENIED /
 * ERROR_SHARING_VIOLATION when another process (antivirus, indexer) or another
 * thread is briefly holding a handle to the destination. We retry with
 * exponential backoff for up to ~250 ms before propagating the failure.
 */
void atomicRename(std::filesystem::path const& src, std::filesystem::path const& dst) {
#if defined(_WIN32)
    DWORD lastError = 0;
    // Up to ~2.5 s of total wait under heavy contention. Concurrent writers from many threads
    // can stack the AV/indexer holds well past 250 ms; the previous 8-attempt budget could still
    // miss a small window. We bump to 12 attempts with a base sleep of 4 ms (capped) so the worst
    // case is ~4 ms * 2^11 = ~8 s but in practice the file frees up well before then.
    constexpr int kMaxAttempts = 12;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (MoveFileExW(src.wstring().c_str(),
                        dst.wstring().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return;
        }
        lastError = GetLastError();
        // Only retry on transient sharing/permission errors.
        if (lastError != ERROR_ACCESS_DENIED && lastError != ERROR_SHARING_VIOLATION) {
            break;
        }
        // Exponential backoff capped at 250 ms per attempt.
        DWORD const sleepMs = static_cast<DWORD>(4 << attempt);
        Sleep(sleepMs > 250 ? 250 : sleepMs);
    }
    throw ProfileIoError{"profile_io: rename " + src.string() + " -> " + dst.string() +
                         " failed (Win32 error " + std::to_string(lastError) + ")"};
#else
    if (::rename(src.string().c_str(), dst.string().c_str()) != 0) {
        std::error_code ec{errno, std::generic_category()};
        throw ProfileIoError{"profile_io: rename " + src.string() + " -> " + dst.string() +
                             " failed: " + ec.message()};
    }
#endif
}

/**
 * @brief Tiny string scanner — finds key tokens in a JSON document.
 *
 * Not a full parser; just enough to confirm the document looks like a
 * profile (presence of "id", "name", "device"). The full parse
 * happens at the app layer with QJsonDocument.
 */
bool containsKey(std::string_view json, std::string_view key) {
    std::string needle{"\""};
    needle += key;
    needle += "\"";
    return json.find(needle) != std::string_view::npos;
}

} // namespace

std::string validateProfileJson(std::string_view json) {
    if (json.empty()) {
        return "profile JSON is empty";
    }
    // Trim leading whitespace.
    std::size_t start = json.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos || json[start] != '{') {
        return "profile JSON does not start with an object";
    }
    // Required keys per docs/protocols/PROFILE_SCHEMA.md.
    // Note: the JSON wire-key for Profile::deviceCodename is "device" (per schema).
    static constexpr char const* kRequired[] = {"id", "name", "device"};
    for (auto const* key : kRequired) {
        if (!containsKey(json, key)) {
            std::string msg{"profile JSON is missing required key \""};
            msg += key;
            msg += "\"";
            return msg;
        }
    }
    return {};
}

void writeProfileToDisk(std::filesystem::path const& path, Profile const& profile) {
    if (path.empty()) {
        throw ProfileIoError{"profile_io: empty destination path"};
    }
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            throw ProfileIoError{"profile_io: cannot create parent directory: " + ec.message()};
        }
    }

    std::string const json = profileToJson(profile);
    if (auto err = validateProfileJson(json); !err.empty()) {
        throw ProfileIoError{"profile_io: serialiser produced invalid JSON: " + err};
    }

    auto const tmp = makeTempPath(path);
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        if (!out) {
            throw ProfileIoError{"profile_io: cannot open temporary file " + tmp.string()};
        }
        // Tighten permissions to user-only before writing payload: a Profile's
        // `settingsJson` may carry plugin secrets (API tokens, refresh tokens)
        // and the surrounding directory may be world-readable. permissions()
        // is a no-op on Windows for POSIX bits but does not throw.
        std::error_code permEc;
        std::filesystem::permissions(tmp,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     permEc); // best-effort; ignore failure on FS without modes
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!out) {
            std::filesystem::remove(tmp, ec); // best-effort cleanup
            throw ProfileIoError{"profile_io: write failed for " + tmp.string()};
        }
        syncFile(out, tmp);
    }
    try {
        atomicRename(tmp, path);
    } catch (...) {
        std::filesystem::remove(tmp, ec); // best-effort
        throw;
    }
}

Profile readProfileFromDisk(std::filesystem::path const& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw ProfileIoError{"profile_io: cannot open " + path.string()};
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string const json = buf.str();

    if (auto err = validateProfileJson(json); !err.empty()) {
        throw ProfileIoError{"profile_io: validation failed: " + err};
    }
    try {
        return profileFromJson(json);
    } catch (std::exception const& e) {
        throw ProfileIoError{std::string{"profile_io: parser threw: "} + e.what()};
    }
}

} // namespace ajazz::core
