# tests/fuzz/CMakeLists.txt
# libFuzzer harnesses for plugin-host security primitives.
# Opt-in via -DAJAZZ_BUILD_FUZZ_TESTS=ON; Clang-only (gated in tests/CMakeLists.txt).
#
# Phase 7 / Plan 07-03 — closes the fuzz-corpus half of TRUST-03.

add_executable(fuzz_load_trust_roots test_load_trust_roots_fuzz.cpp)

# The harness #includes manifest_signer_common.hpp which lives under src/plugins/src/ (private
# include path of ajazz_plugins). Mirror the same access the unit tests use.
target_include_directories(fuzz_load_trust_roots PRIVATE ${CMAKE_SOURCE_DIR}/src/plugins/src)

# Link against ajazz_plugins to pick up loadTrustRoots + its nlohmann private dep.
target_link_libraries(fuzz_load_trust_roots PRIVATE ajazz::plugins)

# libFuzzer + ASan — the recommended fuzz config. PRIVATE so these flags never propagate to the rest
# of the build (the option is gated by AJAZZ_BUILD_FUZZ_TESTS, but PRIVATE adds belt-and-suspenders
# isolation).
target_compile_options(fuzz_load_trust_roots PRIVATE -fsanitize=address,fuzzer)
target_link_options(fuzz_load_trust_roots PRIVATE -fsanitize=address,fuzzer)

# Seed corpus is checked into the repo at trust_roots_corpus/. Copy it next to the binary at build
# time so `./fuzz_load_trust_roots trust_roots_corpus/` works post-build.
add_custom_command(
    TARGET fuzz_load_trust_roots
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/trust_roots_corpus
            $<TARGET_FILE_DIR:fuzz_load_trust_roots>/trust_roots_corpus
    COMMENT "Staging libFuzzer seed corpus next to fuzz_load_trust_roots binary"
    VERBATIM
)
