---
phase: 7
phase_slug: wr-01-trust-roots-parser-hardening
gathered: 2026-05-14
status: Ready for planning
mode: autonomous-interactive
---

# Phase 7: WR-01 Trust-Roots Parser Hardening — Context

**Gathered:** 2026-05-14 via `/gsd-autonomous --interactive`
**Source for recommendations:** `.planning/phases/03-architectural-decisions/ARCH-01-parser-choice.md` (LOCKED: nlohmann::json 3.12.0, FetchContent, PRIVATE-link to ajazz_plugins) + `.planning/research/PITFALLS.md` Pitfalls 7 + 8 + REQUIREMENTS TRUST-01..04 + `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md` (WR-01 partial fix history).

<domain>
## Phase Boundary

The `loadTrustRoots` parser is replaced (per ARCH-01) in lockstep across both translation units, with bounded DoS surface and honest TOCTOU framing. The current mini-grep parser exists at `src/plugins/src/manifest_signer.cpp:102-153` and (duplicated) at `src/plugins/src/manifest_signer_win32.cpp:115-153`. The v1.0 partial fix widened the search window but did not address DoS or TOCTOU framing.

Phase 7 delivers: (1) shared `loadTrustRoots` impl using nlohmann::json with 1 MB byte cap + 1024 entry cap (Pitfall 7), (2) full mini-grep removal from BOTH TUs in the same commit, (3) `nlohmann::json` PRIVATE-linked to `ajazz_plugins` only (never in `ajazz_core` or any installed public header — COD-031), (4) `vcpkg.json` + CMakeLists + CI pin manifests + COD-031 charter update in the SAME commit as the parser swap, (5) test suite covering BOM / escape sequences / nested structures / embedded NUL bytes (TRUST-03), (6) public-API header doc comment naming the 0600 file-permissions assumption (Pitfall 8 / TRUST-04).

Maps to requirements: TRUST-01 .. TRUST-04 (full text in `.planning/REQUIREMENTS.md`).

</domain>

\<spec_lock>

## Locked by ARCH-01 (do NOT re-discuss)

