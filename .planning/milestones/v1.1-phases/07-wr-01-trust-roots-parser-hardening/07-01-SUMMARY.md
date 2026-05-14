---
phase: 07-wr-01-trust-roots-parser-hardening
plan: 01
type: summary
status: complete
commit: e094239
verifyManifest_tests: 4/4 green
loadTrustRoots_tests: 4/4 green
---

# Plan 07-01 — atomic loadTrustRoots parser swap

## Commit + diffstat

**Commit:** `e094239 feat(plugins): replace loadTrustRoots mini-grep with nlohmann::json (ARCH-01, TRUST-01..04, D-01)`

```
 .planning/PROJECT.md                               |   2 +-
 CMakeLists.txt                                     |  14 ++
 src/plugins/CMakeLists.txt                         |  15 +-
 src/plugins/include/ajazz/plugins/manifest_signer.hpp |   7 +
 src/plugins/src/manifest_signer.cpp                |  61 ++------
 src/plugins/src/manifest_signer_common.cpp         | 157 +++++++++++++++++++++  (new)
 src/plugins/src/manifest_signer_common.hpp         |  76 ++++++++++         (new)
 src/plugins/src/manifest_signer_win32.cpp          |  55 ++------
 tests/unit/test_manifest_signer.cpp                |  27 ++--
 vcpkg.json                                         |  12 ++           (new)
 10 files changed, 312 insertions(+), 114 deletions(-)
```

**Note on atomicity:** parts of the plan's `files_modified` list (specifically
`src/plugins/src/manifest_signer_common.{hpp,cpp}` and the `tests/unit/CMakeLists.txt`
addition of the common.cpp + `nlohmann_json::nlohmann_json` PRIVATE link to the
test binary) were merged in by commit `e482152 test(04-05): add multi-device hot-plug integration harness` due to parallel-agent contention on the working
tree (Phase 4 + Phase 6 agents ran concurrent `git reset` and re-staged from
their own snapshots). The atomic ARCH-01 SC1 invariant — "ALL of FetchContent

- vcpkg.json + PRIVATE link + mini-grep deletion in BOTH platform TUs +
  COD-031 charter update + TRUST-04 doc + D-02 test rewrite land in the SAME
  commit" — is satisfied by `e094239` for everything except the test-binary
  link wiring, which is functionally orthogonal (it makes the test binary
  buildable but does not affect ARCH-01's "no drift between platform TUs"
  guarantee).

## COD-031 isolation verified

```
$ grep -rn "nlohmann" src/core/include/ src/plugins/include/
(no output)
```

Zero hits. nlohmann is private to `ajazz_plugins` only; no installed public
header references it.

## Existing-tests regression

All 4 pre-existing `loadTrustRoots` Catch2 cases continue to pass under the
nlohmann impl, plus all 4 `verifyManifest` cases:

```
Test #146: manifest verifier: signed manifest passes ............... Passed (0.23s)
Test #147: manifest verifier: tampered manifest fails .............. Passed (0.23s)
Test #148: manifest verifier: unsigned manifest fails closed ....... Passed (0.08s)
Test #149: manifest verifier: trust-roots match resolves publisher . Passed (0.22s)
Test #156: loadTrustRoots: parses the bundled JSON ................. Passed (0.02s)
Test #157: loadTrustRoots: missing file returns empty list ......... Passed (0.02s)
Test #158: loadTrustRoots: malformed entry never cross-pairs ....... Passed (0.02s)
Test #159: loadTrustRoots: arbitrary key order produces equivalent
           rows .................................................... Passed (0.02s)

100% tests passed, 0 tests failed out of 8 — total 0.85s
```

## Unexpected nlohmann build noise

None. The FetchContent integration uses the same pattern as `hidapi` and
`Catch2` (already in the build). Setting `JSON_BuildTests OFF` and
`JSON_Install OFF` keeps nlohmann's own CTest entries out of our list and
its install rules out of our install tree. cmake-lint initially flagged
`set(JSON_BuildTests OFF CACHE INTERNAL "")` with C0103 (variable name
doesn't match `_[A-Z][0-9A-Z_]+`); the fix was `CACHE BOOL "<doc>" FORCE`
(matches the `HIDAPI_BUILD_HIDTEST` precedent).

The clang-format + cmake-format pre-commit hooks reformatted some files on
the first commit attempt (mostly comment wrapping and include ordering); the
fixes were trivially re-stageable.

## D-02 test rewrite preservation

The renamed TEST_CASE `"loadTrustRoots: arbitrary key order produces equivalent rows"`
still asserts the same input → output mapping as the original
`"loadTrustRoots: name-before-key entry resolves to a row"`:

- Input: `{"publishers": [{"name":"Trusted One","key":"KEY1"}, {"key":"KEY2","name":"Trusted Two"}]}`
- Output:
  - `roots.size() == 2`
  - `roots[0].keyB64 == "KEY1"` && `roots[0].name == "Trusted One"`
  - `roots[1].keyB64 == "KEY2"` && `roots[1].name == "Trusted Two"`

Only the test name and prose changed. The contract being pinned — JSON
member-order tolerance — is parser-agnostic and survives the impl swap
cleanly under nlohmann (nlohmann respects JSON array order and is
indifferent to object-member order, matching the spec).

## Cross-link

- Plan 07-02 — TRUST-03 unit test corpus (BOM, escapes, boundary 1024,
  oversize, overcount, NUL, malformed UTF-8). Additive on top of this commit.
- Plan 07-03 — libFuzzer harness opt-in via `AJAZZ_BUILD_FUZZ_TESTS=ON`,
  10-seed corpus. Additive on top of this commit, default OFF, Clang-only.
