// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile.cpp
 * @brief Hand-rolled JSON writer + reader for the core profile schema.
 *
 * The goal is to avoid a heavyweight JSON dependency in the core library
 * (COD-031 forbids nlohmann::json in ajazz_core, and the design keeps Qt
 * out of the core so the unit tests can run without an event loop).
 *
 * The reader is intentionally narrow: it parses only the format produced by
 * profileToJson() in this file. It is NOT a general-purpose JSON parser. It
 * tolerates extra whitespace and unknown keys (forward compatibility) but
 * rejects malformed input with a descriptive std::runtime_error.
 *
 * Only ASCII-safe strings are supported. Unicode in user labels should be
 * validated upstream before being stored in a Profile.
 */
#include "ajazz/core/profile.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ajazz::core {

namespace {

/**
 * @brief Write an Rgb value as a JSON array `[r,g,b]`.
 *
 * @param out Destination stream.
 * @param c   Color to serialise.
 */
[[maybe_unused]] void writeRgb(std::ostringstream& out, Rgb const& c) {
    out << "[" << static_cast<int>(c.r) << "," << static_cast<int>(c.g) << ","
        << static_cast<int>(c.b) << "]";
}

/**
 * @brief Write a JSON-escaped quoted string.
 *
 * Handles the minimal set of escape sequences required by RFC 8259:
 * double-quote, backslash, newline, carriage-return, and horizontal tab.
 *
 * @param out Destination stream.
 * @param s   Input string; must be ASCII or UTF-8.
 */
void escape(std::ostringstream& out, std::string_view s) {
    out << '"';
    for (char const ch : s) {
        switch (ch) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    out << '"';
}

/// Stringify an @ref ActionKind for JSON.
char const* actionKindName(ActionKind k) noexcept {
    switch (k) {
    case ActionKind::Plugin:
        return "plugin";
    case ActionKind::Sleep:
        return "sleep";
    case ActionKind::KeyPress:
        return "key";
    case ActionKind::RunCommand:
        return "command";
    case ActionKind::OpenUrl:
        return "url";
    case ActionKind::OpenFolder:
        return "openFolder";
    case ActionKind::BackToParent:
        return "back";
    }
    return "plugin";
}

/**
 * @brief Serialise a single Action to JSON object notation.
 *
 * Output: `{"kind":<str>,"id":<str>,"settings":<str>,"label":<str>,"delayMs":<int>}`
 *
 * @param out Destination stream.
 * @param a   Action to serialise.
 */
void writeAction(std::ostringstream& out, Action const& a) {
    out << "{";
    out << "\"kind\":\"" << actionKindName(a.kind) << "\",";
    out << "\"id\":";
    escape(out, a.id);
    out << ",";
    out << "\"settings\":";
    escape(out, a.settingsJson);
    out << ",";
    out << "\"label\":";
    escape(out, a.label);
    out << ",\"delayMs\":" << a.delayMs;
    out << "}";
}

/**
 * @brief Serialise a Binding (onPress/onRelease/onLongPress chains).
 *
 * Output: `{"onPress":[...],"onRelease":[...],"onLongPress":[...]}`
 *
 * @param out Destination stream.
 * @param b   Binding to serialise.
 */
/// Helper: emit a chain of actions as a JSON array under a named key.
void writeChain(std::ostringstream& out, std::string_view key, std::vector<Action> const& actions) {
    out << "\"" << key << "\":[";
    bool first = true;
    for (auto const& a : actions) {
        if (!first) {
            out << ",";
        }
        writeAction(out, a);
        first = false;
    }
    out << "]";
}

void writeBinding(std::ostringstream& out, Binding const& b) {
    out << "{";
    writeChain(out, "onPress", b.onPress);
    out << ",";
    writeChain(out, "onRelease", b.onRelease);
    out << ",";
    writeChain(out, "onLongPress", b.onLongPress);
    out << "}";
}

/// Serialise an EncoderBinding (CW / CCW / Press chains).
void writeEncoderBinding(std::ostringstream& out, EncoderBinding const& b) {
    out << "{";
    writeChain(out, "onCw", b.onCw);
    out << ",";
    writeChain(out, "onCcw", b.onCcw);
    out << ",";
    writeChain(out, "onPress", b.onPress);
    out << "}";
}

} // namespace

std::string profileToJson(Profile const& profile) {
    std::ostringstream out;
    out << "{";
    out << "\"id\":";
    escape(out, profile.id);
    out << ",";
    out << "\"name\":";
    escape(out, profile.name);
    out << ",";
    out << "\"device\":";
    escape(out, profile.deviceCodename);
    out << ",";

    out << "\"keys\":{";
    bool first = true;
    for (auto const& [idx, binding] : profile.keys) {
        if (!first) {
            out << ",";
        }
        out << "\"" << idx << "\":";
        writeBinding(out, binding);
        first = false;
    }
    out << "}";

    // Encoders (CW / CCW / Press chains).
    out << ",\"encoders\":{";
    first = true;
    for (auto const& [idx, eb] : profile.encoders) {
        if (!first) {
            out << ",";
        }
        out << "\"" << idx << "\":";
        writeEncoderBinding(out, eb);
        first = false;
    }
    out << "}";

    // Folder pages (root keys live above; child pages live here).
    out << ",\"pages\":{";
    first = true;
    for (auto const& [pageId, page] : profile.pages) {
        if (!first) {
            out << ",";
        }
        escape(out, pageId);
        out << ":{";
        out << "\"name\":";
        escape(out, page.name);
        out << ",\"keys\":{";
        bool kfirst = true;
        for (auto const& [idx, binding] : page.keys) {
            if (!kfirst) {
                out << ",";
            }
            out << "\"" << idx << "\":";
            writeBinding(out, binding);
            kfirst = false;
        }
        out << "},\"children\":[";
        bool cfirst = true;
        for (auto const& child : page.children) {
            if (!cfirst) {
                out << ",";
            }
            escape(out, child);
            cfirst = false;
        }
        out << "]}";
        first = false;
    }
    out << "}";

    out << ",\"applicationHints\":[";
    first = true;
    for (auto const& hint : profile.applicationHints) {
        if (!first) {
            out << ",";
        }
        escape(out, hint);
        first = false;
    }
    out << "]}";
    return out.str();
}

namespace {

/**
 * @brief Tiny recursive-descent JSON reader narrowed to the profile schema.
 *
 * Not a general-purpose JSON parser. It accepts the format produced by
 * profileToJson() above, plus arbitrary whitespace and unknown keys (forward
 * compatibility). Anything else triggers a std::runtime_error with a byte
 * offset, which @ref readProfileFromDisk re-wraps as a ProfileIoError.
 */
class JsonReader {
public:
    explicit JsonReader(std::string_view src) noexcept : src_(src) {}

