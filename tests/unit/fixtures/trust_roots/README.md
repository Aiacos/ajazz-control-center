# Trust-roots test fixtures

This directory is intentionally empty (apart from this README).

Trust-root test inputs for `tests/unit/test_load_trust_roots.cpp` are
constructed at test time in `std::filesystem::temp_directory_path()` and
removed after each `TEST_CASE` / `SECTION` finishes. There are **no
checked-in binary fixtures** here for two reasons:

1. **Avoid confusion with real config.** The bundled `resources/trusted_publishers.json`
   is the production trust-roots file; checking in similarly-shaped fixtures
   here would invite accidental cross-references during refactors.

1. **Reproducibility.** Building inputs programmatically in the test (rather
   than committing JSON files) keeps each test's input definition next to
   its assertions — easier to read, harder for the fixture and the
   assertion to drift out of sync.

The fuzz corpus seeds — also Phase 7, Plan 07-03 — live separately under
`tests/fuzz/trust_roots_corpus/` because libFuzzer needs the seeds on
disk at run time (`./fuzz_load_trust_roots trust_roots_corpus/`).
