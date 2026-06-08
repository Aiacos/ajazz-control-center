// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_x11.cpp
 * @brief X11/EWMH foreground-window backend for @ref ajazz::core::IActiveWindowWatcher
 *        (_NET_ACTIVE_WINDOW + _NET_WM_PID / WM_CLASS via libX11).
 *
 * PLACEHOLDER (Phase 34 Plan 01): the real backend — Xlib property reads of
 * _NET_ACTIVE_WINDOW and the focused window's WM_CLASS, watched via a
 * PropertyNotify selection on the root window with the QTimer debounce —
 * LANDS IN PLAN 03. Self-empty behind its platform + feature gate so it
 * compiles cleanly on every platform until then.
 */
#if defined(__linux__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
#include "ajazz/core/active_window_watcher.hpp"
// Real X11/EWMH backend implementation lands in Plan 03.
#endif // whole TU empty otherwise — still compiles cleanly on every platform