- **Parser choice:** `nlohmann::json` 3.12.0, single-header.
- **Vendoring:** `FetchContent` (not `vcpkg.json` add — though `vcpkg.json` should also reference it for consistency per ARCH-01's "vcpkg.json + CMakeLists + CI pin manifests in the same commit" rule).
- **Linkage:** `target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)`. **Never appears in `ajazz_core` or any installed public header.**
- **Caps:** 1 MB byte-cap on `readFile(jsonPath)` (fail closed on oversize); 1024 entry-count cap on parser loop.
- **Removal scope:** mini-grep parser fully removed from BOTH `manifest_signer.cpp` AND `manifest_signer_win32.cpp` in the SAME commit (drift between TUs is what re-introduces WR-01).
- **Threat-model framing:** trust_roots.json parsing sits **inside** the sandbox boundary — JSON parser is the right primitive (rejected: in-tree 5-state scanner, simdjson, RapidJSON, RapidYAML).

\</spec_lock>

<decisions>
## Implementation Decisions (locked)

### D-01 — Extract loadTrustRoots to shared `manifest_signer_common.{hpp,cpp}`

ARCH-01 requires removing the mini-grep from both TUs in the same commit. To make drift between the two TUs **structurally impossible**, Phase 7 extracts the shared `loadTrustRoots` impl into a new file:

- **`src/plugins/src/manifest_signer_common.hpp`** — declares `loadTrustRoots` + the `TrustedPublisher` struct that crosses the boundary.
- **`src/plugins/src/manifest_signer_common.cpp`** — single nlohmann-based implementation with byte cap + entry cap + ASAN-safe error handling.
- `manifest_signer.cpp` and `manifest_signer_win32.cpp` both `#include "manifest_signer_common.hpp"` and call into the shared symbol — neither contains its own `loadTrustRoots` body any longer.

**Why structural over enforcement:** The "lockstep removal" rule (ARCH-01 SC1) presumes humans will keep the two impls in sync. CI grep enforcement of "the two files match" works until a reviewer hand-waves "they look close enough". A shared symbol makes drift impossible by construction — the only way to introduce a behavior difference is to add a `#ifdef _WIN32` branch inside `loadTrustRoots`, which a code-reviewer can spot trivially and reject.

**Win32 / POSIX boundary:** the shared impl is platform-agnostic (just JSON parsing + caps). Platform-specific concerns (file-permission verification on POSIX, `LookupAccountSid` checks on Win32) stay in their respective `manifest_signer*.cpp` files and run **before** calling `loadTrustRoots`. The shared impl trusts that its caller has already validated permissions.

**Estimated diff:** ~80 lines new in `manifest_signer_common.{hpp,cpp}`; ~50 lines deleted from each of `manifest_signer.cpp` and `manifest_signer_win32.cpp`; ~10 lines of plumbing per TU (include + call site). Net ~70 line reduction across the three files.

### D-02 — Catch2 test "name-before-key entry resolves to a row": rewrite as "arbitrary key order"

The v1.0 partial-fix Catch2 case (commit message: "fix(plugins): trust-roots accept name-before-key JSON object order (REVIEW WR-01)") asserts behavior that's specific to the mini-grep search-window heuristic. With nlohmann::json (which is JSON-spec compliant by construction), the assertion is technically redundant — but the underlying *behavioral contract* (parser handles arbitrary JSON object key ordering) is real and worth preserving as documentation.

**Rewrite:** rename the test case to `loadTrustRoots: arbitrary key order produces equivalent rows`. Test data: parse `{"name":"X","key":"..."}` AND `{"key":"...","name":"X"}` — both produce identical `TrustedPublisher` rows. Remove any references in test prose to "name-before-key" or "search window" — those were artifacts of the old impl.

**Why not delete:** the existence of the test as a named behavioral contract is the documentation. A future contributor seeing `loadTrustRoots: arbitrary key order produces equivalent rows` understands the expectation. Without it, an over-eager refactor that imposes key ordering (e.g., a fork that switches to a stricter parser) would silently break the contract.

### D-03 — Test corpus shape (Pitfall 7 mitigation verification)

Per TRUST-03 (cover BOM, escape sequences, nested structures, embedded NUL bytes; fuzz corpus runs \<1s on 100 KB inputs), the new test file `tests/unit/test_load_trust_roots.cpp` (or extend `test_manifest_signer.cpp` if the existing test file is still relevant) gets:

**Positive cases** (must parse successfully, produce expected rows):

- Valid trust-roots with 1 entry, 2 entries, 100 entries, 1024 entries (boundary cap).
- BOM-prefixed input (`\xEF\xBB\xBF{...}`).
- Escape sequences in `name` field (`\"`, `\\`, `A`).
- Nested structure variants (rows wrapped in `{"trust_roots": [...]}` vs top-level array).
- Mixed key ordering per D-02.
- Empty trust list (`[]` or `{"trust_roots": []}`).

**Negative cases** (must fail closed with explicit error):

- Oversize input (1.1 MB → fails byte cap).
- 1025-entry input → fails entry count cap.
- Embedded NUL byte mid-string → fails (or sanitizes per nlohmann default; document which).
- Truncated JSON (missing closing `]` or `}`).
- Pathological nesting (`{{{{...10000 deep...}}}}`) → fails fast (nlohmann default depth limit is 1000; rely on it).
- Malformed UTF-8 in `name` field.

**Fuzz corpus** (`tests/fuzz/trust_roots_corpus/`): seed corpus of 10-20 inputs in the 1 KB - 100 KB range; `tests/fuzz/test_load_trust_roots_fuzz.cpp` is a libFuzzer harness. Asserts parser CPU stays bounded (run for 100k iterations, all under 1 second per iteration). Build target `fuzz_load_trust_roots` opt-in via CMake option `AJAZZ_BUILD_FUZZ_TESTS=ON`.

### D-04 — TOCTOU public-API header documentation (Pitfall 8 / TRUST-04)

`manifest_signer_common.hpp` (per D-01) becomes the public-ish header for `loadTrustRoots`. Add a doc comment:

```cpp
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
 *                  walks the in-memory blob. The TOCTOU surface is therefore
 *                  bounded to the read step and the file's permissions
 *                  contract; do not cache the parsed result across calls
 *                  without a file-change-detection mechanism.
 *
 * @param jsonPath  Path to the trust-roots JSON file.
 * @return Parsed list of TrustedPublisher rows (empty if file missing).
 * @throws std::runtime_error on oversize input (>1 MB), too-many entries
 *         (>1024), or malformed JSON.
 */
[[nodiscard]] std::vector<TrustedPublisher>
loadTrustRoots(std::filesystem::path const& jsonPath);
```

Plus mirror the permissions-assumption note in `ManifestSignerConfig::trustedPublishersFile`'s doc comment (existing field) so callers see the constraint at the configuration site too.

### Claude's Discretion

- **`vcpkg.json` entry placement** — alongside other deps under `dependencies:`. Version pin matches the `FetchContent_Declare(GIT_TAG v3.12.0)` value. Both must move together if version changes.
- **CI pin manifests update** — search for any `nlohmann_json` mention in `.github/workflows/` or `vcpkg-configuration.json` and ensure consistency. If none exist, add to whichever is the convention.
- **COD-031 charter update text** — append "ajazz_plugins privately depends on nlohmann::json (PRIVATE-linked, never appears in installed headers)" to the relevant constraint document. If the charter is in PROJECT.md or a separate ARCHITECTURE.md, add there.
- **Linkage verification** — add a CMake `add_test(NAME nlohmann_isolation_check ...)` that greps installed `ajazz_core` headers for `nlohmann` symbols and fails if any leak. Or use `cmake-tidy` / similar header-dep checker if already in use.
- **`std::filesystem::path` vs `std::wstring` Win32 path quirks** — nlohmann's parse-from-file API takes `std::ifstream`, which handles both natively; no special Win32 path conversion needed in `loadTrustRoots` itself.
- **Exception-translating to Result** — if the calling `verifyManifest` uses `Result` enum (not exceptions), `loadTrustRoots` may need a try/catch wrapper that translates `nlohmann::json::parse_error` to a `Result::ParseError` (or equivalent). Planner picks the right pattern based on existing `manifest_signer.cpp` conventions.

</decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### ARCH-01 (LOCKED, MUST-read)

- `.planning/phases/03-architectural-decisions/ARCH-01-parser-choice.md` — full decision rationale, alternatives rejected, what Phase 7 commits to.

### Deferred-fix doc (the spec)

- `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md` — WR-01 entry: partial fix history (window-widening), three replacement options analyzed.

### Pitfalls research (locked design constraints)

- `.planning/research/PITFALLS.md` Pitfall 7 — `loadTrustRoots` parser DoS via deeply nested / pathological JSON. Drives byte cap + entry cap + fuzz corpus.
- `.planning/research/PITFALLS.md` Pitfall 8 — TOCTOU between `loadTrustRoots` read and `verifyManifest` use. Drives the 0600 permissions doc comment.
- `.planning/research/PITFALLS.md` Pitfall 16 — `nlohmann::json` "while you're in there" dep creep on WR-01. Reinforces the PRIVATE-link rule and the same-commit dep+code landing.

### Requirements & roadmap

- `.planning/REQUIREMENTS.md` — TRUST-01..04 verbatim.
- `.planning/ROADMAP.md` Phase 7 success criteria — five contractual SC1..SC5.

### Existing code (touched by this phase)

- `src/plugins/src/manifest_signer.cpp:102-153` — current mini-grep `loadTrustRoots` (POSIX). Body deleted; file `#include`s the new common header and calls into it.
- `src/plugins/src/manifest_signer_win32.cpp:115-153` — current mini-grep `loadTrustRoots` (Win32). Same treatment as POSIX.
- `src/plugins/src/manifest_signer_common.hpp` (NEW) — single declaration of `loadTrustRoots` + `TrustedPublisher` struct.
- `src/plugins/src/manifest_signer_common.cpp` (NEW) — single nlohmann-based impl.
- `src/plugins/CMakeLists.txt` — add `manifest_signer_common.cpp` to sources; add `target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)`; add `FetchContent_Declare(nlohmann_json ...)` if not already present.
- `vcpkg.json` — add `nlohmann-json` entry pinned to 3.12.0.
- `CMakePresets.json` — verify presets that use vcpkg pick up the new dep.
- `tests/unit/test_load_trust_roots.cpp` (NEW or extend `test_manifest_signer.cpp`) — TRUST-03 corpus.
- `tests/fuzz/test_load_trust_roots_fuzz.cpp` (NEW) — libFuzzer harness; opt-in via `AJAZZ_BUILD_FUZZ_TESTS`.
- `tests/fuzz/trust_roots_corpus/` (NEW) — seed corpus.

### Charter / dep-policy docs

- `PROJECT.md` (or wherever COD-031 lives) — append the nlohmann private-dep note.

### Existing test (rewrite per D-02)

- `tests/unit/test_manifest_signer.cpp` — `loadTrustRoots: name-before-key entry resolves to a row` test case rewrites to `loadTrustRoots: arbitrary key order produces equivalent rows`.

\</canonical_refs>

\<code_context>

## Existing Code Insights

### Reusable Assets

- **`readFile(path)` helper** at `manifest_signer.cpp:103` — already reads the file into a string. Phase 7 reuses it but adds the 1 MB byte cap inside (or wraps it). Same call-site shape; new failure mode (oversize) returns empty / throws per the planner's choice.
- **`TrustedPublisher` struct** — already defined; just moves to `manifest_signer_common.hpp` so both TUs see it via the shared header.
- **Existing Catch2 test patterns** in `test_manifest_signer.cpp` — Phase 7's new test file follows the same `TEST_CASE` + `REQUIRE` pattern.
- **`FetchContent_Declare` precedent** — search `CMakeLists.txt` files for any existing `FetchContent_Declare` (Catch2 is likely already vendored this way). Phase 7 follows the same pattern for nlohmann.

### Established Patterns

- **`ajazz_plugins` is Qt-free** per COD-031 — never `#include <Q...>` in headers. nlohmann is `#include <nlohmann/json.hpp>` (no Qt dep).
- **Result enum-based error handling** in `manifest_signer.cpp` — Phase 7's `loadTrustRoots` either translates nlohmann exceptions to `Result::ParseError` or propagates per the existing pattern.
- **`#ifdef _WIN32` for platform-specific code** — `manifest_signer_win32.cpp` is the Windows entry. `manifest_signer_common.cpp` has no `#ifdef _WIN32` blocks (it's platform-agnostic).

### Integration Points

- **`manifest_signer_common.cpp` → both `manifest_signer.cpp` and `manifest_signer_win32.cpp`** — single shared symbol; both TUs include the common header.
- **`verifyManifest` call sites** at `manifest_signer.cpp:183` and `manifest_signer_win32.cpp:187` — keep calling `loadTrustRoots(config.trustedPublishersFile)` exactly as before. Only the impl moves.
- **`ManifestSignerConfig::trustedPublishersFile`** doc comment — mirror the permissions assumption from D-04.

\</code_context>

<specifics>
## Specific Ideas / Anchor Artefacts

- **Per-decision artefact files under `.planning/phases/07-wr-01-trust-roots-parser-hardening/`:**
  - `07-PLAN.md` — gsd-planner output.
  - `07-SUMMARY.md` — gsd-executor output.
  - `07-DEP-MIGRATION.md` (optional) — if the planner wants to capture the vcpkg + FetchContent + COD-031 + CI pin manifest update as a single artefact for review.
- **Atomic commit boundaries** — single commit per ARCH-01 SC1: parser swap + dep add + CMakeLists + vcpkg.json + CI pin manifest + COD-031 charter update + mini-grep removal from both TUs. Subsequent commits: test corpus + fuzz harness (separate, since they're additive). DEP-MIGRATION rules out partial commits.
- **Verification anchor:** the existing `verifyManifest` Catch2 cases must continue to pass unchanged after the swap (existing behavior preserved). New TRUST-03 cases assert the new failure modes (caps, fuzz).

</specifics>

<deferred>
## Deferred Ideas

- **mtime + size + SHA-256 fast-path cache for `loadTrustRoots`** — Pitfall 8 explicitly defers caching across `verifyManifest` calls until file-change detection is in place. v1.2+ candidate.
- **`std::filesystem::permissions()` runtime check inside `loadTrustRoots`** — could verify the file is 0600 before reading. ARCH-01 + D-04 chose to document the assumption rather than enforce at runtime (avoids per-call syscall + cross-platform `permissions()` differences). Reconsider if a future review wants belt-and-braces.
- **Migration path for users with malformed legacy trust-roots files** — the new strict parser may reject inputs the mini-grep tolerated. Phase 7 doesn't ship a migration tool; if user reports surface, add a `--validate-trust-roots` CLI helper.
- **`nlohmann::json::sax_parse` for streaming** — could reduce memory footprint for large trust-roots. The 1 MB byte cap makes this moot; reconsider only if the cap is ever raised.
- **AppArmor / SELinux profile for trust-roots file** — defense-in-depth at the OS layer. Out of scope for v1.1; tracked as a packaging concern.

</deferred>

______________________________________________________________________

*Phase: 07-wr-01-trust-roots-parser-hardening*
*Context gathered: 2026-05-14*
