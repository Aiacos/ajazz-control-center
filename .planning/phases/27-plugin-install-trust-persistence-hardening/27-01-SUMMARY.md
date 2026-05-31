---
phase: 27-plugin-install-trust-persistence-hardening
plan: '01'
subsystem: plugin-verifier
tags: [security, plugins, ed25519, tdd]
dependency_graph:
  requires: []
  provides: [SignatureState-enum, VerifyVerdict-Unsigned, CR-01-installFromFile-gate]
  affects: [src/plugins/include/ajazz/plugins/manifest_signer.hpp, src/app/src/plugin_verify_gate.hpp, src/app/src/plugin_catalog_model.cpp]
tech_stack:
  added: []
  patterns: [TDD-RED-GREEN, Catch2-QZipWriter-fixtures, CR-01-consent-gate]
key_files:
  created: []
  modified:
    - src/plugins/include/ajazz/plugins/manifest_signer.hpp
    - src/plugins/src/manifest_signer.cpp
    - src/plugins/src/manifest_signer_win32.cpp
    - src/app/src/plugin_verify_gate.hpp
    - src/app/src/plugin_verify_gate.cpp
    - src/app/src/plugin_catalog_model.cpp
    - tests/unit/test_manifest_signer.cpp
    - tests/unit/test_plugin_verify_gate.cpp
    - tests/unit/test_plugin_install_from_file.cpp
decisions:
  - SignatureState field added to ManifestVerifyResult alongside existing valid bool for backward compat (valid == signatureState==Valid invariant)
  - 'Early-return for unsigned manifests (no block): skip subprocess spawn, classify None immediately'
  - 'Fail-closed branches (verifier unavailable) classify by signature-block presence: None if no block, Invalid if block present but unverifiable'
  - verifyStagedPlugin fail-closed (empty verifierScript) returns Unsigned not Refused, so no-Python builds can consent-install developer plugins
  - Refused in verdictToTrustLevel now maps to 'tampered' (was 'unsigned'); Unsigned maps to 'unsigned'
  - 'Re-verify after cross-fs copy checks only Refused (tampered): consent was already granted at first check, so Unsigned on re-verify proceeds'
metrics:
  duration: ~25 minutes
  completed: '2026-05-31'
  tasks_completed: 2
  files_modified: 9
---

# Phase 27 Plan 01: Plugin Verifier Unsigned/Tampered Split Summary

Split the manifest verifier's single `Refused` verdict into `Unsigned` (no signature block — developer sideload) and `Refused` (signature block present but Ed25519-invalid — tampered attack package), enforcing the CR-01 security invariant: a tampered `.sdPlugin` is ALWAYS quarantined even when `userConfirmedUnsigned==true`.

## Tasks Completed

### Task 1: Add SignatureState to ManifestVerifyResult and classify None vs Invalid

**TDD RED commit:** `416ccc7` — failing tests for SignatureState in test_manifest_signer.cpp
**TDD GREEN commit:** `be70145` — implementation + fix (fs::path{} not ambiguous)

Added `enum class SignatureState { None, Valid, Invalid }` to `manifest_signer.hpp` and `SignatureState signatureState{SignatureState::None}` field to `ManifestVerifyResult`. Both POSIX (`manifest_signer.cpp`) and Win32 (`manifest_signer_win32.cpp`) backends updated with identical classification logic:

- Read manifest blob before any early-return path so signatureState is always populated
- Detect signature-block presence: BOTH `Ed25519Signature` AND `Ed25519PublicKey` must be present
- No block -> `SignatureState::None`; return early (skip subprocess spawn)
- Block present, verifier exits non-zero -> `SignatureState::Invalid`
- Verifier exits 0 -> `SignatureState::Valid`
- Fail-closed branches (missing script, unresolvable interpreter) classify by block presence

**4 new Catch2 tests verified passing:** unsigned->None, tampered->Invalid, valid->Valid, unavailable-verifier-classifies-by-block-presence.

### Task 2: Map SignatureState to VerifyVerdict::Unsigned and gate installFromFile

