// SPDX-License-Identifier: GPL-3.0-or-later
// libFuzzer harness for ajazz::plugins::loadTrustRoots.
// Opt-in via -DAJAZZ_BUILD_FUZZ_TESTS=ON (Clang-only).
//
// Build:
//   cmake -S . -B build/fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
//                            -DAJAZZ_BUILD_FUZZ_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
//   cmake --build build/fuzz --target fuzz_load_trust_roots
//
// Run:
//   ./build/fuzz/tests/fuzz/fuzz_load_trust_roots \
//       -max_total_time=120 -max_len=131072 \
//       trust_roots_corpus/
//
// Performance budget (TRUST-03 / CONTEXT.md D-03): each input <=100 KB
// completes in <1 second. The harness has no sleeps, no I/O beyond the
// input blob (one temp-file write + the loadTrustRoots read), and no
// dynamic memory amplification beyond what loadTrustRoots itself does
// (capped at 1 MB / 1024 entries per Plan 07-01).

#include "manifest_signer_common.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

// Per-iteration scratch path. Using a fixed filename (not mkstemp) is fine
// here because libFuzzer is single-threaded per process. ASan + libFuzzer
// catches any cross-iteration state leak.
std::filesystem::path scratchPath() {
    static auto const p = std::filesystem::temp_directory_path() / "ajazz-fuzz-trust-roots.json";
    return p;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size) {
    // Write the fuzzer's input blob to disk so we exercise the production
    // code path (loadTrustRoots reads from file, then parses). The 100 KB
    // budget from TRUST-03 keeps the I/O hop fast on tmpfs.
    auto const path = scratchPath();
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return 0;
        }
        out.write(reinterpret_cast<char const*>(data), static_cast<std::streamsize>(size));
    }

    // Drop the result on the floor — we're fuzzing for crashes, not output
    // validity. The byte cap (1 MB), entry cap (1024), and parse_error catch
    // in loadTrustRoots mean no input should ever cause this call to throw
    // or crash; libFuzzer + ASan flag any violation.
    auto const roots = ajazz::plugins::loadTrustRoots(path);
    (void)roots;

    // Cleanup scratch file. If unlink fails libFuzzer continues — /tmp is
    // tmpfs on typical Linux installs and gets cleaned at OS level anyway.
    std::error_code ec;
    std::filesystem::remove(path, ec);

    return 0;
}
