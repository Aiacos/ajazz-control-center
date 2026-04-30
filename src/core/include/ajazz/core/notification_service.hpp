// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file notification_service.hpp
 * @brief Cross-platform desktop notifications.
 *
 * Closes #36 (system notifications).
 *
 * The default implementation forwards to:
 *   - Linux  : `notify-send` (libnotify, via fork/exec to avoid Qt deps in core).
 *   - macOS  : `NSUserNotificationCenter` (UI-thread shim provided by app layer).
 *   - Windows: ToastNotifier (UI-thread shim provided by app layer).
 *
 * The core only ships the Linux path; macOS / Windows wrappers live in the
 * app layer because they require Cocoa / WinRT toolkits.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ajazz::core {

/**
 * @brief Severity badge attached to notifications. Maps roughly to
 *        `notify-send --urgency`.
 */
enum class NotificationLevel : std::uint8_t {
    Info,     ///< Informational (e.g., profile activated).
    Warning,  ///< Recoverable warning (e.g., battery low).
    Critical, ///< Persistent attention required.
};

/**
 * @brief Pluggable notification sink.
 *
 * Implementations should be thread-safe; `notify` may be called from a
 * background hotplug thread.
 */
class NotificationService {
public:
    virtual ~NotificationService() = default;

    /**
     * @brief Display a desktop notification.
     *
     * @param title   Short headline (≤ 80 chars recommended).
     * @param body    Body text; `\n`-separated lines are rendered verbatim.
     * @param level   Severity badge; defaults to Info.
     */
    virtual void notify(std::string_view title,
                        std::string_view body,
                        NotificationLevel level = NotificationLevel::Info) = 0;
};

/**
 * @brief Build the platform-default notification service.
 *
 * Returns a no-op service when the platform integration is unavailable
 * (e.g., notify-send not in PATH on Linux). All other code paths can rely
 * on the returned pointer being non-null.
 */
[[nodiscard]] std::unique_ptr<NotificationService> makeDefaultNotificationService();

} // namespace ajazz::core
