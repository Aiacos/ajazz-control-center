// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_io.hpp
 * @brief Atomic, thread-safe profile persistence on disk.
 *
 * `readProfileFromDisk()` and `writeProfileToDisk()` provide the canonical
 * filesystem boundary for profile JSON. Writes are atomic: data is first
 * streamed to a sibling temporary file, fsync()ed, and only then `rename(2)`d
 * over the destination, guaranteeing that a power loss or crash will leave
 * either the previous file or the new file fully on disk — never a half-
 * written profile.
 *
 * Both functions perform basic schema validation (required keys, value type
 * sanity) and throw `ProfileIoError` on any failure. Callers should treat the
 * exception as recoverable — the caller's previous in-memory state is still
 * valid because reads do not mutate it.
 *
 * @threadsafe Safe to call concurrently from multiple threads, even on the
 * same path: the OS guarantees rename atomicity, so concurrent writers will
 * end up with one of the inputs winning. A per-path mutex layer can be added
 * by callers who need write serialisation.
 *
 * @see SEC-012, COD-017
 */
#pragma once

#include "ajazz/core/profile.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace ajazz::core {

/**
 * @brief Exception raised by readProfileFromDisk / writeProfileToDisk.
 *
 * The `what()` message is suitable for surfacing to a developer console.
 * UI layers should generally show a translated, friendlier message and
 * forward the raw text to the log subsystem.
 */
class ProfileIoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Atomically write a profile to disk as JSON.
 *
 * The write is performed in three steps:
 *   1. Serialise to JSON via profileToJson().
 *   2. Stream the bytes to `<path>.tmp.<pid>.<seq>` and fsync.
 *   3. rename() the temporary file over `path`. POSIX guarantees this step
 *      is atomic with respect to readers; on Windows we use MoveFileExW with
 *      MOVEFILE_REPLACE_EXISTING.
 *
 * @param path    Destination path; parent directory must exist.
 * @param profile Profile to serialise.
 * @throws ProfileIoError on any I/O or serialisation failure.
 */
void writeProfileToDisk(std::filesystem::path const& path, Profile const& profile);

/**
 * @brief Read a profile from disk and validate it.
 *
 * The file is opened, fully read into memory, parsed, and validated. The
 * file is never modified by this call.
 *
 * @param path Source path.
 * @return Parsed and validated Profile.
 * @throws ProfileIoError if the file is missing, unreadable, malformed, or
 *         fails schema validation.
 */
[[nodiscard]] Profile readProfileFromDisk(std::filesystem::path const& path);

/**
 * @brief Lightweight schema check on a profile JSON string.
 *
 * Used by readProfileFromDisk(); also exposed to tests and to external
 * callers (plugin SDK) who need to validate user-supplied JSON before
 * passing it through the loader.
 *
 * @param json JSON string to validate.
 * @return Empty string on success; a human-readable error message on failure.
 */
[[nodiscard]] std::string validateProfileJson(std::string_view json);

} // namespace ajazz::core
