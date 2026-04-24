// SPDX-License-Identifier: GPL-3.0-or-later
//
// Profile schema. Profiles describe the mapping of physical controls
// (keys, encoders, mouse buttons) to logical actions (macros, shell
// commands, plugin actions, scene switches, etc.). Profiles are
// JSON-serializable and can be bound per application.
//
#pragma once

#include "ajazz/core/capabilities.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ajazz::core {

/// Action emitted when a control is activated.
struct Action {
    /// Fully-qualified action id: "<plugin-id>.<action-id>"
    std::string id;

    /// Free-form JSON-compatible settings. Backends don't interpret this;
    /// the plugin host passes it to the owning plugin.
    std::string settingsJson;

    /// User-visible label displayed in the UI.
    std::string label;
};

/// Visual state for a single key (image, text, color).
struct KeyState {
    std::optional<std::string> imagePath; ///< absolute or resource path
    std::optional<std::string> text;      ///< overlay text
    std::optional<Rgb> background;
    std::optional<Rgb> foreground;
    std::uint8_t fontSize{14};
};

/// Binding of a physical control to an action chain.
struct Binding {
    std::vector<Action> onPress;
    std::vector<Action> onRelease;
    std::vector<Action> onLongPress;
    KeyState state;
};

/// Composite profile mapping every physical control.
struct Profile {
    std::string id; ///< UUIDv4
    std::string name;
    std::string deviceCodename;
    std::unordered_map<std::uint16_t, Binding> keys;
    std::unordered_map<std::uint16_t, Binding> encoders;
    std::unordered_map<std::string, Binding> mouseButtons;

    /// Optional per-application triggers: when the foreground app matches
    /// any of these hints, this profile auto-activates.
    std::vector<std::string> applicationHints;
};

/// Serialize / deserialize profiles to JSON. Defined in profile.cpp.
[[nodiscard]] std::string profileToJson(Profile const& profile);
[[nodiscard]] Profile profileFromJson(std::string_view json);

} // namespace ajazz::core
