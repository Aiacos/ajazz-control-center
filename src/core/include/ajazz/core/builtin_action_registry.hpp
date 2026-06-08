// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file builtin_action_registry.hpp
 * @brief Pure-core dispatch table for the @c com.hotspot.streamdock.* built-in UUIDs.
 *
 * Maps each registered built-in UUID to a handler function. The registry is
 * intentionally pure C++ with NO Qt and NO nlohmann::json (COD-031 boundary):
 * the handler is invoked with the verbatim @c settingsJson string; JSON parsing
 * is the responsibility of the app-tier handler.
 *
 * ## Usage
 *
 * The app layer (BuiltinActionsService) populates the registry at construction
 * time via @ref registerAction, then the Application plugin-executor short-circuits
 * any built-in UUID to @ref dispatch BEFORE forwarding to the Phase-19 path:
 *
 * @code
 * if (registry.handles(id)) {
 *     registry.dispatch(id, settingsJson);
 *     return;
 * }
 * // ... Phase-19 fallback
 * @endcode
 *
 * ## COD-031
 *
 * This header is installed alongside the other @c ajazz_core public headers. It
 * MUST NOT include any Qt header or nlohmann::json header. Only standard C++20
 * headers are permitted here.
 */
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ajazz::core {

/**
 * @brief Handler type for a single built-in action UUID.
 *
 * Called by @ref BuiltinActionRegistry::dispatch with the verbatim
 * @c settingsJson from the profile binding. JSON parsing happens inside the
 * handler (app tier), not in the registry (COD-031).
 */
using BuiltinHandler = std::function<void(std::string_view settingsJson)>;

/**
 * @brief UUID-to-handler dispatch table for built-in in-process actions.
 *
 * All registered UUIDs MUST start with the prefix
 * @ref kBuiltinPrefix ("com.hotspot.streamdock."). @ref handles returns
 * @c false for any other prefix so the plugin-executor can forward
 * third-party UUIDs to the Phase-19 path unchanged.
 *
 * @note Not thread-safe. Must be used on the Qt main (GUI) thread.
 */
class BuiltinActionRegistry {
public:
    /// The common prefix for all built-in Stream Dock action UUIDs.
    /// Exposed so the app plugin-executor short-circuit can reuse it
    /// without introducing a hard-coded string literal.
    static constexpr std::string_view kBuiltinPrefix = "com.hotspot.streamdock.";

    /// The canonical built-in UUID for a Multi Action (an action whose
    /// `ActionInstance.children` run sequentially on a single press, BIND-04/05).
    /// MUST start with @ref kBuiltinPrefix so @ref handles can return @c true.
    /// This is the REAL dispatch prefix (the OpenDeck "opendeck.multiaction" id
    /// would fail the prefix check and never fire — RESEARCH A3/Q2). Exposed so
    /// the input-service dispatch seam can id-match a firing binding's instance
    /// without a hard-coded string literal.
    static constexpr std::string_view kMultiActionId = "com.hotspot.streamdock.multiaction";

    /**
     * @brief Register a handler for a built-in UUID.
     *
     * @param uuid    Full UUID string, e.g.
     *                @c "com.hotspot.streamdock.device.brightness".
     *                Does NOT need to start with @ref kBuiltinPrefix — the
     *                prefix check in @ref handles is independent of what is
     *                stored — but callers SHOULD only register built-in UUIDs.
     * @param handler Called with the verbatim settingsJson from the profile
     *                binding when @ref dispatch is invoked for @p uuid.
     */
    void registerAction(std::string uuid, BuiltinHandler handler);

    /**
     * @brief Return @c true when @p id is a known, registered built-in UUID.
     *
     * A UUID is a built-in if and only if:
     *   1. it starts with @ref kBuiltinPrefix ("com.hotspot.streamdock."), AND
     *   2. it has been registered via @ref registerAction.
     *
     * @param id  Action UUID from the profile binding.
     * @return @c true  — built-in UUID, registered, call @ref dispatch.
     *         @c false — third-party UUID or unregistered built-in prefix;
     *                    forward to the Phase-19 path.
     */
    [[nodiscard]] bool handles(std::string_view id) const;

    /**
     * @brief Invoke the handler registered for @p id with @p settingsJson.
     *
     * If no handler is registered for @p id the call is a clean no-op
     * (no exception, no abort). The registry does NOT parse @p settingsJson
     * (COD-031): JSON parsing is delegated to the handler.
     *
     * @param id           Action UUID (should pass @ref handles first).
     * @param settingsJson Verbatim JSON blob from the profile binding.
     */
    void dispatch(std::string_view id, std::string_view settingsJson) const;

private:
    std::unordered_map<std::string, BuiltinHandler> m_handlers;
};

} // namespace ajazz::core
