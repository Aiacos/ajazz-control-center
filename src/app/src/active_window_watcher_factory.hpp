// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_factory.hpp
 * @brief App-tier runtime factory for @ref ajazz::core::IActiveWindowWatcher.
 *
 * Application composes the foreground-window watcher through this seam. On Linux
 * it selects the Wayland (wlr-foreign-toplevel) vs X11/EWMH backend at RUNTIME by
 * session type; on Windows/macOS it returns the compile-guarded native backend;
 * elsewhere (or when the feature is off) it returns the core recording stub.
 *
 * Kept app-tier because the implementations pull Qt6::WaylandClient / libX11 /
 * AppKit / user32 — the @ref ajazz::core interface header stays Qt-free (COD-031).
 */
#pragma once

#include "ajazz/core/active_window_watcher.hpp"

#include <memory>

namespace ajazz::app {

/**
 * @brief Build the platform-appropriate foreground-window watcher.
 *
 * MUST be called on the GUI thread after the QGuiApplication exists (the Wayland
 * client extension needs a live Qt platform integration — Pitfall 5). Returns the
 * native backend (even when its @c capabilityAvailable() is false, so the UI can
 * surface the APROF-03 degradation chip) or the recording stub on unsupported
 * platforms / when the feature gate is off.
 */
[[nodiscard]] std::unique_ptr<core::IActiveWindowWatcher> makeActiveWindowWatcher();

} // namespace ajazz::app
