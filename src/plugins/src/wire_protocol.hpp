// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file wire_protocol.hpp
 * @brief Mini JSON encoder + parser shared by every OS backend of the
 *        @ref OutOfProcessPluginHost.
 *
 * Library-internal header (lives under `src/plugins/src/`, not the
 * public include tree). Both `out_of_process_plugin_host.cpp` (POSIX)
 * and `out_of_process_plugin_host_win32.cpp` (Windows) include it so
 * the wire-format details are encoded exactly once. Pure logic, no
 * platform dependencies — `inline` for ODR safety across multiple
 * including TUs.
 *
 * The protocol is one flat JSON object per line (host -> child and
 * child -> host). Strings are escaped per JSON rules; arrays of
 * strings are supported for the `permissions` field surfaced by
 * slice 3a. Numbers, nested objects, and `null` are intentionally
 * NOT supported — anything richer than a flat string-or-string-array
 * object indicates the wire format has grown beyond this parser's
 * design and the parser should be replaced rather than extended.
 *
 * @par Why purpose-built rather than `nlohmann::json`?
 *
 * - The plugins library is intentionally Qt-free and dep-free
 *   (matches the slice-3e cleanup that dropped pybind11 + Python3
 *   embed). Adding nlohmann::json just for one-line JSON would put
 *   us back into the dependency-bloat hole the OOP migration
 *   eliminated.
 * - The child uses Python's stdlib `json` module so the producing
 *   side is already correct; the host parser only has to handle a
 *   well-known shape, not arbitrary JSON.
 */
#pragma once

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ajazz::plugins::wire {

/// Encode a string value into JSON. Output excludes the surrounding
/// quotes; caller wraps. Escapes `"`, `\\`, control chars, and
/// 0x7f-0xff via `\u00XX` (the input is `std::string`, treated as
/// UTF-8 bytes).
inline std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char const ch : value) {
        auto const c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

/// Build a flat JSON object `{"op":"<name>"}`, optionally with extra
/// string-valued fields. Each field becomes `,"<key>":"<jsonEscape value>"`
/// in declaration order. Numbers and arrays are not supported because
/// nothing in the wire protocol needs them on the host->child path.
inline std::string
buildOp(std::string_view opName,
        std::initializer_list<std::pair<std::string_view, std::string_view>> fields = {}) {
    std::string out{"{\"op\":\""};
    out += jsonEscape(opName);
    out += "\"";
    for (auto const& [key, value] : fields) {
        out += ",\"";
        out += jsonEscape(key);
        out += "\":\"";
        out += jsonEscape(value);
        out += "\"";
    }
    out += "}";
    return out;
}

/// Decode the body of a JSON-string-literal starting at @p i (i.e.
/// @p line[i] is the opening `"` already). Returns the decoded value
/// and updates @p i to point past the closing `"`. Tolerates simple
/// escapes (`\"`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f`); does NOT
/// decode `\uXXXX`. Returns empty string if the literal is malformed.
inline std::string decodeQuotedString(std::string_view line, std::size_t& i) {
    if (i >= line.size() || line[i] != '"') {
        return {};
    }
    ++i; // past opening quote
    std::string value;
    while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < line.size()) {
            char const next = line[i + 1];
            switch (next) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            default:
                value.push_back(next);
                break;
            }
            i += 2;
        } else {
            value.push_back(line[i]);
            ++i;
        }
    }
    if (i < line.size() && line[i] == '"') {
        ++i; // past closing quote
    }
    return value;
}

/// Locate `"<key>":` in @p line, advance past optional whitespace,
/// and return the index where the value starts. Returns
/// @c std::string_view::npos if the key is not present.
inline std::size_t locateFieldValue(std::string_view line, std::string_view key) {
    std::string needle{"\""};
    needle += key;
    needle += "\":";
    auto const pos = line.find(needle);
    if (pos == std::string_view::npos) {
        return std::string_view::npos;
    }
    auto i = pos + needle.size();
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    return i;
}

/// Find `"key":"<value>"` in a JSON line. Returns the decoded value
/// or empty string if not found / value is not a string.
inline std::string findStringField(std::string_view line, std::string_view key) {
    auto i = locateFieldValue(line, key);
    if (i == std::string_view::npos || i >= line.size() || line[i] != '"') {
        return {};
    }
    return decodeQuotedString(line, i);
}

/// Find `"key":["a","b",...]` in a JSON line. Returns the decoded
/// list or an empty vector if the key is missing / value is not an
/// array of strings.
inline std::vector<std::string> findStringArrayField(std::string_view line, std::string_view key) {
    std::vector<std::string> out;
    auto i = locateFieldValue(line, key);
    if (i == std::string_view::npos || i >= line.size() || line[i] != '[') {
        return out;
    }
    ++i; // past opening bracket
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == ',')) {
            ++i;
        }
        if (i >= line.size() || line[i] == ']') {
            break;
        }
        if (line[i] != '"') {
            break; // non-string element — wire contract violated
        }
        out.push_back(decodeQuotedString(line, i));
    }
    return out;
}

} // namespace ajazz::plugins::wire
