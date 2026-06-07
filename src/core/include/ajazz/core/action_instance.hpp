// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file action_instance.hpp
 * @brief OpenDeck/StreamDeck-shaped action-instance data model (Phase 31, BIND-01).
 *
 * An @ref ActionInstance is the per-binding payload that drives Toggle Action
 * (multiple @ref ActionState visuals + a `currentState` index), Multi Action
 * (recursive child instances), and per-instance configuration (`settings`).
 * It is added additively onto @ref ajazz::core::Binding and
 * @ref ajazz::core::EncoderBinding as a `std::optional<ActionInstance> instance`
 * field; the existing onPress/onRelease/onLongPress chains and the legacy
 * singular `KeyState state` are untouched.
 *
 * This header is an installed PUBLIC core header. Per the COD-031 boundary it
 * is intentionally nlohmann-free and Qt-free: only the C++ standard library
 * and @ref ajazz::core::Rgb (from capabilities.hpp) are used. All JSON
 * (de)serialisation lives hand-rolled in profile.cpp's anonymous namespace,
 * not here.
 *
 * @note @ref KeyState lives here (rather than in profile.hpp) so that this
 *       header depends only on capabilities.hpp. profile.hpp includes this
 *       header one-directionally and reuses KeyState; that ordering avoids the
 *       circular include that a two-way profile.hpp <-> action_instance.hpp
 *       dependency would create.
 *
 * @see ajazz::core::Binding, ajazz::core::EncoderBinding, profileToJson
 */
#pragma once

#include "ajazz/core/capabilities.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ajazz::core {

/**
 * @brief Visual appearance of a key in the profile editor and on the device.
 *
 * All fields are optional; unset fields fall back to the device default
 * (e.g., blank black key with no overlay). Reused both as the legacy
 * @ref Binding::state and as each @ref ActionState::visual.
 */
struct KeyState {
    std::optional<std::string> imagePath; ///< Absolute path or Qt resource URL for the key icon.
    std::optional<std::string> text;      ///< Overlay text rendered on top of the image.
    std::optional<Rgb> background;        ///< Solid background fill color.
    std::optional<Rgb> foreground;        ///< Text and icon tint color.
    std::uint8_t fontSize{14};            ///< Overlay text size in points.
};

/**
 * @brief A single visual state of an @ref ActionInstance.
 *
 * Carries full @ref KeyState parity (imagePath / text / background /
 * foreground / fontSize) via the reused `visual` member. The thin wrapper
 * leaves a forward-compatible seam for a future per-state non-visual field at
 * zero wire cost: on the wire a state element is exactly a `$defs.KeyState`
 * object.
 */
struct ActionState {
    KeyState visual; ///< Per-state visual payload; serialised as a $defs.KeyState object.
};

/**
 * @brief An OpenDeck/StreamDeck-shaped action instance bound to a control.
 *
 * Instances support Toggle Action (multiple @ref states + a `currentState`
 * active index), Multi Action (recursive @ref children run sequentially), and
 * opaque per-instance configuration (`settings`). Dispatch of these semantics
 * lands in Phase 32; this struct is the model + wire shape only.
 */
struct ActionInstance {
    /// Dotted plugin action id (e.g. "com.elgato.counter.increment").
    /// Wire key "id" (the reader also accepts "uuid"). Omitted from the JSON
    /// when empty (cheap and forward-compatible).
    std::string id{};

    /// Per-state visuals. Wire key "states" (array). The writer always emits
    /// the array form; the reader also folds a legacy singular "state" into a
    /// one-element vector (lazy v1->v2 migration).
    std::vector<ActionState> states{};

    /// 0-based active state index. Wire key "currentState". Defensively clamped
    /// to 0 on read when out of range.
    std::uint32_t currentState{0};

    /// Opaque per-instance configuration. Wire key "settings". An ESCAPED JSON
    /// STRING (mirroring Action::settingsJson), NOT a nested object, so the
    /// wire stays linear and unknown sub-keys round-trip untouched.
    std::string settings{};

    /// Child instances for Multi Action nesting. Wire key "children" (array).
    /// Recursive: std::vector of an incomplete self-type is legal (C++17+),
    /// so no unique_ptr indirection is needed.
    std::vector<ActionInstance> children{};
};

} // namespace ajazz::core
