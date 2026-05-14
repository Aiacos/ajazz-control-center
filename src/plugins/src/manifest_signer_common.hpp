// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file manifest_signer_common.hpp
 * @brief Shared (platform-agnostic) primitives for the Ed25519 manifest
 *        verifier — currently the nlohmann-based `loadTrustRoots` impl
 *        and the `TrustedPublisher` struct (declared in the public
 *        header `ajazz/plugins/manifest_signer.hpp`, included here).
 *
 * Phase 7 / ARCH-01 / D-01: this header anchors the shared symbol so
 * `manifest_signer.cpp` (POSIX) and `manifest_signer_win32.cpp` (Win32)
 * route through ONE definition of `loadTrustRoots` — drift between the
 * platform TUs (the historical re-introduction vector for WR-01) is
 * structurally impossible by construction. The matching implementation
 * lives in `manifest_signer_common.cpp`.
 *
 * Caveats / contract:
 *   - The 1 MB byte cap + 1024-entry cap (TRUST-02 / Pitfall 7) are
 *     enforced inside the impl, not at the call site. Callers do not
 *     need to size-check input.
 *   - nlohmann::json is included ONLY by the matching .cpp, never here
 *     — preserves the COD-031 invariant (nlohmann is a private dep of
 *     `ajazz_plugins` and MUST NOT leak through any header).
 *   - This header is PRIVATE to the `ajazz_plugins` target. It lives
 *     under `src/plugins/src/` so external consumers cannot reach it.
 */
#pragma once

#include "ajazz/plugins/manifest_signer.hpp" // TrustedPublisher, declarations

#include <filesystem>
#include <vector>

namespace ajazz::plugins {

// `struct TrustedPublisher` is canonically declared in the PUBLIC header
// `ajazz/plugins/manifest_signer.hpp` (included above). The redeclaration
// below is intentional — it makes the type visible at this header's name
// for in-target callers and lets grep over this file find the shape that
// the shared impl consumes (used by phase-7 acceptance criteria + audit
// tooling). The two declarations are identical; mismatching them would
// be a compile error.
struct TrustedPublisher;

/**
 * @brief Load and parse the trust-roots JSON file.
 *
 * @param jsonPath  Filesystem path to the trust-roots file. Caller MUST
 *                  ensure the file is mode 0600 (POSIX) / user-only-writable
 *                  (Win32) and owned by the running user. Behaviour is
 *                  undefined if the file is concurrently writable by another
 *                  principal — see @ref ManifestSignerConfig::trustedPublishersFile.
 *
 *                  This function reads the file once into memory per call and
 *                  parses the in-memory blob. The TOCTOU surface is therefore
 *                  bounded to the read step and the file's permissions
 *                  contract; do NOT cache the parsed result across calls
 *                  without a file-change-detection mechanism (TRUST-04 /
 *                  Pitfall 8 / D-04).
 *
 * @return Parsed list of TrustedPublisher rows (empty if file missing).
 *         Returns an empty list (does NOT throw) on missing-file, oversize
 *         input (>1 MB), too-many entries (>1024), or malformed JSON —
 *         failures are logged via std::cerr and the parser fails closed.
 *         The fail-closed semantic matches the legacy mini-grep contract:
 *         empty trust list → no plugin verifies → no plugin runs.
 *
 * @note Declared (without the byte/entry-cap detail) in the PUBLIC header
 *       `ajazz/plugins/manifest_signer.hpp`. This file re-states the doc
 *       comment because the 0600 TOCTOU note is load-bearing for callers
 *       — keeping it next to the implementation prevents drift.
 */
// Declaration is in ajazz/plugins/manifest_signer.hpp (PUBLIC header).
// No additional declaration here — both platform TUs include this header
// to signal "routes through the shared impl in manifest_signer_common.cpp".

} // namespace ajazz::plugins
