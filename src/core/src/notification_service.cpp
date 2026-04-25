// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file notification_service.cpp
 * @brief Linux `notify-send` implementation of @ref NotificationService.
 *
 * The macOS / Windows back-ends ship in the app layer because they require
 * Cocoa / WinRT toolkits that we do not want to drag into ajazz_core.
 *
 * Closes #36.
 */
#include "ajazz/core/notification_service.hpp"

#include "ajazz/core/logger.hpp"

#include <array>
#include <cstdlib>
#include <string>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
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

/**
 * @brief notify-send-backed implementation. Falls back to a logger-only
 *        stub when notify-send is unavailable at runtime.
 */
class LinuxNotificationService final : public NotificationService {
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
    return std::make_unique<LinuxNotificationService>();
}

} // namespace ajazz::core
