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

#if !defined(_WIN32)
/**
 * @brief fsync() the directory containing @p path so the rename of
 *        a child entry is durable across a power loss (SEC-S4).
 *
 * Failure is logged via errno only; the rename itself already succeeded so
 * we never propagate this as an error to the caller. Best-effort durability.
 */
void fsyncParentDir(std::filesystem::path const& path) {
    auto const parent = path.parent_path();
    if (parent.empty()) {
        return;
    }
    int const dfd = ::open(parent.string().c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        return;
    }
    (void)::fsync(dfd);
    ::close(dfd);
}
#endif

/**
 * @brief Atomically write @p data to @p path under a tighter security model
 *        than std::ofstream offers.
 *
 * Behaviour (Linux/macOS):
 *  - O_NOFOLLOW prevents writing into a target reachable through a symlink
 *    (SEC-S4 mitigation: an attacker who can plant a symlink at the target
 *    cannot redirect the write).
 *  - O_CLOEXEC stops the fd from leaking into child processes.
 *  - Mode 0600 caps permissions even on filesystems that don't honour
 *    std::filesystem::permissions().
 *  - fsync() is called on the *original* fd — not on a re-opened copy — so
 *    the bytes the caller wrote are guaranteed to reach the kernel before
 *    the rename swap (SEC-S6).
 *
 * On Windows the std::ofstream path is preserved because the security model
 * here (CreateFile sharing flags + atomic MoveFileExW) is different and
 * already handled by atomicRename().
 */
void writeAndSyncFile(std::filesystem::path const& path, std::string const& data) {
#if defined(_WIN32)
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        throw ProfileIoError{"profile_io: cannot open temporary file " + path.string()};
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw ProfileIoError{"profile_io: write failed for " + path.string()};
    }
    out.flush();
    if (!out) {
        throw ProfileIoError{"profile_io: flush failed for " + path.string()};
    }
#else
    // O_NOFOLLOW combined with O_CREAT|O_EXCL on a fresh tmp filename means we
    // either create a new regular file at exactly that path, or we fail — no
    // symlink trickery in between.
    int const fd =
        ::open(path.string().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        std::error_code const ec{errno, std::generic_category()};
        throw ProfileIoError{"profile_io: cannot open temporary file " + path.string() + ": " +
                             ec.message()};
    }
    auto const* p = data.data();
    auto remaining = data.size();
    while (remaining > 0) {
        ssize_t const n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::error_code const ec{errno, std::generic_category()};
            ::close(fd);
            throw ProfileIoError{"profile_io: write failed for " + path.string() + ": " +
                                 ec.message()};
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        std::error_code const ec{errno, std::generic_category()};
        ::close(fd);
        throw ProfileIoError{"profile_io: fsync failed for " + path.string() + ": " + ec.message()};
    }
    if (::close(fd) != 0) {
        std::error_code const ec{errno, std::generic_category()};
        throw ProfileIoError{"profile_io: close failed for " + path.string() + ": " + ec.message()};
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
    // can stack the AV/indexer holds well past 250 ms.
    constexpr int kMaxAttempts = 12;
    auto const srcW = src.wstring();
    auto const dstW = dst.wstring();
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        // Strategy:
        //  1) If the destination already exists, prefer ReplaceFileW — it is purpose-built for
        //     atomic-replace-with-backup, holds an internal handle long enough to dodge most
        //     AV/indexer races, and preserves NTFS metadata (ACLs, owner, timestamps).
        //  2) Otherwise, MoveFileExW with REPLACE_EXISTING | WRITE_THROUGH is the cheapest
        //     correct primitive (single rename when nothing exists at the target).
        BOOL ok = FALSE;
        DWORD const dstAttrs = GetFileAttributesW(dstW.c_str());
        bool const dstExists = (dstAttrs != INVALID_FILE_ATTRIBUTES);
        if (dstExists) {
            // ReplaceFileW with REPLACEFILE_WRITE_THROUGH issues a write-through replace and
            // returns FALSE if the destination is held by another process. We feed nullptr as
            // the backup filename so no .bak is left behind.
            ok = ReplaceFileW(dstW.c_str(),
                              srcW.c_str(),
                              nullptr,
                              REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS,
                              nullptr,
                              nullptr);
        } else {
            ok = MoveFileExW(
                srcW.c_str(), dstW.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        if (ok) {
            return;
        }
        lastError = GetLastError();
        // Only retry on transient sharing/permission errors.
        if (lastError != ERROR_ACCESS_DENIED && lastError != ERROR_SHARING_VIOLATION &&
            lastError != ERROR_LOCK_VIOLATION && lastError != ERROR_USER_MAPPED_FILE) {
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
    try {
        writeAndSyncFile(tmp, json);
#if defined(_WIN32)
        // Windows: tighten permissions via std::filesystem after the file exists
        // (no equivalent of POSIX 0600-on-create that fits std::ofstream).
        std::error_code permEc;
        std::filesystem::permissions(tmp,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     permEc); // best-effort
#endif
    } catch (...) {
        std::filesystem::remove(tmp, ec); // best-effort
        throw;
    }
    try {
        atomicRename(tmp, path);
    } catch (...) {
        std::filesystem::remove(tmp, ec); // best-effort
        throw;
    }
#if !defined(_WIN32)
    // Durability: fsync the parent directory so the rename is recoverable
    // even after power loss before the next checkpoint.
    fsyncParentDir(path);
#endif
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
