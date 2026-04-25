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
 * @brief Built-in step kinds understood directly by the action engine,
 *        independently of any plugin.
 *
 * Plugin-provided actions still use Kind::Plugin and are dispatched via the
 * plugin host. The remaining kinds are interpreted natively by
 * @ref ActionEngine and do not require a plugin to be loaded.
 */
enum class ActionKind : std::uint8_t {
    Plugin = 0,   ///< Forward to plugin host (id = "<plugin>.<action>").
    Sleep,        ///< Pause the chain for `delayMs` milliseconds.
    KeyPress,     ///< Synthesise an OS-level key press (`settingsJson` carries the keycode).
    RunCommand,   ///< Run a shell command (`settingsJson` carries argv0 + args).
    OpenUrl,      ///< Open a URL in the default browser.
    OpenFolder,   ///< Switch the device to a child ProfilePage (folders — see Profile::pages).
    BackToParent, ///< Pop one ProfilePage off the navigation stack.
};

/**
 * @brief A single step in an @ref ActionChain.
 *
 * `kind` selects which interpreter handles the step:
 *   - `Kind::Plugin` forwards the step to the plugin host. The dotted
 *     `id` is required; `settingsJson` is forwarded verbatim.
 *   - `Kind::Sleep` honours `delayMs` and ignores everything else.
 *   - `Kind::KeyPress`/`RunCommand`/`OpenUrl` use `settingsJson`.
 *   - `Kind::OpenFolder` reads the target page id from `settingsJson`
 *     (`{"target": "<page-id>"}`).
 *   - `Kind::BackToParent` ignores all fields.
 *
 * Plugin actions remain backward-compatible because `kind` defaults to
 * `Kind::Plugin` and unknown JSON keys are tolerated by the reader.
 */
struct Action {
    ActionKind kind{ActionKind::Plugin}; ///< Step kind; selects the interpreter.
    std::string id{};                    ///< Plugin id when kind == Plugin.
    std::string settingsJson{};          ///< Free-form JSON blob.
    std::string label{};                 ///< User-visible label shown in the UI.
    std::uint32_t delayMs{0};            ///< Inter-step delay (Sleep + post-step pause).
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
 *
 * The chain types intentionally use plain `std::vector<Action>` to keep the
 * profile schema flat and JSON-friendly. The action engine treats every
 * vector as an @ref ActionChain and walks it sequentially, honouring the
 * per-step `delayMs`.
 */
struct Binding {
    std::vector<Action> onPress;     ///< Actions fired when the control is pressed.
    std::vector<Action> onRelease;   ///< Actions fired when the control is released.
    std::vector<Action> onLongPress; ///< Actions fired after a long-press threshold.
    KeyState state;                  ///< Visual appearance for this control slot.
};

/**
 * @brief Encoder-specific binding (CW / CCW / Press) used by AKP03 and AKP05.
 *
 * Stored separately from @ref Binding so the UI can render three distinct
 * action-chain editors (rotate clockwise, rotate counter-clockwise, push).
 */
struct EncoderBinding {
    std::vector<Action> onCw;    ///< Chain fired on a clockwise rotation tick.
    std::vector<Action> onCcw;   ///< Chain fired on a counter-clockwise tick.
    std::vector<Action> onPress; ///< Chain fired on a knob press.
    KeyState state;              ///< Optional LCD label for AKP05's encoder strip.
};

/**
 * @brief A page of bindings, used to model nested folders on AKP devices.
 *
 * `id` is a stable string id (UUIDv4 recommended) referenced by
 * `Action::OpenFolder` steps. The root page of a profile lives in
 * `Profile::pages` under the key "root".
 */
struct ProfilePage {
    std::string id;                                  ///< Stable page id.
    std::string name;                                ///< User-visible name.
    std::unordered_map<std::uint16_t, Binding> keys; ///< Per-key bindings on this page.
    std::vector<std::string> children;               ///< Child page ids (sub-folders).
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
    std::string id;                                  ///< Stable UUIDv4 identifier.
    std::string name;                                ///< User-visible profile name.
    std::string deviceCodename;                      ///< Target device, e.g. "akp153".
    std::unordered_map<std::uint16_t, Binding> keys; ///< Key index → binding (root page).
    std::unordered_map<std::uint16_t, EncoderBinding> encoders; ///< Encoder index → binding.
    std::unordered_map<std::string, Binding> mouseButtons;      ///< Button name → binding.

    /**
     * @brief Optional folder pages — keyed by ProfilePage::id.
     *
     * When non-empty, `keys` is treated as the root page and child pages are
     * reachable through `Action::OpenFolder` steps. Empty by default for
     * backwards compatibility with single-page profiles.
     */
    std::unordered_map<std::string, ProfilePage> pages;

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
