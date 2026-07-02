// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher.hpp
 * @brief Pure-core foreground-window detection interface: observe which
 *        application currently has OS focus and emit debounced change
 *        notifications — the INPUT side of the Phase 34 per-app profile
 *        auto-switch (APROF-01..04) and the host-level
 *        @c applicationDidLaunch / @c applicationDidTerminate events.
 *
 * The interface deliberately contains NO Qt and NO nlohmann::json (COD-031
 * boundary — public headers install alongside ajazz_core and must not
 * drag in Qt or JSON implementation headers for consumers). It deals in
 * plain app-id strings only; all Qt machinery (the Wayland client extension,
 * the timer-based debounce, Xlib/Win32/AppKit calls) lives in the app-tier
 * per-OS backend TUs, never in this header. The change notification is delivered
 * via an injected @c std::function callback — the "seams via std::function
 * injection" composition decision (STATE.md), mirroring
 * @ref input_synthesizer.hpp exactly.
 *
 * ## Backend selection
 *
 * @ref makeDefaultActiveWindowWatcher returns a @c StubActiveWindowWatcher
 * when no native backend is compiled / available (the default in tests and
 * on platforms without a supported foreground API). When a platform backend
 * is present, the factory delegates to @c makePlatformActiveWindowWatcher().
 * On Linux the platform backend additionally selects wayland-vs-x11 at
 * RUNTIME (by session type), not at compile time — see the per-OS TUs
 * (landed in Plan 03).
 *
 * ## Graceful degradation (APROF-03)
 *
 * @ref IActiveWindowWatcher::capabilityAvailable reports whether the OS
 * actually exposes a foreground signal on this desktop. On Wayland it maps
 * to @c QWaylandClientExtension::isActive() — @c false when the compositor
 * does not advertise @c zwlr_foreign_toplevel_manager_v1 (e.g. some
 * GNOME/KDE configurations). The UI drives a non-blocking capability-warning
 * chip from this flag; the watcher never crashes and manual switching still
 * works (fail safe, never fail open).
 *
 * Mirrors the @c input_synthesizer.hpp platform-split + free-factory pattern.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ajazz::core {

/**
 * @brief Identity of the currently-focused application window.
 *
 * @c appId is the platform-neutral application identity token used to match
 * against @c Profile::applicationHints:
 *   - Wayland : @c app_id (e.g. @c "firefox")
 *   - X11     : @c WM_CLASS instance/class (e.g. @c "Navigator"/"firefox")
 *   - Windows : process image base name (e.g. @c "firefox.exe" — normalized)
 *   - macOS   : bundle id or localized application name
 *
 * @c title is the human-readable window title (best-effort; may be empty).
 * The matcher (Plan 04) keys on @c appId; @c title is carried for diagnostics
 * and future heuristics only.
 */
struct ActiveWindowInfo {
    std::string appId; ///< Application identity token (see per-platform mapping above).
    std::string title; ///< Window title (best-effort; may be empty).
};

/**
 * @brief Foreground-window observation interface.
 *
 * Implementations are single-instance, owned via @c std::unique_ptr returned
 * by @ref makeDefaultActiveWindowWatcher. They must be constructed on the GUI
 * thread AFTER the QGuiApplication exists (the Wayland backend requires a live
 * Qt platform integration — never at static-init time). Thread-safety is
 * per-implementation; the stub is not thread-safe (UI thread only).
 */
class IActiveWindowWatcher {
public:
    virtual ~IActiveWindowWatcher() = default;

    /**
     * @brief Begin observing foreground-window changes.
     *
     * Stores @p onChange and fires it (after debounce, in real backends) for
     * each distinct foreground application. Calling @c start a second time
     * replaces the previously-registered callback. The callback is invoked on
     * the GUI thread.
     *
     * @param onChange Invoked with the new @ref ActiveWindowInfo on each
     *                 debounced foreground change. Never null in practice; a
     *                 null callback makes @ref start a no-op observer.
     */
    virtual void start(std::function<void(ActiveWindowInfo)> onChange) = 0;

    /**
     * @brief Stop observing and release any OS resources (Wayland bind / X11
     *        property watch). Idempotent; safe to call when not started.
     */
    virtual void stop() = 0;

    /**
     * @brief Whether the OS exposes a usable foreground signal on this desktop.
     *
     * Drives the APROF-03 capability-warning chip. On Wayland this maps to
     * @c QWaylandClientExtension::isActive(): @c false when the compositor does
     * not advertise @c zwlr_foreign_toplevel_manager_v1. When @c false the
     * watcher emits no automatic changes (graceful degradation); manual profile
     * switching remains available.
     *
     * @return @c true if foreground detection is available; @c false otherwise.
     */
    [[nodiscard]] virtual bool capabilityAvailable() const = 0;
};

/**
 * @brief Recording, injectable stub watcher — the default implementation and
 *        the test/debug seam.
 *
 * Used as the default when no native backend is available (every unit test).
 * It records each foreground change into a string @c log_ and exposes a
 * @ref injectForeground test seam that synthesises a foreground change without
 * a real OS focus event — this backs the @c window.setForeground debug RPC
 * (live verification + the < 500 ms switch-latency check) and the Catch2
 * scaffolds. @ref capabilityAvailable is settable so the APROF-03
 * graceful-degradation path can be unit-tested without a degraded compositor.
 *
 * Declared in the public header (unlike the input-synth stub, which is
 * anonymous) precisely BECAUSE the debug facade and the unit tests need the
 * concrete @ref injectForeground / @ref log seam — the factory returns the
 * base interface, so callers @c dynamic_cast to this type to reach the seam.
 * The class is Qt-free and nlohmann-free (COD-031).
 *
 * Not thread-safe (UI thread only).
 */
class StubActiveWindowWatcher final : public IActiveWindowWatcher {
public:
    StubActiveWindowWatcher() = default;
    explicit StubActiveWindowWatcher(bool capabilityAvailable)
        : capabilityAvailable_(capabilityAvailable) {}

    void start(std::function<void(ActiveWindowInfo)> onChange) override;
    void stop() override;
    [[nodiscard]] bool capabilityAvailable() const override;

    /// Set the capability flag (drives the APROF-03 degradation test).
    void setCapabilityAvailable(bool available) noexcept;

    /**
     * @brief Synthesise a foreground change: record it and fire the stored
     *        @c start() callback (if any).
     *
     * This is the Wave-0 "inject a synthetic foreground-app change" seam. It
     * does NOT debounce (the stub is deterministic for tests); the real
     * backends own the timer-based debounce. The recorded log entry has the form
     * @c "fg:<appId>".
     *
     * @param info The synthetic foreground application identity.
     */
    void injectForeground(ActiveWindowInfo info);

    /// Read-only access to the recorded change log (for test assertions).
    [[nodiscard]] std::vector<std::string> const& log() const noexcept;

    /// Clear the change log (e.g. between test cases).
    void clearLog() noexcept;

private:
    std::function<void(ActiveWindowInfo)> onChange_;
    std::vector<std::string> log_;
    bool capabilityAvailable_{true};
};

/**
 * @brief Build the platform-default @ref IActiveWindowWatcher.
 *
 * Returns a @ref StubActiveWindowWatcher when no native backend is compiled /
 * available (the default — used by every unit test). When a platform backend
 * is present, returns it via @c makePlatformActiveWindowWatcher() (Plan 03);
 * on Linux that backend picks wayland-vs-x11 at runtime by session type.
 *
 * Construct from Application init on the GUI thread, never at static-init time.
 */
[[nodiscard]] std::unique_ptr<IActiveWindowWatcher> makeDefaultActiveWindowWatcher();

} // namespace ajazz::core
