// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal hand-rolled JSON writer/reader for profiles. The goal here is to
// avoid a heavyweight JSON dependency in the core library; the app layer
// can swap this for Qt's QJsonDocument transparently by overriding these
// two free functions at link time.
//
#include "ajazz/core/profile.hpp"

#include <sstream>
#include <stdexcept>

namespace ajazz::core {

namespace {

[[maybe_unused]] void writeRgb(std::ostringstream& out, Rgb const& c) {
    out << "[" << static_cast<int>(c.r) << "," << static_cast<int>(c.g) << ","
        << static_cast<int>(c.b) << "]";
}

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

void writeAction(std::ostringstream& out, Action const& a) {
    out << "{";
    out << "\"id\":";
    escape(out, a.id);
    out << ",";
    out << "\"settings\":";
    escape(out, a.settingsJson);
    out << ",";
    out << "\"label\":";
    escape(out, a.label);
    out << "}";
}

void writeBinding(std::ostringstream& out, Binding const& b) {
    auto writeChain = [&](std::string_view key, std::vector<Action> const& actions) {
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
    };

    out << "{";
    writeChain("onPress", b.onPress);
    out << ",";
    writeChain("onRelease", b.onRelease);
    out << ",";
    writeChain("onLongPress", b.onLongPress);
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
