// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_win.cpp
 * @brief Win32 foreground-window backend for @ref ajazz::core::IActiveWindowWatcher
 *        (GetForegroundWindow + GetWindowThreadProcessId + QueryFullProcessImageNameW).
 *
 * Polls the foreground window on a QTimer and, on a change, resolves the owning
 * process image and normalizes it to the app-identity contract (image base name
 * without the @c .exe suffix, lowercased — e.g. @c "FIREFOX.EXE" -> @c "firefox").
 * Emits through the shared trailing-edge debounce so an alt-tab storm coalesces to
 * one change (T-34-03-01).
 *
 * COMPILE-GUARDED + unit-tested only: there is no Windows device in this dev
 * environment, so this TU is validated by compiling clean (the build list always
 * includes it) and by the stub-driven debounce/factory unit tests on Linux. The
 * live focus-change walk is a Windows-host task (deferred — Phase 35 WINPLG).
 *
 * MSVC /W4 /WX compliance (CLAUDE.md cross-platform strictness):
 *  - No deprecated CRT (C4996 is a hard error) — uses the @c _s/W APIs only.
 *  - No unused parameters with values.
 *
 * Gated by @c _WIN32 && @c AJAZZ_FEATURE_ACTIVE_WINDOW; the whole TU is empty
 * otherwise so it compiles cleanly on every platform (incl. the Linux build here).
 */
#if defined(_WIN32) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)

#include "active_window_debounce.hpp"
#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/logger.hpp"

#include <QString>
#include <QTimer>

#include <memory>
#include <string>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ajazz::core {

namespace {

/// Foreground-window poll cadence. Combined with the trailing-edge debounce this
/// gives a sub-300ms perceived latency without a kernel hook (anti-feature).
constexpr int kPollIntervalMs = 120;

/// Normalize a process image path to the app-identity token: take the base name,
/// strip a trailing ".exe", and lowercase (matches the Wayland app_id contract).
std::string normalizeImageName(QString const& fullPath) {
    QString base = fullPath;
    int const slash = base.lastIndexOf(QLatin1Char('\\'));
    if (slash >= 0) {
        base = base.mid(slash + 1);
    }
    if (base.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        base.chop(4);
    }
    return base.toLower().toStdString();
}

/**
 * @brief Win32 @ref IActiveWindowWatcher implementation (foreground poll).
 */
class Win32ActiveWindowWatcher final : public IActiveWindowWatcher {
public:
    Win32ActiveWindowWatcher() : debounce_(std::make_unique<ActiveWindowDebouncer>()) {
        timer_.setInterval(kPollIntervalMs);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this]() { poll(); });
    }

    void start(std::function<void(ActiveWindowInfo)> onChange) override {
        debounce_->setCallback(std::move(onChange));
        lastHwnd_ = nullptr;
        lastAppId_.clear();
        timer_.start();
        poll(); // seed immediately
    }

    void stop() override {
        timer_.stop();
        debounce_->setCallback(nullptr);
    }

    // GetForegroundWindow always exposes a usable foreground on Windows.
    [[nodiscard]] bool capabilityAvailable() const override { return true; }

private:
    void poll() {
        HWND const hwnd = GetForegroundWindow();
        if (hwnd == nullptr) {
            return;
        }
        // WR-04: the HWND check is only a cheap fast-path, NOT the identity.
        // HWND values are recycled by Windows after a window is destroyed, so a
        // different application can be assigned a previously-seen HWND. Deduping
        // solely on HWND would (a) suppress a genuine foreground change to a
        // process that inherited the old HWND, and (b) never collapse two windows
        // of the same app. The real identity contract is the resolved appId, so
        // the authoritative dedup happens against lastAppId_ below.
        if (hwnd == lastHwnd_) {
            return;
        }
        lastHwnd_ = hwnd;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) {
            return;
        }
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc == nullptr) {
            return;
        }
        wchar_t buf[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        std::string appId;
        if (QueryFullProcessImageNameW(proc, 0, buf, &size) != 0) {
            appId = normalizeImageName(QString::fromWCharArray(buf, static_cast<int>(size)));
        }
        CloseHandle(proc);
        if (appId.empty()) {
            return;
        }
        // WR-04: dedup on the RESOLVED appId (the identity contract). A recycled
        // HWND that maps to the same app is suppressed here; a change to a new
        // app with a recycled HWND is correctly NOT suppressed.
        if (appId == lastAppId_) {
            return;
        }
        lastAppId_ = appId;

        wchar_t titleBuf[512] = {0};
        int const titleLen = GetWindowTextW(hwnd, titleBuf, 512);
        std::string const title = titleLen > 0
                                      ? QString::fromWCharArray(titleBuf, titleLen).toStdString()
                                      : std::string{};

        debounce_->submit(ActiveWindowInfo{appId, title});
    }

    std::unique_ptr<ActiveWindowDebouncer> debounce_;
    QTimer timer_;
    HWND lastHwnd_{nullptr};
    std::string lastAppId_; ///< WR-04: identity dedup key (resolved image base).
};

} // namespace

std::unique_ptr<IActiveWindowWatcher> makeWin32ActiveWindowWatcher() {
    return std::make_unique<Win32ActiveWindowWatcher>();
}

} // namespace ajazz::core

#endif // defined(_WIN32) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
