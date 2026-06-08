// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_factory.cpp
 * @brief Runtime backend-selection factory for @ref ajazz::core::IActiveWindowWatcher
 *        (Phase 34 Plan 03, APROF-01).
 *
 * The four per-OS backends live in app-tier TUs (they pull Qt6::WaylandClient /
 * libX11 / AppKit / user32, which must not cross the COD-031 ajazz_core boundary).
 * This factory selects between them and is the one the Application composes.
 *
 * ## Linux: wayland-vs-x11 chosen at RUNTIME (NOT compile time) — Pitfall 6
 *
 * Under a Wayland session the X11 backend would only ever see XWayland clients
 * (wrong answer), so the choice must be made at runtime by session type. We
 * prefer @c QGuiApplication::platformName() ("wayland" / "xcb") — it reflects the
 * Qt platform integration actually in use — and fall back to the
 * @c XDG_SESSION_TYPE env var when no QGuiApplication is up yet. If the selected
 * native backend reports @c capabilityAvailable()==false (e.g. a Wayland desktop
 * without the wlr global), it is still returned: the watcher degrades gracefully
 * and the UI shows the capability-warning chip (APROF-03) — we do NOT silently
 * fall back to the stub, because that would hide the degradation.
 *
 * Construct from Application init on the GUI thread AFTER QGuiApplication exists
 * (Pitfall 5) — never at static-init time (the Wayland client needs a live Qt
 * platform integration).
 */
#include "active_window_watcher_factory.hpp"

#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/logger.hpp"

#include <memory>

#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
#include <QGuiApplication>
#include <QString>

#include <cstdlib>
#endif

namespace ajazz::core {
#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
// Defined by the per-OS backend TUs (each self-#if-guarded by platform).
#if defined(__linux__)
std::unique_ptr<IActiveWindowWatcher> makeWaylandActiveWindowWatcher();
std::unique_ptr<IActiveWindowWatcher> makeX11ActiveWindowWatcher();
#elif defined(_WIN32)
std::unique_ptr<IActiveWindowWatcher> makeWin32ActiveWindowWatcher();
#elif defined(__APPLE__)
std::unique_ptr<IActiveWindowWatcher> makeMacActiveWindowWatcher();
#endif
#endif
} // namespace ajazz::core

namespace ajazz::app {

#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW) && defined(__linux__)
namespace {

/// True when the running session is Wayland (prefer the live Qt platform name;
/// fall back to XDG_SESSION_TYPE before the QGuiApplication exists).
bool isWaylandSession() {
    if (auto const* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
        gui != nullptr) {
        return QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                        Qt::CaseInsensitive);
    }
    char const* sessionType = std::getenv("XDG_SESSION_TYPE"); // NOLINT(concurrency-mt-unsafe)
    return sessionType != nullptr &&
           QString::fromUtf8(sessionType).compare(QStringLiteral("wayland"), Qt::CaseInsensitive) ==
               0;
}

} // namespace
#endif

std::unique_ptr<core::IActiveWindowWatcher> makeActiveWindowWatcher() {
#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
#if defined(__linux__)
    if (isWaylandSession()) {
        AJAZZ_LOG_INFO("active_window", "selecting Wayland (wlr-foreign-toplevel) backend");
        return core::makeWaylandActiveWindowWatcher();
    }
    AJAZZ_LOG_INFO("active_window", "selecting X11/EWMH backend");
    return core::makeX11ActiveWindowWatcher();
#elif defined(_WIN32)
    AJAZZ_LOG_INFO("active_window", "selecting Win32 GetForegroundWindow backend");
    return core::makeWin32ActiveWindowWatcher();
#elif defined(__APPLE__)
    AJAZZ_LOG_INFO("active_window", "selecting macOS NSWorkspace backend");
    return core::makeMacActiveWindowWatcher();
#else
    AJAZZ_LOG_INFO("active_window", "no native backend on this platform; using recording stub");
    return core::makeDefaultActiveWindowWatcher();
#endif
#else
    AJAZZ_LOG_INFO("active_window", "AJAZZ_FEATURE_ACTIVE_WINDOW off; using recording stub");
    return core::makeDefaultActiveWindowWatcher();
#endif
}

} // namespace ajazz::app
