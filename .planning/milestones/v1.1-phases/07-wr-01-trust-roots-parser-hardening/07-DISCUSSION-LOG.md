# Phase 7: WR-01 Trust-Roots Parser Hardening - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-14
**Phase:** 07-wr-01-trust-roots-parser-hardening
**Areas discussed:** Cross-TU shape, Legacy test
**Mode:** `/gsd-autonomous --interactive` (default discuss-phase mode, lightweight because ARCH-01 + REQUIREMENTS lock most decisions)

______________________________________________________________________

## Cross-TU shape

### Q1 — loadTrustRoots is duplicated across two files. With nlohmann landing, what's the right structure?

| Option                                              | Description                                                                                                                        | Selected |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Extract to manifest_signer_common.cpp shared helper | New shared file holds single nlohmann-based impl. Both TUs call into it. Drift impossible by construction. ~70 line net reduction. | ✓        |
| Keep duplicated, enforce via CI grep                | Two impls, CI checks they match. Maintenance liability remains; grep enforcement is review-bypass-able.                            |          |
| Move to manifest_signer.cpp; Win32 calls into it    | Asymmetric. Risks pulling POSIX into Win32 file (platform-purity violation).                                                       |          |

**User's choice:** Extract to manifest_signer_common.cpp shared helper.
**Notes:** ARCH-01 SC1 ("drift between the two TUs is what re-introduces WR-01") presumes humans will keep two impls in sync. A shared symbol makes drift structurally impossible. Platform-specific concerns (file-permission verification) stay in manifest_signer\*.cpp and run before calling the shared loadTrustRoots.

______________________________________________________________________

## Legacy test

### Q1 — v1.0 partial-fix Catch2 case "name-before-key entry resolves to a row" — what to do with it under nlohmann?

| Option                                      | Description                                                                                                                                                | Selected |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Rewrite as "arbitrary key order" assertions | Behavior pinned (parser handles arbitrary key order) is real and worth preserving as documentation; just rephrase from mini-grep terms to JSON-spec terms. | ✓        |
| Delete the test                             | Tests an artifact of old impl; nlohmann makes redundant. Loses documentation value.                                                                        |          |
| Keep verbatim, rename only                  | Same data, same assertions. Confusing if reviewers look up why this case has its own test.                                                                 |          |

**User's choice:** Rewrite as "arbitrary key order" assertions.
**Notes:** Test data: parse `{"name":"X","key":"..."}` AND `{"key":"...","name":"X"}` → both produce identical TrustedPublisher rows. Documents the contract; protects against future stricter parser refactors.

______________________________________________________________________

## Claude's Discretion

The following were left for Claude / planner to decide:

- **vcpkg.json entry + version pin alignment** — pinned to 3.12.0 to match FetchContent_Declare GIT_TAG.
- **CI pin manifest updates** — search .github/workflows/ for existing nlohmann mentions; align if present, add otherwise.
- **COD-031 charter update text** — append "ajazz_plugins privately depends on nlohmann::json" note to wherever the charter lives (PROJECT.md or ARCHITECTURE.md).
- **Linkage verification mechanism** — CMake test that greps installed ajazz_core headers for nlohmann symbols and fails if found.
- **Exception → Result translation** — try/catch wrapper translates `nlohmann::json::parse_error` to project's Result enum if existing code uses Result-based error handling.
- **Test file location** — new `tests/unit/test_load_trust_roots.cpp` OR extend existing `test_manifest_signer.cpp`; planner picks based on file organization conventions.
- **Fuzz corpus seeds** — 10-20 inputs spanning 1 KB - 100 KB; under `tests/fuzz/trust_roots_corpus/`; opt-in via `AJAZZ_BUILD_FUZZ_TESTS=ON` CMake option.
- **TOCTOU header doc comment text** — full text drafted in CONTEXT.md D-04, planner adopts verbatim or refines.

## Deferred Ideas

- **mtime + size + SHA-256 fast-path cache** — Pitfall 8 explicitly defers; v1.2+ candidate.
- **`std::filesystem::permissions()` runtime check** — chose document-not-enforce per ARCH-01.
- **Migration tool for malformed legacy trust-roots** — add `--validate-trust-roots` CLI helper if user reports surface.
- **`nlohmann::json::sax_parse` streaming** — moot with 1 MB byte cap; reconsider only if cap raises.
- **AppArmor / SELinux profile for trust-roots file** — packaging concern, out of scope for v1.1.