    void skipWs() {
        while (pos_ < src_.size()) {
            char const c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                return;
            }
        }
    }

    [[nodiscard]] bool eof() const noexcept { return pos_ >= src_.size(); }

    [[nodiscard]] char peek() {
        skipWs();
        if (eof()) {
            fail("unexpected end of input");
        }
        return src_[pos_];
    }

    void expect(char c) {
        if (peek() != c) {
            fail(std::string{"expected '"} + c + "' but got '" + src_[pos_] + "'");
        }
        ++pos_;
    }

    [[nodiscard]] bool tryConsume(char c) {
        skipWs();
        if (!eof() && src_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::string readString() {
        expect('"');
        std::string out;
        while (pos_ < src_.size()) {
            char const c = src_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    fail("dangling escape sequence in string");
                }
                char const esc = src_[pos_++];
                switch (esc) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'u':
                    // Minimal BMP \uXXXX support: copy through as UTF-8 if ASCII range,
                    // else leave as ?-substitute. Real Unicode payloads are not expected
                    // in the profile schema (label strings are validated upstream).
                    if (pos_ + 4 > src_.size()) {
                        fail("truncated \\uXXXX escape");
                    }
                    {
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char const h = src_[pos_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') {
                                code |= static_cast<unsigned>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                code |= static_cast<unsigned>(h - 'a' + 10);
                            } else if (h >= 'A' && h <= 'F') {
                                code |= static_cast<unsigned>(h - 'A' + 10);
                            } else {
                                fail("invalid hex digit in \\uXXXX escape");
                            }
                        }
                        if (code < 0x80U) {
                            out.push_back(static_cast<char>(code));
                        } else if (code < 0x800U) {
                            out.push_back(static_cast<char>(0xC0U | (code >> 6)));
                            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
                        } else {
                            out.push_back(static_cast<char>(0xE0U | (code >> 12)));
                            out.push_back(static_cast<char>(0x80U | ((code >> 6) & 0x3FU)));
                            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
                        }
                    }
                    break;
                default:
                    fail(std::string{"unknown escape sequence \\"} + esc);
                }
            } else {
                out.push_back(c);
            }
        }
        fail("unterminated string");
    }

    [[nodiscard]] std::uint32_t readUInt() {
        skipWs();
        std::size_t const start = pos_;
        if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) {
            ++pos_;
        }
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ == start) {
            fail("expected integer");
        }
        std::string const tok{src_.substr(start, pos_ - start)};
        try {
            return static_cast<std::uint32_t>(std::stoul(tok));
        } catch (std::exception const&) {
            fail("integer out of range: " + tok);
        }
    }

    /// Skip a complete JSON value (string, number, array, object, true, false, null).
    void skipValue() {
        char const c = peek();
        if (c == '"') {
            (void)readString();
        } else if (c == '{') {
            ++pos_;
            if (!tryConsume('}')) {
                while (true) {
                    (void)readString();
                    expect(':');
                    skipValue();
                    if (tryConsume(',')) {
                        continue;
                    }
                    expect('}');
                    break;
                }
            }
        } else if (c == '[') {
            ++pos_;
            if (!tryConsume(']')) {
                while (true) {
                    skipValue();
                    if (tryConsume(',')) {
                        continue;
                    }
                    expect(']');
                    break;
                }
            }
        } else if (c == 't' || c == 'f' || c == 'n') {
            // true / false / null - consume keyword.
            while (pos_ < src_.size() && src_[pos_] >= 'a' && src_[pos_] <= 'z') {
                ++pos_;
            }
        } else {
            // Number (possibly with sign, decimal point, exponent).
            while (pos_ < src_.size()) {
                char const k = src_[pos_];
                if ((k >= '0' && k <= '9') || k == '-' || k == '+' || k == '.' || k == 'e' ||
                    k == 'E') {
                    ++pos_;
                } else {
                    break;
                }
            }
        }
    }

    [[noreturn]] void fail(std::string const& msg) const {
        std::ostringstream err;
        err << "profileFromJson: " << msg << " at byte " << pos_;
        throw std::runtime_error(err.str());
    }