**TDD RED commit:** `bface27` — failing tests for Unsigned split and CR-01 consent gate
**TDD GREEN commit:** `8f37961` — implementation

`plugin_verify_gate.hpp`: Added `VerifyVerdict::Unsigned` between `SelfSigned` and `Refused`. `Refused` now means cryptographically-invalid only (tampered). Updated `verdictToTrustLevel`: `Unsigned -> "unsigned"`, `Refused -> "tampered"`.

`plugin_verify_gate.cpp`: Switch on `result.signatureState`:

- `None -> VerifyVerdict::Unsigned` ("manifest is unsigned")
- `Invalid -> VerifyVerdict::Refused` ("signature verification failed -- tampered")
- `Valid -> SelfSigned or Trusted` (publisher lookup unchanged)
- Fail-closed (empty verifierScript) -> `VerifyVerdict::Unsigned` (no-Python build consent path)

`plugin_catalog_model.cpp::installFromFile`:

- `Refused` block: CR-01 comment updated; `userConfirmedUnsigned` does NOT appear here (tampered = always quarantine)
- New `Unsigned` block: promote if `userConfirmedUnsigned || untrustedPluginsAllowed()`; otherwise quarantine with "unsigned plugin -- confirm to install"
- Stale "NOTE: verifyStagedPlugin cannot yet distinguish..." CR-01 comment removed (split resolved)

**5 new Catch2 tests verified passing:**

- `PluginVerifyGate unsigned manifest -> Unsigned`
- `PluginVerifyGate verdictToTrustLevel: Unsigned maps to unsigned`
- `PluginVerifyGate verdictToTrustLevel: Refused maps to tampered`
- `PluginInstallFromFile tampered plugin refused even with consent` (CR-01 security gate)
- `PluginInstallFromFile unsigned plugin refused without consent`
- `PluginInstallFromFile unsigned plugin installs with consent`

## Verification Results

```
ctest --preset linux-release -E qml
100% tests passed, 0 tests failed out of 696
```

Baseline: ~645. New count: 696 (51 new tests landed during Phase 27-01 + prior session growth).

```
ctest --preset linux-release -R "Verify|ManifestSigner|InstallFromFile" -E qml
100% tests passed, 0 tests failed out of 23
```

```
grep -rn "#include.*nlohmann" src/core/include/
(no output — COD-031 boundary intact)
```

## Deviations from Plan

None - plan executed exactly as written.

The one minor deviation was a Catch2 test compile fix: `cfg.trustedPublishersFile = {};` was ambiguous for `std::filesystem::path` under GCC 16's stricter overload resolution; fixed to `cfg.trustedPublishersFile = fs::path{};`. This is a Rule 1 (bug fix) auto-correction within the RED commit.

The "signer unavailable" test in `test_plugin_verify_gate.cpp` was updated: the old test expected `Refused` but after the split, an unsigned manifest with a non-existent verifier returns `Unsigned` (None signatureState). This is correct behavior — the test was asserting old pre-split semantics.

## Known Stubs

None — all changes are behavioral/security logic with no stub placeholders.

## Threat Flags

No new network endpoints, auth paths, or file access patterns introduced. The changes harden an existing trust boundary (filesystem .sdPlugin -> app) by splitting what was previously a single refusal verdict into an attack-safe classification.

## Self-Check: PASSED

Files exist:

- src/plugins/include/ajazz/plugins/manifest_signer.hpp: contains SignatureState enum
- src/app/src/plugin_verify_gate.hpp: contains VerifyVerdict::Unsigned
- tests/unit/test_manifest_signer.cpp: contains signatureState assertions
- tests/unit/test_plugin_verify_gate.cpp: contains VerifyVerdict::Unsigned assertions
- tests/unit/test_plugin_install_from_file.cpp: contains "tampered plugin refused even with consent"

Commits exist (verified via git log):

- 416ccc7: test(27-01): RED - SignatureState tests
- be70145: feat(27-01): GREEN - SignatureState implementation
- bface27: test(27-01): RED - Unsigned split + CR-01 tests
- 8f37961: feat(27-01): GREEN - Unsigned split + CR-01 implementation
