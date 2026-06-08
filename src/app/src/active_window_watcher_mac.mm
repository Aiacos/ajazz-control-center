// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_mac.mm
 * @brief macOS foreground-application backend for @ref ajazz::core::IActiveWindowWatcher
 *        (NSWorkspace.frontmostApplication + didActivateApplicationNotification).
 *
 * PLACEHOLDER (Phase 34 Plan 01): the real Obj-C++ backend — an NSWorkspace
 * notification observer resolving the frontmost application's bundle id / name
 * with the QTimer debounce — LANDS IN PLAN 03. Self-empty behind its platform +
 * feature gate so it compiles cleanly until then. This .mm TU is only added to
 * the target in the Darwin branch of src/app/CMakeLists.txt (CMake enables
 * OBJCXX there).
 */
#if defined(__APPLE__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
#include "ajazz/core/active_window_watcher.hpp"
// Real macOS/AppKit backend implementation lands in Plan 03.
#endif // whole TU empty otherwise — still compiles cleanly
