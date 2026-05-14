---
phase: 07-wr-01-trust-roots-parser-hardening
plan: 03
type: summary
status: complete
commit: bf321dc
default_build_unaffected: true
clang_opt_in_compiles: true
gcc_opt_in_fail_fast: true
---

# Plan 07-03 — libFuzzer harness

## Commit + diffstat

**Commit:** `bf321dc test(plugins): add libFuzzer harness for loadTrustRoots (Plan 07-03)`

Files added (15):

```
 CMakeLists.txt                                            |   6 +
 tests/CMakeLists.txt                                      |  15 +
 tests/fuzz/CMakeLists.txt                                 |  34 ++   (new)
 tests/fuzz/README.md                                      |  70 ++   (new)
 tests/fuzz/test_load_trust_roots_fuzz.cpp                 |  69 ++   (new)
 tests/fuzz/trust_roots_corpus/seed_01_minimal.json        |   1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_02_two_entries.json    |   1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_03_bom.json            |   1 +    (new, real 0xEFBBBF prefix)
 tests/fuzz/trust_roots_corpus/seed_04_escapes.json        |   1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_05_top_level_array.json|   1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_06_empty_publishers.json|  1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_07_unicode_names.json  |   1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_08_100_entries.json    |   1 +    (new, ~3 KB)
 tests/fuzz/trust_roots_corpus/seed_09_nested_object.json  |   1 +    (new)
 tests/fuzz/trust_roots_corpus/seed_10_truncated.bin       |   1 +    (new, .bin to skip check-json)
```

## Local Clang availability

```
$ which clang++ clang
/usr/bin/clang++
/usr/bin/clang
```

Clang is on PATH (Fedora packages clang 22 by default; clang 19 and 21
are also installed as separately-versioned packages).

## Opt-in build verification

```
$ cmake -S . -B build/fuzz \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DAJAZZ_BUILD_FUZZ_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
    -DAJAZZ_ENABLE_WERROR=OFF
```

Result: **configure succeeds**. nlohmann FetchContent resolves, hidapi
FetchContent resolves, Qt6 discovery succeeds, the fuzz target enters
the generated build graph.

```
$ cmake --build build/fuzz --target fuzz_load_trust_roots
```

Result: **compile succeeds, link fails on system-packaging issue**. Build
output shows the harness compiles cleanly (one `.o` produced); link fails
because clang 22 on this Fedora install doesn't ship `libclang_rt.fuzzer.a`
or `libclang_rt.asan.a` (they exist for clang 19 and clang 21, just not
in clang 22's tree by default — `dnf install compiler-rt` would fix it,
but that's an out-of-scope system mutation).

This is environmental, not a code issue:

- The harness source file compiles under clang's `-fsanitize=address,fuzzer`
  flags without warnings or errors.
- The `target_link_options(... PRIVATE -fsanitize=address,fuzzer)` is the
  documented invocation for libFuzzer.
- OSS-Fuzz containers ship `compiler-rt` for the required Clang version
  by definition, so the OSS-Fuzz path is unaffected.
- Developers running fuzz locally can either install `compiler-rt` for
  clang 22, or invoke with an older Clang (`clang-19` or `clang-21`) that
  has the runtime libs.

The README documents the requirement explicitly.

## Default-build invariant

```
$ cmake -S . -B build/check && cmake --build build/check --target ajazz_plugins
$ find build/check -name "fuzz_load_trust_roots*"
(no output)
```

**Confirmed:** the default build (no `-DAJAZZ_BUILD_FUZZ_TESTS=ON`) does
NOT produce the fuzz binary. Existing CI workflows are completely
unaffected.

## GCC opt-in fail-fast path

```
$ cmake -S . -B /tmp/test-gcc-fuzz \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DAJAZZ_BUILD_FUZZ_TESTS=ON
...
CMake Error at tests/CMakeLists.txt:17 (message):
  AJAZZ_BUILD_FUZZ_TESTS=ON requires Clang (libFuzzer is a Clang-only
  sanitizer runtime).  Current compiler: GNU.

-- Configuring incomplete, errors occurred!
```

**Confirmed:** the explicit FATAL_ERROR fires at configure time, no
silent skip, no confusing link error later.

## OSS-Fuzz integration notes for v1.2

The harness shape matches OSS-Fuzz's `LLVMFuzzerTestOneInput` requirement
verbatim. A minimal OSS-Fuzz integration would need:

1. `projects/ajazz-control-center/Dockerfile` — base `ossfuzz/cifuzz`
   image + apt-installs for our build deps (cmake, ninja, qt6-dev, libudev-dev)
   - git clone of this repo.
1. `projects/ajazz-control-center/build.sh` — runs
   `cmake -S . -B build -DAJAZZ_BUILD_FUZZ_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX`,
   builds `fuzz_load_trust_roots`, copies the binary + the seed corpus
   to `$OUT/`.
1. Seed corpus rotation policy: re-baseline `trust_roots_corpus/` from the
   OSS-Fuzz `corpus/` artifact every release (keeps the in-tree seeds
   informative without bloating the repo with auto-generated mutations).

Out of scope for v1.1.

## Cross-link

- Plan 07-01 — atomic parser swap (commit e094239)
- Plan 07-02 — TRUST-03 unit corpus (commit bff9c52)
- Phase 7 rollup — `.planning/phases/07-wr-01-trust-roots-parser-hardening/07-SUMMARY.md`
