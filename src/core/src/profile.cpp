// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile.cpp
 * @brief Minimal hand-rolled JSON writer for the core profile schema.
 *
 * The goal is to avoid a heavyweight JSON dependency in the core library;
 * the app layer can transparently override profileToJson() / profileFromJson()
 * at link time with an implementation backed by Qt's QJsonDocument.
 *
 * Only ASCII-safe strings are supported. Unicode in user labels should be
 * validated upstream before being stored in a Profile.
 */
#include "ajazz/core/profile.hpp"

#include <sstream>
#include <stdexcept>

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

Profile profileFromJson(std::string_view /*json*/) {
    // The core ships a writer and a stub reader. The full reader is
    // implemented in `src/app/profile_io.cpp` on top of QJsonDocument so it
    // can interoperate naturally with the Qt UI model.
    throw std::logic_error("profileFromJson: use ajazz::app::loadProfile instead");
}

} // namespace ajazz::core
