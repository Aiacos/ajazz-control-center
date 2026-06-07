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
#include <cstdio>
#include <limits>
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
void writeRgb(std::ostringstream& out, Rgb const& c) {
    out << "[" << static_cast<int>(c.r) << "," << static_cast<int>(c.g) << ","
        << static_cast<int>(c.b) << "]";
}

/**
 * @brief Write a JSON-escaped quoted string.
 *
 * Handles the escape sequences required by RFC 8259: double-quote, backslash,
 * the named controls (\b \f \n \r \t), and \u00XX for every remaining control
 * character in U+0000..U+001F. Without the \u00XX fallback, raw control bytes
 * (NUL, 0x01..0x1F) would be emitted verbatim, producing invalid JSON that any
 * strict reader (or a NUL-truncating C-string consumer) would reject or mangle
 * (WR-01).
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
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
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
            // char may be signed; test the unsigned value so 0x80..0xFF UTF-8
            // continuation bytes are NOT mistaken for control characters.
            if (static_cast<unsigned char>(ch) < 0x20U) {
                char buf[7];
                std::snprintf(buf,
                              sizeof buf,
                              "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(ch)));
                out << buf;
            } else {
                out << ch;
            }
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

/// True when a KeyState carries no user-set visuals (all optionals empty and
/// fontSize at its struct default). Such states are omitted from the wire so
/// bindings that never set visuals don't bloat the profile JSON; the reader
/// leaves an absent state default-constructed, so the round-trip is exact.
[[nodiscard]] bool keyStateIsDefault(KeyState const& s) noexcept {
    return !s.imagePath && !s.text && !s.background && !s.foreground &&
           s.fontSize == KeyState{}.fontSize;
}

/// Serialise a KeyState as `{"imagePath":..,"text":..,"background":[r,g,b],
/// "foreground":[r,g,b],"fontSize":n}`, emitting only the present fields per
/// docs/protocols/PROFILE_SCHEMA.md $defs.KeyState.
void writeKeyState(std::ostringstream& out, KeyState const& s) {
    out << "{";
    bool first = true;
    auto const sep = [&] {
        if (!first) {
            out << ",";
        }
        first = false;
    };
    if (s.imagePath) {
        sep();
        out << "\"imagePath\":";
        escape(out, *s.imagePath);
    }
    if (s.text) {
        sep();
        out << "\"text\":";
        escape(out, *s.text);
    }
    if (s.background) {
        sep();
        out << "\"background\":";
        writeRgb(out, *s.background);
    }
    if (s.foreground) {
        sep();
        out << "\"foreground\":";
        writeRgb(out, *s.foreground);
    }
    sep();
    out << "\"fontSize\":" << static_cast<int>(s.fontSize);
    out << "}";
}

// Forward declaration: writeBinding / writeEncoderBinding emit an optional
// instance via writeActionInstance, which is defined later (after the KeyState
// helpers) to sit next to its readActionInstance counterpart.
void writeActionInstance(std::ostringstream& out, ActionInstance const& inst);

void writeBinding(std::ostringstream& out, Binding const& b) {
    out << "{";
    writeChain(out, "onPress", b.onPress);
    out << ",";
    writeChain(out, "onRelease", b.onRelease);
    out << ",";
    writeChain(out, "onLongPress", b.onLongPress);
    if (!keyStateIsDefault(b.state)) {
        out << ",\"state\":";
        writeKeyState(out, b.state);
    }
    if (b.instance) {
        out << ",\"instance\":";
        writeActionInstance(out, *b.instance);
    }
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
    if (!keyStateIsDefault(b.state)) {
        out << ",\"state\":";
        writeKeyState(out, b.state);
    }
    if (b.instance) {
        out << ",\"instance\":";
        writeActionInstance(out, *b.instance);
    }
    out << "}";
}

/// Serialise a TouchZoneBinding (onTap chain; Phase 26 D-11 / schema v2).
/// Hand-rolled per COD-031: no nlohmann::json in ajazz_core.
void writeTouchZoneBinding(std::ostringstream& out, TouchZoneBinding const& b) {
    out << "{";
    writeChain(out, "onTap", b.onTap);
    if (!keyStateIsDefault(b.state)) {
        out << ",\"state\":";
        writeKeyState(out, b.state);
    }
    out << "}";
}

/// Serialise a single ActionState (Phase 31, BIND-01). A states[] element is
/// ALWAYS emitted as a non-empty $defs.KeyState object (writeKeyState always
/// emits fontSize), which is correct for an array element that must be present
/// even when visually blank.
void writeActionState(std::ostringstream& out, ActionState const& s) {
    writeKeyState(out, s.visual);
}

/// Serialise an ActionInstance (Phase 31, BIND-01). The writer ALWAYS emits
/// the `states` array form (never the legacy singular `state` — only the
/// reader understands that, Pitfall 2). `id` and `settings` are emitted only
/// when non-empty; `children` recurses.
void writeActionInstance(std::ostringstream& out, ActionInstance const& inst) {
    out << "{";
    bool first = true;
    auto const sep = [&] {
        if (!first) {
            out << ",";
        }
        first = false;
    };
    if (!inst.id.empty()) {
        sep();
        out << "\"id\":";
        escape(out, inst.id);
    }
    sep();
    out << "\"states\":[";
    {
        bool firstState = true;
        for (auto const& st : inst.states) {
            if (!firstState) {
                out << ",";
            }
            writeActionState(out, st);
            firstState = false;
        }
    }
    out << "]";
    sep();
    out << "\"currentState\":" << inst.currentState;
    if (!inst.settings.empty()) {
        sep();
        out << "\"settings\":";
        escape(out, inst.settings);
    }
    sep();
    out << "\"children\":[";
    {
        bool firstChild = true;
        for (auto const& child : inst.children) {
            if (!firstChild) {
                out << ",";
            }
            writeActionInstance(out, child); // RECURSIVE (Multi Action nesting).
            firstChild = false;
        }
    }
    out << "]";
    out << "}";
}

} // namespace

std::string profileToJson(Profile const& profile) {
    std::ostringstream out;
    out << "{";
    out << "\"id\":";
    escape(out, profile.id);
    // Schema version bump: v2 adds the touchZones map (Phase 26 D-12).
    // Reader defaults to v1 (empty touchZones) when this field is absent,
    // so existing v1 files remain readable without modification.
    out << ",\"_schemaVersion\":2";
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

    // Touch-strip zones (onTap chain; schema v2, Phase 26 D-11).
    // Emitted as an empty object when no zones are configured so that
    // round-trips on v2 profiles always include the key (T-26-07: unknown
    // future _schemaVersion values are treated as v2 — read touchZones if
    // present, skip unknown keys, never throw on forward-compat fields).
    out << ",\"touchZones\":{";
    first = true;
    for (auto const& [idx, tz] : profile.touchZones) {
        if (!first) {
            out << ",";
        }
        out << "\"" << static_cast<unsigned>(idx) << "\":";
        writeTouchZoneBinding(out, tz);
        first = false;
    }
    out << "}";

    // Mouse buttons (button name → key-style Binding). Wire key
    // "mouseButtons" per docs/protocols/PROFILE_SCHEMA.md; string-keyed
    // (escaped), unlike the uint16-keyed keys/encoders maps above.
    out << ",\"mouseButtons\":{";
    first = true;
    for (auto const& [name, binding] : profile.mouseButtons) {
        if (!first) {
            out << ",";
        }
        escape(out, name);
        out << ":";
        writeBinding(out, binding);
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
        // Unsigned field: a leading sign is malformed here. std::stoul would
        // silently wrap "-1" to 4294967295, so e.g. a negative delayMs would
        // become a multi-year sleep. Reject it instead of accepting garbage.
        if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) {
            fail("expected an unsigned integer but found a leading sign");
        }
        std::size_t const start = pos_;
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ == start) {
            fail("expected integer");
        }
        std::string const tok{src_.substr(start, pos_ - start)};
        // std::stoul returns unsigned long, which is 64-bit on LP64 platforms,
        // so values in (UINT32_MAX, ULONG_MAX] parse without throwing and then
        // truncate on the cast (e.g. "4294967296" -> 0). Range-check explicitly
        // so an out-of-range delayMs fails loudly instead of becoming garbage.
        unsigned long parsed = 0;
        try {
            parsed = std::stoul(tok);
        } catch (std::exception const&) {
            fail("integer out of range: " + tok);
        }
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            fail("integer out of range: " + tok);
        }
        return static_cast<std::uint32_t>(parsed);
    }

    /// Skip a complete JSON value (string, number, array, object, true, false, null).
    void skipValue() {
        DepthGuard guard{*this}; // CR-01: bound nested object/array descent.
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

    /// Maximum recursion depth for nested values. Deep enough for any
    /// legitimate Multi Action `children` nesting, shallow enough that the C++
    /// call stack can never overflow before the cap fires (CR-01). Both
    /// recursion sites -- readActionInstance's children loop and skipValue's
    /// nested object/array descent -- are bounded by this via @ref DepthGuard.
    static constexpr int kMaxDepth = 64;

    /// RAII recursion-depth tracker. Construct one at the top of every
    /// recursive parse entry point; it increments the reader's depth and
    /// fail()s past kMaxDepth (turning an unbounded stack-overflow SIGSEGV into
    /// the parser's usual loud std::runtime_error), then decrements on scope
    /// exit. Note: fail() throws, so the destructor of a guard that tripped the
    /// cap never runs -- depth_ is left as-is, which is harmless because the
    /// throw unwinds the entire parse.
    struct DepthGuard {
        JsonReader& r;
        explicit DepthGuard(JsonReader& rr) : r(rr) {
            if (++r.depth_ > kMaxDepth) {
                r.fail("maximum nesting depth exceeded");
            }
        }
        ~DepthGuard() { --r.depth_; }
        DepthGuard(DepthGuard const&) = delete;
        DepthGuard& operator=(DepthGuard const&) = delete;
        DepthGuard(DepthGuard&&) = delete;
        DepthGuard& operator=(DepthGuard&&) = delete;
    };

private:
    std::string_view src_;
    std::size_t pos_{0};
    int depth_{0};
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

/// Parse a JSON `[r,g,b]` array (0..255 each) into an Rgb.
[[nodiscard]] Rgb readRgb(JsonReader& r) {
    auto const channel = [&] { return static_cast<std::uint8_t>(r.readUInt() & 0xFFu); };
    Rgb c{};
    r.expect('[');
    c.r = channel();
    r.expect(',');
    c.g = channel();
    r.expect(',');
    c.b = channel();
    r.expect(']');
    return c;
}

/// Parse a KeyState object (per PROFILE_SCHEMA.md $defs.KeyState). Absent
/// fields stay default (optionals empty, fontSize at its struct default),
/// so an omitted `state` round-trips to a default-constructed KeyState.
[[nodiscard]] KeyState readKeyState(JsonReader& r) {
    KeyState s{};
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "imagePath") {
                s.imagePath = r.readString();
            } else if (key == "text") {
                s.text = r.readString();
            } else if (key == "background") {
                s.background = readRgb(r);
            } else if (key == "foreground") {
                s.foreground = readRgb(r);
            } else if (key == "fontSize") {
                s.fontSize = static_cast<std::uint8_t>(r.readUInt() & 0xFFu);
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
    return s;
}

/// Parse a single ActionState (Phase 31, BIND-01) — delegates to readKeyState.
[[nodiscard]] ActionState readActionState(JsonReader& r) {
    return ActionState{readKeyState(r)};
}

/// Parse an ActionInstance object (Phase 31, BIND-01/BIND-02).
///
/// Dispatches purely on key PRESENCE (never on a `_schemaVersion` field —
/// Pitfall 1 / the CR-01/WR-06 anti-pattern). The reader accepts BOTH the new
/// `states` array AND a legacy singular `state` object, folding the latter into
/// a one-element vector (lazy v1->v2 migration, BIND-02). `children` recurses.
/// After parsing, `currentState` is defensively clamped to 0 when out of range
/// so a stale/hostile index never indexes out of bounds (T-31-01).
[[nodiscard]] ActionInstance readActionInstance(JsonReader& r) {
    JsonReader::DepthGuard guard{r}; // CR-01: bound recursive children nesting.
    ActionInstance inst{};
    // Precedence (WR-02): the v2 `states` array ALWAYS wins when its key is
    // present, even if empty; the legacy singular `state` is folded into
    // states[] ONLY when no `states` key was seen. Tracking PRESENCE (not
    // emptiness) makes the fold order-independent and lossless -- a later
    // `state` can never wipe an already-parsed (even empty) states[], and a
    // later `states` always supersedes a previously folded legacy `state`.
    bool sawStatesKey = false;
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "states") {
                // v2 wins: drop any legacy `state` already folded in, then take
                // the array verbatim (idempotent if `states` somehow repeats).
                inst.states.clear();
                sawStatesKey = true;
                r.expect('[');
                if (!r.tryConsume(']')) {
                    while (true) {
                        inst.states.push_back(readActionState(r));
                        if (r.tryConsume(',')) {
                            continue;
                        }
                        r.expect(']');
                        break;
                    }
                }
            } else if (key == "state") {
                // LAZY v1->v2 FOLD: a legacy singular state becomes states[one]
                // -- but ONLY if no `states` array was seen. If states[] is
                // present (v2 wins), discard the legacy form without dropping
                // already-parsed data.
                if (!sawStatesKey) {
                    inst.states.clear();
                    inst.states.push_back(readActionState(r));
                } else {
                    (void)readActionState(r); // states[] wins; discard legacy.
                }
            } else if (key == "currentState") {
                inst.currentState = r.readUInt();
            } else if (key == "settings") {
                inst.settings = r.readString();
            } else if (key == "children") {
                r.expect('[');
                if (!r.tryConsume(']')) {
                    while (true) {
                        inst.children.push_back(readActionInstance(r)); // RECURSIVE
                        if (r.tryConsume(',')) {
                            continue;
                        }
                        r.expect(']');
                        break;
                    }
                }
            } else if (key == "id" || key == "uuid") {
                inst.id = r.readString();
            } else {
                r.skipValue(); // forward-compat: skip unknown keys.
            }
            if (r.tryConsume(',')) {
                continue;
            }
            r.expect('}');
            break;
        }
    }
    // Defensive clamp (CONTEXT lossless-load rule): never reject, never index OOB.
    if (inst.states.empty() || inst.currentState >= inst.states.size()) {
        inst.currentState = 0;
    }
    return inst;
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
            } else if (key == "state") {
                b.state = readKeyState(r);
            } else if (key == "instance") {
                b.instance = readActionInstance(r);
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
            } else if (key == "state") {
                eb.state = readKeyState(r);
            } else if (key == "instance") {
                eb.instance = readActionInstance(r);
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

/// Parse a TouchZoneBinding object (schema v2, Phase 26 D-11).
/// Absent fields default-construct, so a minimal {"onTap":[]} round-trips cleanly.
TouchZoneBinding readTouchZoneBinding(JsonReader& r) {
    TouchZoneBinding tz{};
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "onTap") {
                tz.onTap = readActionArray(r);
            } else if (key == "state") {
                tz.state = readKeyState(r);
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
    return tz;
}

/// Read a `{"<uint>":<value>, ...}` map; @p readValue consumes one value.
template <typename Value, typename ReadValueFn>
void readUintKeyedMap(JsonReader& r,
                      std::unordered_map<std::uint16_t, Value>& dst,
                      ReadValueFn&& readValue) {
    r.expect('{');
    if (r.tryConsume('}')) {
        return;
    }
    while (true) {
        std::string const idxStr = r.readString();
        std::uint16_t idx = 0;
        try {
            unsigned long const parsed = std::stoul(idxStr);
            if (parsed > std::numeric_limits<std::uint16_t>::max()) {
                throw std::out_of_range(idxStr); // key index would truncate; reject
            }
            idx = static_cast<std::uint16_t>(parsed);
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
    // Parse _schemaVersion for future dispatch (CR-01 fix: no longer used to guard
    // touchZones — see the touchZones branch below). Marked maybe_unused so the
    // compiler does not warn when the value is consumed only by readUInt() and not
    // subsequently read (the field must still be consumed from the stream).
    // Future schema versions > 2 that require different read semantics can re-arm
    // this variable.
    [[maybe_unused]] int schemaVersion = 1;
    while (true) {
        std::string const key = r.readString();
        r.expect(':');
        if (key == "_schemaVersion") {
            schemaVersion = static_cast<int>(r.readUInt());
        } else if (key == "id") {
            profile.id = r.readString();
        } else if (key == "name") {
            profile.name = r.readString();
        } else if (key == "device") {
            // Schema wire-key for Profile::deviceCodename (per PROFILE_SCHEMA.md).
            profile.deviceCodename = r.readString();
        } else if (key == "keys") {
            readUintKeyedMap(r, profile.keys, [](JsonReader& rr) { return readBinding(rr); });
        } else if (key == "encoders") {
            readUintKeyedMap(
                r, profile.encoders, [](JsonReader& rr) { return readEncoderBinding(rr); });
        } else if (key == "touchZones") {
            // Parse unconditionally when the key is present: a file that carries a
            // "touchZones" object is implicitly v2 regardless of where "_schemaVersion"
            // appears in the JSON object. RFC 8259 does not guarantee key ordering, so
            // guarding on schemaVersion here would silently discard all touch-zone data
            // whenever an external tool or hand-edit emits "touchZones" before
            // "_schemaVersion" (CR-01 / WR-06 ordering-dependency fix).
            //
            // Safety on genuine v1 profiles: v1 files contain no "touchZones" key at
            // all, so this branch is never reached for them — the default empty map is
            // correctly preserved (D-12 backward compat holds).
            {
                r.expect('{');
                if (!r.tryConsume('}')) {
                    while (true) {
                        std::string const idxStr = r.readString();
                        std::uint8_t idx = 0;
                        try {
                            unsigned long const parsed = std::stoul(idxStr);
                            // The old `& 0xFFu` mask silently folded e.g. "256" -> 0;
                            // reject anything that would not fit a uint8 instead.
                            if (parsed > std::numeric_limits<std::uint8_t>::max()) {
                                throw std::out_of_range(idxStr);
                            }
                            idx = static_cast<std::uint8_t>(parsed);
                        } catch (std::exception const&) {
                            std::ostringstream err;
                            err << "profileFromJson: invalid uint8 touchZone key \"" << idxStr
                                << "\"";
                            throw std::runtime_error(err.str());
                        }
                        r.expect(':');
                        profile.touchZones.emplace(idx, readTouchZoneBinding(r));
                        if (r.tryConsume(',')) {
                            continue;
                        }
                        r.expect('}');
                        break;
                    }
                }
            }
        } else if (key == "mouseButtons") {
            // String-keyed (button name) map of key-style Bindings. Wire key
            // "mouseButtons" per PROFILE_SCHEMA.md. Mirrors the pages reader's
            // inline string-key loop since readUintKeyedMap is uint16-only.
            r.expect('{');
            if (!r.tryConsume('}')) {
                while (true) {
                    std::string const btn = r.readString();
                    r.expect(':');
                    profile.mouseButtons.emplace(btn, readBinding(r));
                    if (r.tryConsume(',')) {
                        continue;
                    }
                    r.expect('}');
                    break;
                }
            }
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
