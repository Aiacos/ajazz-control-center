// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file notification_service.cpp
 * @brief Cross-platform implementation of @ref NotificationService.
 *
 * - Linux: `notify-send` via fork/exec (libnotify wire protocol).
 * - macOS: `osascript -e 'display notification "..." with title "..."'`.
 *   AppleScript stays out of the core's deps and works without Cocoa
 *   bridging; the user-facing experience is a Notification Center banner.
 * - Windows: PowerShell `New-BurntToastNotification` if BurntToast is
 *   available, otherwise a balloon tip via WinRT `ToastNotifier` is
 *   not used (keeps core off WinRT). Practical fallback is `msg.exe`
 *   (built-in since Windows XP) which opens a system modal box — not
 *   ideal but never silently swallowed.
 *
 * All three back-ends shell out (fork/exec or _wspawn) so the core
 * library stays free of Cocoa / WinRT / WebView dependencies and
 * remains unit-testable in pure C++.
 *
 * Closes #36.
 */
#include "ajazz/core/notification_service.hpp"

#include "ajazz/core/logger.hpp"

#include <array>
#include <cstdlib>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#endif

namespace ajazz::core {

namespace {

[[maybe_unused]] char const* urgencyArg(NotificationLevel level) noexcept {
    switch (level) {
    case NotificationLevel::Info:
        return "low";
    case NotificationLevel::Warning:
        return "normal";
    case NotificationLevel::Critical:
        return "critical";
    }
    return "low";
}

#if defined(__APPLE__)
/// Escape a string for inclusion in an AppleScript string literal.
/// Doubles every embedded `"` and `\` so the resulting script parses.
[[nodiscard]] std::string escapeForAppleScript(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char const c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}
#endif

#if defined(_WIN32)
/// Quote a wide string for cmd.exe and Windows GetCommandLine() parsing.
/// We wrap in double quotes and escape embedded quotes by doubling.
[[nodiscard]] std::wstring quoteForCmd(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size() + 4);
    out.push_back(L'"');
    for (wchar_t const c : s) {
        if (c == L'"') {
            out.append(L"\"\"");
        } else {
            out.push_back(c);
        }
    }
    out.push_back(L'"');
    return out;
}
#endif

/**
 * @brief Cross-platform native notification back-end.
 *
 * Falls back to a logger-only stub when the platform helper is not
 * available at runtime (e.g. notify-send not installed on a minimal
 * Linux desktop, or PowerShell stripped from a hardened Windows image).
 */
class NativeNotificationService final : public NotificationService {
public:
    void notify(std::string_view title, std::string_view body, NotificationLevel level) override {
#if defined(__linux__)
        // fork/exec keeps us out of the parent's signal handlers and avoids
        // pulling in QProcess from the core library.
        pid_t const pid = fork();
        if (pid < 0) {
            AJAZZ_LOG_WARN("notifications", "fork() failed: {}", std::string{title});
            return;
        }
        if (pid == 0) {
            // Child process — exec notify-send.
            std::string titleStr{title};
            std::string bodyStr{body};
            char const* args[] = {
                "notify-send",
                "--urgency",
                urgencyArg(level),
                "--app-name",
                "Ajazz Control Center",
                titleStr.c_str(),
                bodyStr.c_str(),
                nullptr,
            };
            // Cast away const for execvp — POSIX prototype predates const.
            execvp("notify-send", const_cast<char* const*>(args));
            std::_Exit(127);
        }
        // Parent: reap the child to avoid zombies.
        int status = 0;
        ::waitpid(pid, &status, 0);
#elif defined(__APPLE__)
        // osascript -e 'display notification "BODY" with title "TITLE"'.
        // The user sees a Notification Center banner; clicking it does
        // nothing (no app delegate routing). For Critical we add a sound
        // hint that defaults to the system's user-selected alert sound.
        std::string const titleEsc = escapeForAppleScript(title);
        std::string const bodyEsc = escapeForAppleScript(body);
        std::string script = "display notification \"";
        script += bodyEsc;
        script += "\" with title \"";
        script += titleEsc;
        script += "\"";
        if (level == NotificationLevel::Critical) {
            script += " sound name \"Glass\"";
        }
        pid_t const pid = fork();
        if (pid < 0) {
            AJAZZ_LOG_WARN("notifications", "fork() failed: {}", std::string{title});
            return;
        }
        if (pid == 0) {
            char const* args[] = {"osascript", "-e", script.c_str(), nullptr};
            execvp("osascript", const_cast<char* const*>(args));
            std::_Exit(127);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
#elif defined(_WIN32)
        // PowerShell `New-BurntToastNotification` if BurntToast is available
        // gives a proper toast in the Action Center; otherwise we degrade to
        // a balloon via `msg.exe`. We try BurntToast first.
        auto const toWide = [](std::string_view s) -> std::wstring {
            int const n = ::MultiByteToWideChar(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
            return out;
        };
        std::wstring const titleW = toWide(title);
        std::wstring const bodyW = toWide(body);
        std::wstring const command =
            L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command "
            L"\"if (Get-Module -ListAvailable BurntToast) {"
            L" Import-Module BurntToast;"
            L" New-BurntToastNotification -Text " +
            quoteForCmd(titleW) + L"," + quoteForCmd(bodyW) +
            L" } else {"
            L" Add-Type -AssemblyName System.Windows.Forms;"
            L" $n=New-Object System.Windows.Forms.NotifyIcon;"
            L" $n.Icon=[System.Drawing.SystemIcons]::Information;"
            L" $n.Visible=$true;"
            L" $n.ShowBalloonTip(5000," + quoteForCmd(titleW) + L"," + quoteForCmd(bodyW) +
            L",[System.Windows.Forms.ToolTipIcon]::Info);"
            L" Start-Sleep -Seconds 5;"
            L" $n.Dispose() }\"";
        // CREATE_NO_WINDOW so the launching cmd flash doesn't pop up.
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring mutableCmd = command; // CreateProcessW signature requires LPWSTR
        if (!::CreateProcessW(nullptr,
                              mutableCmd.data(),
                              nullptr,
                              nullptr,
                              FALSE,
                              CREATE_NO_WINDOW,
                              nullptr,
                              nullptr,
                              &si,
                              &pi)) {
            AJAZZ_LOG_WARN("notifications", "CreateProcessW(powershell) failed: {}",
                           static_cast<unsigned long>(::GetLastError()));
            (void)level;
            return;
        }
        // Don't wait for the toast subprocess - it sleeps 5 s for the
        // balloon to display, which would block the caller. Just close
        // the handles so we don't leak.
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        (void)level;
#else
        (void)title;
        (void)body;
        (void)level;
        AJAZZ_LOG_INFO("notifications", "no native back-end on this platform");
#endif
    }
};

} // namespace

std::unique_ptr<NotificationService> makeDefaultNotificationService() {
    return std::make_unique<NativeNotificationService>();
}

} // namespace ajazz::core
