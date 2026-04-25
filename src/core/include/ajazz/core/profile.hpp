// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile.hpp
 * @brief Profile schema for mapping physical controls to logical actions.
 *
 * A Profile describes how each key, encoder, or mouse button on a specific
 * device is bound to a chain of plugin-defined actions. Profiles are
 * JSON-serialisable and can auto-activate when a particular application
 * comes into focus.
 *
 * The core library ships a JSON writer (profileToJson()) and a stub reader;
 * the full reader is implemented in the app layer using QJsonDocument so it
 * can interoperate with the Qt UI model.
 *
 * @see profileToJson, profileFromJson, Binding, Profile
 */
#pragma once

#include "ajazz/core/capabilities.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ajazz::core {

/**
 * @brief A single plugin action that can be bound to a control.
 *
 * Actions are identified by a fully-qualified dotted string
 * ("<plugin-id>.<action-id>"). The settings blob is opaque to the core;
 * the plugin host forwards it verbatim to the owning plugin when the
 * action fires.
 */
struct Action {
    std::string id;           ///< Fully-qualified action id: "<plugin-id>.<action-id>".
    std::string settingsJson; ///< Free-form JSON blob forwarded to the plugin.
    std::string label;        ///< User-visible label shown in the UI.
};

/**
 * @brief Visual appearance of a key in the profile editor and on the device.
 *
 * All fields are optional; unset fields fall back to the device default
 * (e.g., blank black key with no overlay).
 */
struct KeyState {
    std::optional<std::string> imagePath; ///< Absolute path or Qt resource URL for the key icon.
    std::optional<std::string> text;      ///< Overlay text rendered on top of the image.
    std::optional<Rgb> background;        ///< Solid background fill color.
    std::optional<Rgb> foreground;        ///< Text and icon tint color.
    std::uint8_t fontSize{14};            ///< Overlay text size in points.
};

/**
 * @brief Full mapping of a single physical control (key, encoder, button).
 *
 * Each trigger event can fire an ordered chain of actions. The `state`
 * field drives the key's visual rendering in the UI and on the device LCD.
 */
struct Binding {
    std::vector<Action> onPress;     ///< Actions fired when the control is pressed.
    std::vector<Action> onRelease;   ///< Actions fired when the control is released.
    std::vector<Action> onLongPress; ///< Actions fired after a long-press threshold.
    KeyState state;                  ///< Visual appearance for this control slot.
};

/**
 * @brief Complete binding table for a single device.
 *
 * A Profile owns all bindings for every key, encoder, and mouse button
 * on one particular device model (identified by codename). Multiple
 * profiles can exist for the same device; the profile engine selects
 * the active one based on applicationHints or explicit user selection.
 */
struct Profile {
    std::string id;                                        ///< Stable UUIDv4 identifier.
    std::string name;                                      ///< User-visible profile name.
    std::string deviceCodename;                            ///< Target device, e.g. "akp153".
    std::unordered_map<std::uint16_t, Binding> keys;       ///< Key index → binding.
    std::unordered_map<std::uint16_t, Binding> encoders;   ///< Encoder index → binding.
    std::unordered_map<std::string, Binding> mouseButtons; ///< Button name → binding.

    /**
     * Application process-name hints for auto-activation. When the active
     * foreground application matches any hint string this profile is
     * automatically loaded by the profile engine.
     */
    std::vector<std::string> applicationHints;
};

/**
 * @brief Serialise a Profile to a compact JSON string.
 *
 * Uses a hand-rolled writer (no external JSON dependency in the core).
 * The app layer may override this at link time with a QJsonDocument
 * implementation if richer formatting is desired.
 *
 * @param profile Profile to serialise.
 * @return UTF-8 JSON string.
 */
[[nodiscard]] std::string profileToJson(Profile const& profile);

/**
 * @brief Deserialise a Profile from a JSON string.
 *
 * @note The core implementation always throws std::logic_error; the
 *       real reader lives in `src/app/profile_io.cpp` and is built on
 *       QJsonDocument. Do not call this directly from core code.
 *
 * @param json UTF-8 JSON string produced by profileToJson().
 * @return Deserialised Profile.
 * @throws std::logic_error always, unless overridden at link time.
 */
[[nodiscard]] Profile profileFromJson(std::string_view json);

} // namespace ajazz::core
