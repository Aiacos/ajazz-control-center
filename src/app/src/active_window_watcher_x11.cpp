// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_x11.cpp
 * @brief X11/EWMH foreground-window backend for @ref ajazz::core::IActiveWindowWatcher
 *        (_NET_ACTIVE_WINDOW + _NET_WM_PID / WM_CLASS via libX11).
 *
 * Opens its OWN Xlib display connection (independent of Qt's so we can select
 * PropertyChange events on the root window without disturbing Qt), selects
 * @c PropertyNotify on the root window, and integrates the connection fd into the
 * Qt event loop via a @c QSocketNotifier — NO subprocess (xdotool/wmctrl are
 * forbidden, 34-RESEARCH Anti-Patterns / threat T-34-03-03).
 *
 * On each @c _NET_ACTIVE_WINDOW change it resolves the focused window's identity:
 *   - primary token: @c WM_CLASS instance/class (app-id-like, e.g. "firefox"),
 *   - title (best-effort): @c _NET_WM_NAME (UTF-8) falling back to @c WM_NAME.
 * The app id is lowercased so it matches the same case-insensitive contract as
 * the Wayland @c app_id (see ACTIVE-WINDOW-IDENTITY in plugin-event-parity doc).
 *
 * The compositor/WM-reported WM_CLASS is an untrusted string: it is carried as a
 * plain match token and never eval'd/exec'd/shelled out (threat T-34-03-03).
 *
 * Gated by @c __linux__ && @c AJAZZ_FEATURE_ACTIVE_WINDOW; the whole TU is empty
 * otherwise so it compiles cleanly on every platform.
 */
#if defined(__linux__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)

#include "active_window_debounce.hpp"
#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/logger.hpp"

#include <QSocketNotifier>
#include <QString>

#include <memory>
#include <string>
#include <utility>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h> // XClassHint / XGetClassHint

namespace ajazz::core {

namespace {

/// Lowercase an ASCII-ish app id so X11 WM_CLASS matches the Wayland app_id case
/// contract (the matcher in Plan 04 is case-insensitive).
std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

/**
 * @brief X11/EWMH @ref IActiveWindowWatcher implementation.
 *
 * Holds a dedicated Xlib display, an interned-atom cache, and a QSocketNotifier
 * driving the read pump. Degrades gracefully: if the display cannot be opened
 * (no X server — e.g. a pure-Wayland session where this backend was selected by
 * mistake) it reports @c capabilityAvailable()==false and emits nothing.
 */
class X11ActiveWindowWatcher final : public IActiveWindowWatcher {
public:
    X11ActiveWindowWatcher() : debounce_(std::make_unique<ActiveWindowDebouncer>()) {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
            AJAZZ_LOG_WARN("active_window",
                           "X11 backend: XOpenDisplay failed; capabilityAvailable=false");
            return;
        }
        root_ = DefaultRootWindow(display_);
        atomActiveWindow_ = XInternAtom(display_, "_NET_ACTIVE_WINDOW", False);
        atomNetWmName_ = XInternAtom(display_, "_NET_WM_NAME", False);
        atomUtf8String_ = XInternAtom(display_, "UTF8_STRING", False);

        // Watch the root window for _NET_ACTIVE_WINDOW property changes.
        XSelectInput(display_, root_, PropertyChangeMask);

        int const fd = ConnectionNumber(display_);
        notifier_ = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
        QObject::connect(
            notifier_.get(), &QSocketNotifier::activated, notifier_.get(), [this]() { pump(); });
        capable_ = true;
        AJAZZ_LOG_INFO("active_window", "X11ActiveWindowWatcher constructed (display open)");
    }

    ~X11ActiveWindowWatcher() override {
        notifier_.reset();
        if (display_ != nullptr) {
            XCloseDisplay(display_);
        }
    }

    X11ActiveWindowWatcher(X11ActiveWindowWatcher const&) = delete;
    X11ActiveWindowWatcher& operator=(X11ActiveWindowWatcher const&) = delete;
    X11ActiveWindowWatcher(X11ActiveWindowWatcher&&) = delete;
    X11ActiveWindowWatcher& operator=(X11ActiveWindowWatcher&&) = delete;

