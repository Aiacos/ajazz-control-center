// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file builtin_actions_service.hpp
 * @brief App-tier service that populates the BuiltinActionRegistry and provides
 *        the plugin-executor short-circuit entry for the ~24 built-in UUIDs.
 *
 * BuiltinActionsService owns:
 *   - a @ref core::BuiltinActionRegistry populated with a handler per LOCKED
 *     UUID (@c com.hotspot.streamdock.*).
 *   - a @ref core::IInputSynthesizer for @c system.hotkey OUTPUT, @c plain.text,
 *     @c system.multimedia, @c system.volume (from 21-01).
 *   - (gated @c AJAZZ_HAVE_WEBSOCKETS) an @ref ObsClient for @c obsstudio (from 21-02).
 *
 * The service exposes @ref onPluginAction — the replacement for the Phase-15
 * @c plugin executor stub. Application wires the @c ActionEngine::plugin executor
 * to this method; it short-circuits registered built-in UUIDs to the registry and
 * forwards the rest to the Phase-19 fallback injected at construction time.
 *
 * ## COD-031
 * JSON parsing happens in the handler bodies (app tier, @c QJsonDocument). The
 * registry itself never sees a parsed JSON value. The @c nlohmann library is NOT
 * used here.
 *
 * ## Anti-features (LOCKED)
 *   - @c system.hotkey global-hook CAPTURE is OFF by default (T-21-hook).
 *     @ref captureHotkeysEnabled is false unless the user explicitly enables it
 *     via QSettings. OUTPUT synthesis (sendChord) is always the default path.
 *   - @c obsstudio auth is default-on (T-21-obsauth): inherited from ObsClient.
 *
 * ## LunBo per-key cursor (T-21-lunbo)
 * @c multiactions.LunBo maintains a per-key cursor keyed by a stable binding id
 * (page id + key index encoded in the settingsJson). Cursors do not share state
 * across keys and reset on profile change.
 *
 * ## Dependency injection seams (for unit tests)
 * All OS-touching operations are injected so unit tests can spy / replace them:
 *   - @c BrightnessSink (std::function) for @c device.brightness.
 *   - @c NavigateSink (std::function) for @c page.previous / @c page.next.
 *   - @c OpenUrlFn (std::function) for @c browser.
 *   - @c core::ActionEngine* for @c multiactions, @c profile.openchild / @c backtoparent.
 *   - @c core::IInputSynthesizer* injection overload (via @c setSynthesizer) for synthesis.
 *
 * @see BuiltinActionRegistry, IInputSynthesizer, ObsClient
 */
#pragma once

#include "ajazz/core/builtin_action_registry.hpp"
#include "ajazz/core/input_synthesizer.hpp"

#ifdef AJAZZ_HAVE_WEBSOCKETS
#include "obs_client.hpp"
#endif

#include <QObject>
#include <QSettings>
#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ajazz::core {
class ActionEngine;
} // namespace ajazz::core

namespace ajazz::app {

/**
 * @class BuiltinActionsService
 * @brief Headless QObject service that populates and drives the BuiltinActionRegistry.
 *
 * Constructed by Application (or by test fixtures with injected spies). Not
 * QML-exposed — Application wires the plugin executor to @ref onPluginAction.
 *
 * Construction order note: the service MUST be constructed AFTER the
 * StreamDockControlService, ActionEngine, and (if present) PluginDeviceBridge.
 * Application declares @c m_builtinActions AFTER those members to respect
 * GCC @c -Wreorder.
 */
class BuiltinActionsService : public QObject {
    Q_OBJECT

public:
    /// Called for @c device.brightness: level is parsed (0..100 clamped) from settingsJson.
    using BrightnessSink = std::function<void(int level)>;

    /// Called for @c page.previous (-1), @c page.next (+1); for @c page.goto a non-(-1/+1)
    /// absolute index may be used depending on the semantic chosen.
    using NavigateSink = std::function<void(int direction)>;

    /// Called for @c browser: receives the validated http(s) URL string.
    using OpenUrlFn = std::function<void(std::string_view url)>;

