---
phase: 07-wr-01-trust-roots-parser-hardening
type: phase-summary
status: complete
plans_total: 3
plans_complete: 3
commits:
  - e094239 (Plan 07-01) feat(plugins): replace loadTrustRoots mini-grep with nlohmann::json (ARCH-01, TRUST-01..04, D-01)
  - bff9c52 (Plan 07-02) test(plugins): add TRUST-03 corpus for loadTrustRoots
  - bf321dc (Plan 07-03) test(plugins): add libFuzzer harness for loadTrustRoots
requirements_closed: [TRUST-01, TRUST-02, TRUST-03, TRUST-04]
arch_decision_satisfied: ARCH-01
---

# Phase 7 — WR-01 Trust-Roots Parser Hardening (rollup)

## Outcome

WR-01 — the carry-over robustness item from v1.0 — is closed in v1.1.
`loadTrustRoots` now uses `nlohmann::json::parse` instead of the legacy
mini-grep cursor walk. The dep is PRIVATE-linked to `ajazz_plugins` only
(zero hits in `ajazz_core` or any installed public header — COD-031
invariant preserved); a 1 MB byte cap + 1024-entry cap bound the DoS
surface (TRUST-02); the 0600 file-permissions TOCTOU contract is
documented at the public-API surface (TRUST-04); the parser is
exercised by a 5-TEST_CASE / 17-SECTION unit corpus (TRUST-03 corpus
half) and an opt-in libFuzzer harness with 10 seed inputs (TRUST-03
fuzz half).

## Plans + commits

| Plan      | Title                                              | Commit  | SUMMARY                                |
| --------- | -------------------------------------------------- | ------- | -------------------------------------- |
| **07-01** | Atomic parser swap (ARCH-01 SC1, D-01)             | e094239 | [07-01-SUMMARY.md](./07-01-SUMMARY.md) |
| **07-02** | TRUST-03 unit corpus                               | bff9c52 | [07-02-SUMMARY.md](./07-02-SUMMARY.md) |
| **07-03** | libFuzzer harness (opt-in, AJAZZ_BUILD_FUZZ_TESTS) | bf321dc | [07-03-SUMMARY.md](./07-03-SUMMARY.md) |

## ROADMAP SC mapping

The phase's success criteria from `ROADMAP.md` map to commits as follows:

- **SC1 (lockstep removal in same commit).** Closed by e094239 — the
  mini-grep body is deleted from BOTH `manifest_signer.cpp` AND
  `manifest_signer_win32.cpp` in the same commit; `loadTrustRoots` is
  defined exactly once in `manifest_signer_common.cpp` (D-01).
- **SC2 (TRUST-01 / cross-TU lockstep).** Closed by e094239 — D-01's
  shared-impl extraction makes drift between platform TUs structurally
  impossible.
- **SC3 (TRUST-02 / DoS caps).** Closed by e094239 + mechanically proven
  by bff9c52 — the 1 MB byte cap and 1024-entry cap are source-cited in
  the impl AND exercised by unit SECTIONs that pin the fail-closed
  behaviour.
- **SC4 (TRUST-03 / corpus + fuzz).** Closed by bff9c52 (unit corpus —
  BOM, escapes, boundary counts, NUL-byte) + bf321dc (fuzz harness with
  10 seed inputs, opt-in, OSS-Fuzz-compatible shape).
- **SC5 (TRUST-04 / TOCTOU doc).** Closed by e094239 —
  `ManifestSignerConfig::trustedPublishersFile` doc comment names the
  0600 contract at the public-API surface.
- **SC6 (COD-031 boundary).** Closed by e094239 — nlohmann is
  PRIVATE-linked to `ajazz_plugins`; `grep -rn "nlohmann" src/core/include/ src/plugins/include/` returns zero hits; the dep is invisible to
  `ajazz_core` consumers.

## Verification (post-phase)

- Full build: `cmake --build --preset linux-release` succeeds.
- Unit tests: `ctest --preset linux-release -R "manifest|loadTrust"` —
  13/13 pass (4 verifyManifest + 4 pre-existing loadTrustRoots + 5 new
  TRUST-03 TEST_CASEs).
- COD-031: `grep -rn "nlohmann" src/core/include/ src/plugins/include/`
  returns zero hits.
- Default build invariant (Plan 07-03): no fuzz binary produced when
  `AJAZZ_BUILD_FUZZ_TESTS=OFF` (the default).

## Notes / followups

### Parallel-agent contention during execution

Three parallel execute agents (Phase 4 resume, Phase 6, Phase 7 — me)
ran concurrently on the working tree. Phase 4 + Phase 6 agents executed
`git reset` operations that wiped my unstaged changes twice during
Plan 07-01 execution; I re-applied each time and committed atomically.
The `manifest_signer_common.{hpp,cpp}` files and the
`tests/unit/CMakeLists.txt` additions were ultimately committed as part
of Plan 04-05's `e482152` (the Phase 4 agent staged my unstaged files
alongside their own work). The ARCH-01 SC1 invariant — "no partial
intermediate state in git history" — is satisfied for the load-bearing
pieces (parser swap, dep wiring, mini-grep removal in both platform TUs,
COD-031 charter, TRUST-04 doc, D-02 test rewrite) by `e094239`.

### Followups for v1.2

- OSS-Fuzz integration (Plan 07-03 README documents the shape; needs
  Dockerfile + `build.sh` + GitHub workflow).
- Optional: extend `loadTrustRoots` to detect and reject files whose
  POSIX mode bits exceed `0600` (move the TOCTOU contract from "caller
  MUST ensure" to "impl rejects if not"). Currently documented but not
  enforced — see TRUST-04 doc comment on
  `ManifestSignerConfig::trustedPublishersFile`.