    void start(std::function<void(ActiveWindowInfo)> onChange) override {
        debounce_->setCallback(std::move(onChange));
        if (capable_) {
            emitCurrent(); // seed with the current foreground
        }
    }

    void stop() override { debounce_->setCallback(nullptr); }

    [[nodiscard]] bool capabilityAvailable() const override { return capable_; }

private:
    // Drain pending X events; on a root _NET_ACTIVE_WINDOW change, re-resolve.
    void pump() {
        if (display_ == nullptr) {
            return;
        }
        while (XPending(display_) > 0) {
            XEvent ev;
            XNextEvent(display_, &ev);
            if (ev.type == PropertyNotify && ev.xproperty.window == root_ &&
                ev.xproperty.atom == atomActiveWindow_) {
                emitCurrent();
            }
        }
    }

    void emitCurrent() {
        Window const active = activeWindow();
        if (active == 0) {
            return; // no active window reported; keep current (Pitfall 4 analog)
        }
        std::string const appId = toLowerAscii(wmClass(active));
        if (appId.empty()) {
            return;
        }
        debounce_->submit(ActiveWindowInfo{appId, windowTitle(active)});
    }

    [[nodiscard]] Window activeWindow() const {
        Atom actualType = 0;
        int actualFormat = 0;
        unsigned long nItems = 0;
        unsigned long bytesAfter = 0;
        unsigned char* prop = nullptr;
        Window result = 0;
        if (XGetWindowProperty(display_,
                               root_,
                               atomActiveWindow_,
                               0,
                               1,
                               False,
                               XA_WINDOW,
                               &actualType,
                               &actualFormat,
                               &nItems,
                               &bytesAfter,
                               &prop) == Success &&
            prop != nullptr) {
            if (nItems >= 1) {
                result = *reinterpret_cast<Window*>(prop);
            }
            XFree(prop);
        }
        return result;
    }

    // WM_CLASS is two consecutive NUL-terminated strings (instance, class); we use
    // the instance (first), which is the app-id-like token (e.g. "firefox").
    [[nodiscard]] std::string wmClass(Window win) const {
        XClassHint hint{nullptr, nullptr};
        std::string instance;
        if (XGetClassHint(display_, win, &hint) != 0) {
            if (hint.res_name != nullptr) {
                instance = hint.res_name;
            } else if (hint.res_class != nullptr) {
                instance = hint.res_class;
            }
            if (hint.res_name != nullptr) {
                XFree(hint.res_name);
            }
            if (hint.res_class != nullptr) {
                XFree(hint.res_class);
            }
        }
        return instance;
    }

    [[nodiscard]] std::string windowTitle(Window win) const {
        // Prefer _NET_WM_NAME (UTF-8); fall back to WM_NAME.
        Atom actualType = 0;
        int actualFormat = 0;
        unsigned long nItems = 0;
        unsigned long bytesAfter = 0;
        unsigned char* prop = nullptr;
        std::string title;
        if (XGetWindowProperty(display_,
                               win,
                               atomNetWmName_,
                               0,
                               1024,
                               False,
                               atomUtf8String_,
                               &actualType,
                               &actualFormat,
                               &nItems,
                               &bytesAfter,
                               &prop) == Success &&
            prop != nullptr) {
            title = reinterpret_cast<char const*>(prop);
            XFree(prop);
        }
        if (title.empty()) {
            char* name = nullptr;
            if (XFetchName(display_, win, &name) != 0 && name != nullptr) {
                title = name;
                XFree(name);
            }
        }
        return title;
    }

    std::unique_ptr<ActiveWindowDebouncer> debounce_;
    std::unique_ptr<QSocketNotifier> notifier_;
    Display* display_{nullptr};
    Window root_{0};
    Atom atomActiveWindow_{0};
    Atom atomNetWmName_{0};
    Atom atomUtf8String_{0};
    bool capable_{false};
};

} // namespace

std::unique_ptr<IActiveWindowWatcher> makeX11ActiveWindowWatcher() {
    return std::make_unique<X11ActiveWindowWatcher>();
}

} // namespace ajazz::core

#endif // defined(__linux__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
