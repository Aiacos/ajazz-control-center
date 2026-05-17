// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_load_trust_roots.cpp
 * @brief TRUST-03 corpus for `ajazz::plugins::loadTrustRoots` — positive cases
 *        (BOM, escape sequences, top-level array, boundary entry counts),
 *        negative cases (DoS caps fail closed, malformed input fails closed),
 *        and pinned NUL-byte behaviour. Phase 7 / Plan 07-02 / CONTEXT.md D-03.
 *
 * Layered additively on top of Plan 07-01's nlohmann-based impl. Does NOT
 * modify the impl; only exercises it. All test inputs are constructed at
 * test time in `std::filesystem::temp_directory_path()` and removed after
 * each TEST_CASE - no checked-in binary fixtures.
 */
#include "ajazz/plugins/manifest_signer.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;
using ajazz::plugins::loadTrustRoots;
using ajazz::plugins::TrustedPublisher;

namespace {

/// Write @p content to @p path in binary mode (no newline injection). Used by
/// every SECTION below so the on-disk bytes match the test's intent exactly
/// — important for the BOM and embedded-NUL cases.
void writeBinary(fs::path const& path, std::string_view content) {
    std::ofstream f{path, std::ios::binary | std::ios::trunc};
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

/// Build {"publishers":[{"key":"K0","name":"N0"},...,{"key":"K<n-1>","name":"N<n-1>"}]}
/// as a single std::string - used by the boundary-count SECTIONs and the
/// overcount negative SECTION.
std::string makePublishersWithCount(std::size_t count) {
    std::ostringstream oss;
    oss << R"({"publishers":[)";
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << R"({"key":"K)" << i << R"(","name":"N)" << i << R"("})";
    }
    oss << "]}";
    return oss.str();
}

/// Scratch path under temp_directory_path with a unique suffix so concurrent
/// CTest workers don't collide. The TEST_CASE removes it at the end.
fs::path scratchFile(std::string_view tag) {
    auto p = fs::temp_directory_path() / (std::string{"ajazz-test-trustroots-"} + std::string{tag});
    return p;
}

} // namespace

TEST_CASE("loadTrustRoots: positive corpus - boundary entry counts",
          "[manifest-signer][trust-roots]") {
    SECTION("1 entry parses to 1 row") {
        auto const path = scratchFile("count-1");
        writeBinary(path, R"({"publishers":[{"key":"K1","name":"N1"}]})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 1);
        REQUIRE(roots[0].keyB64 == "K1");
        REQUIRE(roots[0].name == "N1");
        fs::remove(path);
    }

    SECTION("2 entries parse to 2 rows in file order") {
        auto const path = scratchFile("count-2");
        writeBinary(path, R"({"publishers":[{"key":"KA","name":"NA"},{"key":"KB","name":"NB"}]})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 2);
        REQUIRE(roots[0].keyB64 == "KA");
        REQUIRE(roots[1].keyB64 == "KB");
        fs::remove(path);
    }

    SECTION("100 entries parse to 100 rows") {
        auto const path = scratchFile("count-100");
        writeBinary(path, makePublishersWithCount(100));
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 100);
        REQUIRE(roots[0].keyB64 == "K0");
        REQUIRE(roots[99].keyB64 == "K99");
        REQUIRE(roots[99].name == "N99");
        fs::remove(path);
    }

    SECTION("1024 entries (boundary cap) parses to 1024 rows") {
        // Exactly at the cap — MUST succeed, not fail closed. The cap rejects
        // only >1024; 1024 is inclusive (Plan 07-01).
        auto const path = scratchFile("count-1024");
        writeBinary(path, makePublishersWithCount(1024));
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 1024);
        REQUIRE(roots[1023].keyB64 == "K1023");
        fs::remove(path);
    }
}

TEST_CASE("loadTrustRoots: positive corpus - text edge cases", "[manifest-signer][trust-roots]") {
    SECTION("BOM-prefixed input parses (nlohmann strips BOM)") {
        // File starts with 0xEF 0xBB 0xBF (UTF-8 BOM) then valid JSON.
        // nlohmann v3.x is documented to handle BOM transparently. If this
        // SECTION fails, the impl needs an explicit pre-strip step.
        auto const path = scratchFile("bom");
        std::string content;
        content.push_back('\xEF');
        content.push_back('\xBB');
        content.push_back('\xBF');
        content += R"({"publishers":[{"key":"BOMK","name":"BOM Publisher"}]})";
        writeBinary(path, content);
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 1);
        REQUIRE(roots[0].keyB64 == "BOMK");
        REQUIRE(roots[0].name == "BOM Publisher");
        fs::remove(path);
    }

    SECTION("escape sequences in name field round-trip correctly") {
        // JSON escapes: \" -> ", \\ -> \, \t -> tab, \n -> newline.
        auto const path = scratchFile("escapes");
        writeBinary(path, R"({"publishers":[{"key":"K","name":"He said \"hi\" \\ A\tB\nC"}]})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 1);
        REQUIRE(roots[0].name == "He said \"hi\" \\ A\tB\nC");
        fs::remove(path);
    }

    SECTION("empty publishers list returns empty vector") {
        auto const path = scratchFile("empty-publishers");
        writeBinary(path, R"({"publishers":[]})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }

    SECTION("top-level array shape is also accepted (fallback shape from Plan 07-01)") {
        auto const path = scratchFile("top-array");
        writeBinary(path, R"([{"key":"TLA","name":"Top Level Array"}])");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 1);
        REQUIRE(roots[0].keyB64 == "TLA");
        fs::remove(path);
    }

    SECTION("empty top-level array returns empty vector") {
        auto const path = scratchFile("empty-array");
        writeBinary(path, "[]");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }
}