    /// Phase-19 fallback: invoked for third-party UUIDs the registry does not handle.
    using PluginFallback = std::function<void(std::string_view id, std::string_view settingsJson)>;

    /**
     * @brief Construct the service with all injection seams.
     *
     * @param brightness   Sink for device.brightness actions.
     * @param navigate     Sink for page.previous/next/goto (direction or target index).
     * @param openUrl      Executor for browser actions.
     * @param engine       ActionEngine for multiactions run + pushPage/popPage.
     * @param fallback     Phase-19 plugin path for non-builtin UUIDs.
     * @param parent       QObject parent.
     */
    explicit BuiltinActionsService(BrightnessSink brightness,
                                   NavigateSink navigate,
                                   OpenUrlFn openUrl,
                                   core::ActionEngine* engine,
                                   PluginFallback fallback,
                                   QObject* parent = nullptr);

    ~BuiltinActionsService() override;

    /**
     * @brief Override the default IInputSynthesizer (for unit tests).
     *
     * By default the service creates @c makeDefaultInputSynthesizer() in the ctor.
     * Calling this before any dispatch replaces the synthesizer with the provided one
     * (e.g. a FakeSynth spy).
     *
     * @param synth  Unique ownership transferred to the service.
     */
    void setSynthesizer(std::unique_ptr<core::IInputSynthesizer> synth);

    /**
     * @brief Plugin-executor entry: short-circuits built-in UUIDs, forwards the rest.
     *
     * The Application @c ActionEngine::plugin executor calls this method instead of
     * the Phase-15 logged stub:
     *
     * @code
     * execs.plugin = [this](std::string_view id, std::string_view json) {
     *     m_builtinActions->onPluginAction(id, json);
     * };
     * @endcode
     *
     * If @c m_registry.handles(id) is true, @c m_registry.dispatch(id, settingsJson)
     * is called and the method returns. Otherwise @c m_fallback(id, settingsJson) is
     * called (Phase-15/19 path).
     *
     * @param id           Action UUID from the profile binding.
     * @param settingsJson Verbatim settings blob from the binding.
     */
    void onPluginAction(std::string_view id, std::string_view settingsJson);

    /**
     * @brief Reset LunBo per-key cursors (call on profile change).
     *
     * Each profile change invalidates the round-robin position for every LunBo key.
     * Call this from Application when profileChanged fires.
     */
    void resetLunBoCursors();

    /**
     * @brief Enable or disable the opt-in global-hook CAPTURE (T-21-hook).
     *
     * When @p enable is false (the default), no global hook is installed and
     * captureHotkeys is never called on the synthesizer. When @p enable is true,
     * the synthesizer's captureHotkeys(true, ...) is activated.
     *
     * This is separate from OUTPUT synthesis — sendChord/typeText/sendMediaKey are
     * always available regardless of this toggle.
     *
     * @param enable  true to activate global hotkey capture; false to deactivate.
     */
    void setCaptureHotkeys(bool enable);

    /**
     * @brief Return current capture-hotkeys state.
     */
    [[nodiscard]] bool captureHotkeysEnabled() const noexcept { return m_captureEnabled; }

private:
    /// Populate the registry with all LOCKED core-set UUIDs.
    void populate();

    // ---- Dependency-injected sinks ----
    BrightnessSink m_brightness;
    NavigateSink m_navigate;
    OpenUrlFn m_openUrl;
    core::ActionEngine* m_engine{nullptr};
    PluginFallback m_fallback;

    // ---- Owned collaborators ----
    core::BuiltinActionRegistry m_registry;
    std::unique_ptr<core::IInputSynthesizer> m_synth;

#ifdef AJAZZ_HAVE_WEBSOCKETS
    std::unique_ptr<ObsClient> m_obs;
    bool m_obsConnected{false};
#endif

    // ---- Opt-in capture state (T-21-hook, LOCKED OFF by default) ----
    bool m_captureEnabled{false};

    // ---- LunBo per-key cursor (T-21-lunbo) ----
    /// Key: stable binding id string (encoded from settingsJson "bindingId" or
    /// fallback "page:key" pair). Value: current carousel position index.
    std::unordered_map<std::string, std::size_t> m_lunboCursors;
};

} // namespace ajazz::app
