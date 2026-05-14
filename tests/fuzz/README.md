# Fuzz harnesses

libFuzzer harnesses for plugin-host security primitives. Opt-in build —
the default CI matrix is unaffected.

Phase 7 / Plan 07-03 — closes the fuzz-corpus half of TRUST-03
("fuzz corpus runs \<1s on 100 KB inputs").

## Building

Requires Clang (libFuzzer is a Clang-only sanitizer runtime). GCC/MSVC
builds will fail at configure time with an explicit error message if
`AJAZZ_BUILD_FUZZ_TESTS=ON` is passed.

```bash
cmake -S . -B build/fuzz \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DAJAZZ_BUILD_FUZZ_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/fuzz --target fuzz_load_trust_roots
```

## Running

```bash
cd build/fuzz/tests/fuzz
./fuzz_load_trust_roots \
  -max_total_time=120 \
  -max_len=131072 \
  trust_roots_corpus/
```

`-max_len=131072` (128 KB) is well above the 100 KB TRUST-03 budget,
leaving slack for libFuzzer to expand inputs without immediately hitting
the 1 MB byte cap inside `loadTrustRoots`.

## Performance budget (TRUST-03)

Each input \<=100 KB MUST complete in \<1 second. The harness has no
sleeps, no extra I/O beyond one temp-file write per iteration, and no
dynamic memory amplification beyond what the impl itself does
(capped at 1 MB / 1024 entries — see `src/plugins/src/manifest_signer_common.cpp`).

## Adding to OSS-Fuzz

The harness shape (`LLVMFuzzerTestOneInput` in a single `.cpp` file,
opt-in via CMake option) is OSS-Fuzz-compatible. Wiring is out of scope
for v1.1; track in `PROJECT.md` as a candidate v1.2 follow-up.

## Seed corpus

`trust_roots_corpus/` contains 10 hand-crafted seeds covering the JSON
shapes `loadTrustRoots` accepts and rejects:

| Seed                          | Shape                 | Expected `loadTrustRoots` result |
| ----------------------------- | --------------------- | -------------------------------- |
| seed_01_minimal.json          | 1 entry               | 1 row                            |
| seed_02_two_entries.json      | 2 entries             | 2 rows                           |
| seed_03_bom.json              | BOM-prefixed          | 1 row                            |
| seed_04_escapes.json          | JSON escape sequences | 1 row                            |
| seed_05_top_level_array.json  | top-level array       | 1 row                            |
| seed_06_empty_publishers.json | empty list            | 0 rows                           |
| seed_07_unicode_names.json    | UTF-8 multi-byte      | 1 row                            |
| seed_08_100_entries.json      | 100 entries           | 100 rows                         |
| seed_09_nested_object.json    | wrong shape           | empty (logged)                   |
| seed_10_truncated.bin         | malformed             | empty (parse_error caught)       |

The truncated seed has a `.bin` extension rather than `.json` so the
repo's `check-json` pre-commit hook doesn't try to parse it (it's
deliberately invalid). libFuzzer doesn't care about file extensions.
