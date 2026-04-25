// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file macro_recorder.hpp
 * @brief Capture an OS-level keystroke / mouse-event stream into an
 *        @ref ajazz::core::ActionChain that can be replayed by the action
 *        engine.
 *
 * Closes #30 (macro recorder).
 *
 * The recorder is intentionally split into a portable interface
 * (@ref MacroRecorder) and per-platform back-ends:
 *
 *   - Linux : evdev / libinput grab (root or `input` group required).
 *   - macOS : CGEventTap via Accessibility permission (TODO).
 *   - Windows : SetWindowsHookExW low-level hooks (TODO).
 *
 * The shipped Linux back-end currently provides a "stub" no-op
 * implementation that satisfies the interface and exists so the UI can wire
 * the workflow end-to-end. The full evdev capture path is gated behind the
 * `AJAZZ_FEATURE_MACRO_RECORDER` CMake option (off by default).
 */
#pragma once

#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/profile.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ajazz::core {

/// One captured event pre-translation, as seen by the OS hook layer.
struct MacroEvent {
    enum class Kind : std::uint8_t {
        KeyDown,
        KeyUp,
        MouseDown,
        MouseUp,
        MouseMove,
    };

    Kind kind{Kind::KeyDown};
    std::uint32_t code{0};                  ///< Platform-specific scan-code or button id.
    std::chrono::milliseconds timestamp{0}; ///< Ms since recording started.
    std::string label;                      ///< Optional human-readable label.
};

/**
 * @brief Capture an OS-level event stream into an @ref ActionChain.
 *
 * The recorder is single-shot: call @ref start once, drive your event loop,
 * then call @ref stop to retrieve the resulting chain.
 *
 * Implementations must be thread-safe: @ref stop may be called from any
 * thread, including a Qt UI thread.
 */
class MacroRecorder {
public:
    using EventCallback = std::function<void(MacroEvent const&)>;

    virtual ~MacroRecorder() = default;

    /**
     * @brief Begin capturing events.
     *
     * @param onEvent Optional callback fired for every captured event;
     *                useful for live UI previews. May be empty.
     * @return true if recording started successfully.
     */
    virtual bool start(EventCallback onEvent = {}) = 0;

    /**
     * @brief Stop capture and convert the accumulated events to an
     *        @ref ActionChain compatible with @ref ActionEngine.
     *
     * Each captured KeyDown is emitted as a @ref ActionKind::KeyPress
     * step; mouse events are not yet translated and are skipped (they
     * remain accessible via the raw stream — see @ref rawEvents).
     *
     * @return Replayable chain. Empty if no events were captured.
     */
    [[nodiscard]] virtual ActionChain stop() = 0;

    /// Read-only access to the raw event stream captured between
    /// start()/stop(). Cleared by the next start() call.
    [[nodiscard]] virtual std::vector<MacroEvent> const& rawEvents() const noexcept = 0;
};

/**
 * @brief Build the platform-default @ref MacroRecorder.
 *
 * Returns a stub implementation when no native back-end is available
 * (e.g. on a system without evdev). The stub still records the
 * start/stop calls so wiring tests can exercise the path.
 */
[[nodiscard]] std::unique_ptr<MacroRecorder> makeDefaultMacroRecorder();

} // namespace ajazz::core
