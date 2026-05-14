---
phase: 07-wr-01-trust-roots-parser-hardening
plan: 02
type: summary
status: complete
commit: bff9c52
new_test_cases: 5
new_sections: 12
total_loadtrustroots_tests: 9
---

# Plan 07-02 — TRUST-03 corpus

## Commit + diffstat

**Commit:** `bff9c52 test(plugins): add TRUST-03 corpus for loadTrustRoots (Plan 07-02)`

```
 tests/unit/CMakeLists.txt                          |   7 +
 tests/unit/fixtures/trust_roots/README.md          |  21 +++   (new)
 tests/unit/test_load_trust_roots.cpp               | 296 +++++++++++   (new)
 3 files changed, 324 insertions(+), 7 deletions(-)
```

Purely additive on top of Plan 07-01's `e094239`. The impl from Plan 07-01
is unchanged; this commit only exercises it. The 4 pre-existing
`loadTrustRoots` TEST_CASEs in `test_manifest_signer.cpp` remain
untouched and continue to pass.

## Per-SECTION wall-clock (`ctest --output-on-failure`)

```
Test #128: loadTrustRoots: positive corpus - boundary entry counts ........ 0.02s
Test #129: loadTrustRoots: positive corpus - text edge cases .............. 0.02s
Test #130: loadTrustRoots: negative corpus - DoS caps fail closed ......... 0.02s
Test #131: loadTrustRoots: negative corpus - malformed input fails closed . 0.02s
Test #132: loadTrustRoots: NUL-byte handling - pinned behaviour ........... 0.02s
Test #161: loadTrustRoots: parses the bundled JSON ........................ 0.02s   (pre-existing)
Test #162: loadTrustRoots: missing file returns empty list ................ 0.02s   (pre-existing)
Test #163: loadTrustRoots: malformed entry never cross-pairs .............. 0.02s   (pre-existing)
Test #164: loadTrustRoots: arbitrary key order produces equivalent rows ... 0.02s   (Plan 07-01 D-02)

100% tests passed, 0 tests failed out of 9 — total 0.15s
```

Well under the TRUST-03 sub-1s-per-iteration budget (which applies to the
fuzz corpus, not the unit corpus). The 1024-entry boundary case — the
slowest synthetic input — completes in ~20 ms on the dev machine.

## Observed behaviours worth pinning

### BOM handling (nlohmann v3.12.0)

**Observed: nlohmann strips BOM transparently.** The 0xEF 0xBB 0xBF
prefix is consumed before parsing; the test asserts `roots.size() == 1`
for a BOM-prefixed valid trust-roots file, and that assertion passes.
No impl-side pre-strip is needed.

### NUL-byte handling (nlohmann v3.12.0)

**This is the most interesting observation from the corpus run.** nlohmann
v3.12.0 treats raw NUL bytes and `�` escapes **asymmetrically**:

- **Raw NUL byte mid-string → REJECTED.** Throws
  `json::parse_error.101`: "invalid string: missing closing quote".
  nlohmann treats 0x00 mid-string as an unterminated-string error
  (consistent with RFC 8259 §7 — NUL is a control character that MUST
  be escaped to appear inside a JSON string). `loadTrustRoots`
  returns empty list — fail closed.

- **`�` escape → ACCEPTED.** nlohmann decodes the 6-character
  escape to a literal NUL byte in the parsed `std::string`. The parsed
  `name` is `"A\0B"` — 3 bytes long, `name[1] == '\0'`.

**Downstream-consumer contract:** any code comparing trust-roots
publisher `name` fields against external input MUST be NUL-tolerant —
use the full `std::string` (which knows its size), NOT a C-string
`.c_str()` strcmp. The Ed25519 publisher `key` field is base64-encoded
so it cannot contain NUL; this is a `name` field invariant only.

The verifyManifest call site at `manifest_signer.cpp:140-149` compares
publisher key strings with `std::string operator==` (the keys are
base64-encoded Ed25519 public keys), which is safe — no `.c_str()` is
involved. And nothing downstream of `verifyManifest` compares publisher
names against anything else; the name is purely an ornament-for-UI value.
So the asymmetry is benign for v1.1 — but documented here in case a
future refactor adds name-based lookups.

### Boundary entry-count

**1024 entries: PARSES.** Cap is inclusive (`> 1024U` rejects, `1024`
is accepted). `roots[1023].keyB64 == "K1023"` confirms file order is
preserved end-to-end.

**1025 entries: REJECTS.** Fails closed (returns empty list). Test
mechanically proves the cap is enforced — not just source-grepped.

### Oversize byte cap

**1.1 MB input: REJECTS.** Read short-circuits at cap+1 byte, sets
`oversize=true`, returns empty list. The temp file is removed at test
end so /tmp doesn't accumulate 1.1 MB blobs across CI runs.

## Impl behaviours that would suggest a follow-up fix

None observed. Every D-03 corpus item lands on the actually-observed
behaviour the impl claims to provide. The NUL-byte asymmetry is a
nlohmann property (and the impl correctly fails-closed on the
problematic case); no impl change is recommended.

## Cross-link

- Plan 07-03 — libFuzzer harness opt-in via `AJAZZ_BUILD_FUZZ_TESTS=ON`,
  10-seed corpus. Closes the fuzz-corpus half of TRUST-03 ("fuzz corpus
  runs \<1s on 100 KB inputs"); additive on top of this commit.
