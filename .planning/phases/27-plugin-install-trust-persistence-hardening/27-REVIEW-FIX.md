---
phase: 27
slug: plugin-install-trust-persistence-hardening
status: resolved
reviewed: 2026-05-31
findings_total: 7
findings_fixed: 5
findings_deferred: 2
final_test_count: 713
---

# Phase 27 — Code Review Fix Record

Source review: `27-REVIEW.md` (status `issues_found`: 0 Critical, 4 Warning, 3 Info). The CR-01 security invariant was independently confirmed to hold by the reviewer across every promotion path. Fixes applied by `gsd-code-fixer` (sequential on main tree, reusing `build/linux-release`). Final suite: **713/713 pass** (+2 vs the 711 baseline — the new WR-01 tests).

## Fixed

| Finding                                                                                                                                                                           | Severity       | Commit    | Resolution                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| WR-01 — per-plugin "Allow this plugin" was a dead feature (wrote `plugins/allowed/<uuid>` that nothing read; sweep deleted the plugin on restart unless the global toggle was on) | Warning        | `45ab674` | Added `perPluginAllowed(uuid)` consulted in BOTH the launch-sweep Unsigned branch AND the `installFromFile` Unsigned gate; `allowPlugin()` emits `installFinished` to drive `rediscover()`. Per-plugin allow now survives restart + makes the plugin runnable with the global toggle OFF. **Unsigned-only** — 2 new tests prove (i) survival with toggle OFF, (ii) a tampered plugin is NOT promotable even with a forged `plugins/allowed` key (CR-01 preserved). |
| WR-02 — Win32 verifier handed a bare PATH-searched `python3` to `_wspawnvp` (CWE-426 PATH-hijack could forge a Valid verdict); asymmetric with the POSIX fail-closed guard        | Warning        | `a1922ef` | Mirrored the POSIX guard: reject a non-concrete interpreter path → `SignatureState::Invalid`; switched to `_wspawnv` (no PATH search). Windows-only — matched the POSIX contract (not locally buildable).                                                                                                                                                                                                                                                          |
| WR-03 — Refused branch leaked the `.plugin_staging` parent dir                                                                                                                    | Warning        | `d540159` | Added `rmdir(".")` on the staging parent, mirroring the Unsigned/SelfSigned branches.                                                                                                                                                                                                                                                                                                                                                                              |
| WR-04 — implicit "dirs are pre-verified" invariant on `installFinished`→`rediscover()`                                                                                            | Warning        | `abb5f6f` | Documentation-only: contract comments at `rediscover()` and the `application.cpp` connection site (idempotent; trusts pre-verified dirs).                                                                                                                                                                                                                                                                                                                          |
| IN-01 — `SignatureState` enum base size (clang-tidy `performance-enum-size`)                                                                                                      | Info (trivial) | `dcd18c4` | `enum class SignatureState : std::uint8_t` + `<cstdint>`.                                                                                                                                                                                                                                                                                                                                                                                                          |

## Deferred (Info — out of fix scope)

- **IN-02** — `entryFor()` omits `downloadUrl`. Cosmetic; no behavior impact.
- **IN-03** — cross-fs copy-fallback recurses only one directory level (breaks plugins with nested asset trees on a cross-filesystem install). Touches the CR-02 copy-fallback path; warrants its own test + plan rather than a blind change. Tracked for a future minor sweep.

## CR-01 invariant — confirmed preserved post-fix

The now-functional per-plugin path is Unsigned-only. `perPluginAllowed` is consulted exclusively inside the two Unsigned branches; `allowPlugin()` refuses `Refused`/tampered rows and writes no key; the launch-sweep Refused branch stays unconditional-quarantine. The pre-existing `tampered refused even with consent` and `allowUnsignedPlugins=true tampered STILL refused` tests pass, plus the new `per-plugin allow does not promote a tampered plugin (CR-01)`.
