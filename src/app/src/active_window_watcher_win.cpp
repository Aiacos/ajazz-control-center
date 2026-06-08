// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_win.cpp
 * @brief Win32 foreground-window backend for @ref ajazz::core::IActiveWindowWatcher
 *        (GetForegroundWindow + GetWindowThreadProcessId + QueryFullProcessImageNameW).
 *
 * PLACEHOLDER (Phase 34 Plan 01): the real backend — a foreground poll/hook
 * resolving the focused HWND to its process image base name with the QTimer
 * debounce — LANDS IN PLAN 03. Self-empty behind its platform + feature gate so
 * it compiles cleanly on every platform until then.
 */
#if defined(_WIN32) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
#include "ajazz/core/active_window_watcher.hpp"
// Real Win32 backend implementation lands in Plan 03.
#endif // whole TU empty otherwise — still compiles cleanly on every platform
