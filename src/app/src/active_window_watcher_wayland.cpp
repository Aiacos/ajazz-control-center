// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_wayland.cpp
 * @brief Wayland foreground-window backend for @ref ajazz::core::IActiveWindowWatcher
 *        (zwlr_foreign_toplevel_management_unstable_v1 via Qt6::WaylandClient).
 *
 * PLACEHOLDER (Phase 34 Plan 01): the real backend — a
 * QWaylandClientExtensionTemplate<T> subclass over the qtwaylandscanner-
 * generated QtWayland::zwlr_foreign_toplevel_manager_v1 base, plus the
 * activated-state tracking, app_id/title identity, isActive()->capability
 * gating, and the QTimer debounce — LANDS IN PLAN 03. This TU is added to the
 * app source list now (stable build-list shape) and is self-empty behind its
 * platform + feature gate so it compiles cleanly on every platform until then.
 */
#if defined(__linux__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
#include "ajazz/core/active_window_watcher.hpp"
// Real Wayland backend implementation lands in Plan 03.
#endif // whole TU empty otherwise — still compiles cleanly on every platform