private:
    std::string_view src_;
    std::size_t pos_{0};
};

ActionKind actionKindFromString(std::string_view s) noexcept {
    if (s == "sleep") {
        return ActionKind::Sleep;
    }
    if (s == "key") {
        return ActionKind::KeyPress;
    }
    if (s == "command") {
        return ActionKind::RunCommand;
    }
    if (s == "url") {
        return ActionKind::OpenUrl;
    }
    if (s == "openFolder") {
        return ActionKind::OpenFolder;
    }
    if (s == "back") {
        return ActionKind::BackToParent;
    }
    return ActionKind::Plugin;
}

Action readAction(JsonReader& r) {
    Action a{};
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "kind") {
                a.kind = actionKindFromString(r.readString());
            } else if (key == "id") {
                a.id = r.readString();
            } else if (key == "settings") {
                a.settingsJson = r.readString();
            } else if (key == "label") {
                a.label = r.readString();
            } else if (key == "delayMs") {
                a.delayMs = r.readUInt();
            } else {
                r.skipValue();
            }
            if (r.tryConsume(',')) {
                continue;
            }
            r.expect('}');
            break;
        }
    }
    return a;
}

std::vector<Action> readActionArray(JsonReader& r) {
    std::vector<Action> out;
    r.expect('[');
    if (!r.tryConsume(']')) {
        while (true) {
            out.push_back(readAction(r));
            if (r.tryConsume(',')) {
                continue;
            }
            r.expect(']');
            break;
        }
    }
    return out;
}

Binding readBinding(JsonReader& r) {
    Binding b{};
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "onPress") {
                b.onPress = readActionArray(r);
            } else if (key == "onRelease") {
                b.onRelease = readActionArray(r);
            } else if (key == "onLongPress") {
                b.onLongPress = readActionArray(r);
            } else {
                r.skipValue();
            }
            if (r.tryConsume(',')) {
                continue;
            }
            r.expect('}');
            break;
        }
    }
    return b;
}