TEST_CASE("loadTrustRoots: negative corpus - DoS caps fail closed",
          "[manifest-signer][trust-roots]") {
    SECTION("input >1 MB returns empty list (TRUST-02 byte cap)") {
        // Build {"publishers":[{"key":"K","name":"<1.1 MB of A>"}]} — the
        // single name field is ~1.1 MB so the file blows past kMaxTrustRootsBytes.
        // The impl short-circuits the read at cap+1 byte and returns empty.
        auto const path = scratchFile("oversize");
        std::ostringstream oss;
        oss << R"({"publishers":[{"key":"K","name":")";
        constexpr std::size_t kFillSize = 1100000U; // 1.1 MB
        oss << std::string(kFillSize, 'A');
        oss << R"("}]})";
        writeBinary(path, oss.str());
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path); // don't leave 1.1 MB in /tmp
    }

    SECTION("input with 1025 entries returns empty list (TRUST-02 entry cap)") {
        // 1025 entries — each ~30 bytes so total ~30 KB, well under the
        // byte cap. Tests the entry-count cap in isolation.
        auto const path = scratchFile("overcount");
        writeBinary(path, makePublishersWithCount(1025));
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }
}

TEST_CASE("loadTrustRoots: negative corpus - malformed input fails closed",
          "[manifest-signer][trust-roots]") {
    SECTION("truncated JSON (missing closing braces) returns empty list") {
        auto const path = scratchFile("truncated");
        writeBinary(path, R"({"publishers":[{"key":"K","name":"N"})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }

    SECTION("malformed UTF-8 in name field returns empty list") {
        // Inject raw bytes 0xC0 0xC0 (invalid UTF-8 start-byte sequence)
        // inside the name string. nlohmann's strict UTF-8 mode rejects.
        auto const path = scratchFile("bad-utf8");
        std::string content = R"({"publishers":[{"key":"K","name":")";
        content.push_back('\xC0');
        content.push_back('\xC0');
        content += R"("}]})";
        writeBinary(path, content);
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }

    SECTION("wrong top-level shape (object without publishers, not array) returns empty list") {
        auto const path = scratchFile("wrong-shape");
        writeBinary(path, R"({"foo":"bar","baz":42})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }

    SECTION("non-object entry in publishers array is skipped") {
        // Mixed array: one valid object, one bare string, one valid object.
        // Per impl: non-object entries are skipped silently — the valid
        // entries are still emitted.
        auto const path = scratchFile("mixed-types");
        writeBinary(
            path,
            R"({"publishers":[{"key":"K1","name":"N1"},"not-an-object",{"key":"K2","name":"N2"}]})");
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 2);
        REQUIRE(roots[0].keyB64 == "K1");
        REQUIRE(roots[1].keyB64 == "K2");
        fs::remove(path);
    }
}

TEST_CASE("loadTrustRoots: NUL-byte handling - pinned behaviour",
          "[manifest-signer][trust-roots]") {
    // CONTEXT.md D-03: "Embedded NUL byte mid-string -> fails (or sanitizes per
    // nlohmann default; document which)." This TEST_CASE pins the actually-
    // observed behaviour on nlohmann v3.12.0 — a future upgrade that changes
    // either pin breaks CI loudly.
    //
    // Observed (verified 2026-05-14, nlohmann v3.12.0):
    //
    //   1. Raw NUL byte (0x00) inside a JSON string literal: REJECTED with
    //      `parse_error.101` — "invalid string: missing closing quote".
    //      nlohmann treats 0x00 mid-string as an unterminated-string error
    //      (consistent with RFC 8259 sec. 7's grammar: NUL is a control
    //      character that MUST be escaped). loadTrustRoots returns empty
    //      list — fail closed.
    //
    //   2. \u0000 escape inside a JSON string literal: ACCEPTED. nlohmann
    //      decodes \u0000 to a literal NUL byte in the parsed std::string.
    //      The parsed `name` is "A" + NUL + "B" — 3 bytes long, name[1]=='\0'.
    //      Downstream consumers comparing names against trust-roots blobs
    //      MUST be NUL-tolerant (use the full std::string, NOT a C-string
    //      `.c_str()` strcmp). Ed25519 keys are base64 so they cannot contain
    //      NUL; this is a `name` field invariant only.

    SECTION("raw NUL byte mid-string is rejected (fails closed)") {
        auto const path = scratchFile("nul-byte-raw");
        std::string content = R"({"publishers":[{"key":"K","name":"A)";
        content.push_back('\0'); // raw NUL byte inside the JSON string
        content += R"(B"}]})";
        writeBinary(path, content);
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.empty());
        fs::remove(path);
    }

    SECTION("backslash-u0000 escape decodes to literal NUL in parsed name") {
        auto const path = scratchFile("nul-byte-escape");
        // R"(...)" raw literal preserves the literal 6-character sequence
        // \u0000; nlohmann decodes it to a NUL byte in the parsed string.
        std::string content = "{\"publishers\":[{\"key\":\"K\",\"name\":\"A\\u0000B\"}]}";
        writeBinary(path, content);
        auto const roots = loadTrustRoots(path);
        REQUIRE(roots.size() == 1);
        REQUIRE(roots[0].keyB64 == "K");
        REQUIRE(roots[0].name.size() == 3);
        REQUIRE(roots[0].name[0] == 'A');
        REQUIRE(roots[0].name[1] == '\0');
        REQUIRE(roots[0].name[2] == 'B');
        fs::remove(path);
    }
}