EncoderBinding readEncoderBinding(JsonReader& r) {
    EncoderBinding eb{};
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "onCw") {
                eb.onCw = readActionArray(r);
            } else if (key == "onCcw") {
                eb.onCcw = readActionArray(r);
            } else if (key == "onPress") {
                eb.onPress = readActionArray(r);
            } else {
                r.skipValue();
            }
            if (r.tryConsume(',')) {
                continue;
            }
            r.expect('}');
            break;
        }
    }
    return eb;
}

/// Read a `{"<uint>":<value>, ...}` map; @p readValue consumes one value.
template <typename Value, typename ReadValueFn>
void readUintKeyedMap(JsonReader& r, std::unordered_map<std::uint16_t, Value>& dst,
                      ReadValueFn&& readValue) {
    r.expect('{');
    if (r.tryConsume('}')) {
        return;
    }
    while (true) {
        std::string const idxStr = r.readString();
        std::uint16_t idx = 0;
        try {
            idx = static_cast<std::uint16_t>(std::stoul(idxStr));
        } catch (std::exception const&) {
            std::ostringstream err;
            err << "profileFromJson: invalid uint16 map key \"" << idxStr << "\"";
            throw std::runtime_error(err.str());
        }
        r.expect(':');
        dst.emplace(idx, readValue(r));
        if (r.tryConsume(',')) {
            continue;
        }
        r.expect('}');
        return;
    }
}

ProfilePage readPage(JsonReader& r, std::string id) {
    ProfilePage p{};
    p.id = std::move(id);
    r.expect('{');
    if (r.tryConsume('}')) {
        return p;
    }
    while (true) {
        std::string const key = r.readString();
        r.expect(':');
        if (key == "name") {
            p.name = r.readString();
        } else if (key == "keys") {
            readUintKeyedMap(r, p.keys, [](JsonReader& rr) { return readBinding(rr); });
        } else if (key == "children") {
            r.expect('[');
            if (!r.tryConsume(']')) {
                while (true) {
                    p.children.push_back(r.readString());
                    if (r.tryConsume(',')) {
                        continue;
                    }
                    r.expect(']');
                    break;
                }
            }
        } else {
            r.skipValue();
        }
        if (r.tryConsume(',')) {
            continue;
        }
        r.expect('}');
        return p;
    }
}

} // namespace

Profile profileFromJson(std::string_view json) {
    Profile profile{};
    JsonReader r{json};
    r.expect('{');
    if (r.tryConsume('}')) {
        return profile;
    }
    while (true) {
        std::string const key = r.readString();
        r.expect(':');
        if (key == "id") {
            profile.id = r.readString();
        } else if (key == "name") {
            profile.name = r.readString();
        } else if (key == "device") {
            // Schema wire-key for Profile::deviceCodename (per PROFILE_SCHEMA.md).
            profile.deviceCodename = r.readString();
        } else if (key == "keys") {
            readUintKeyedMap(r, profile.keys,
                             [](JsonReader& rr) { return readBinding(rr); });
        } else if (key == "encoders") {
            readUintKeyedMap(r, profile.encoders,
                             [](JsonReader& rr) { return readEncoderBinding(rr); });
        } else if (key == "pages") {
            r.expect('{');
            if (!r.tryConsume('}')) {
                while (true) {
                    std::string pageId = r.readString();
                    r.expect(':');
                    profile.pages.emplace(pageId, readPage(r, pageId));
                    if (r.tryConsume(',')) {
                        continue;
                    }
                    r.expect('}');
                    break;
                }
            }
        } else if (key == "applicationHints") {
            r.expect('[');
            if (!r.tryConsume(']')) {
                while (true) {
                    profile.applicationHints.push_back(r.readString());
                    if (r.tryConsume(',')) {
                        continue;
                    }
                    r.expect(']');
                    break;
                }
            }
        } else {
            r.skipValue();
        }
        if (r.tryConsume(',')) {
            continue;
        }
        r.expect('}');
        break;
    }
    return profile;
}

} // namespace ajazz::core
