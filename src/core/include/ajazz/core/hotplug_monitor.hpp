// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hotplug_monitor.hpp
 * @brief Cross-platform USB hot-plug detector.
 *
 * Watches the OS for USB device arrival and removal events and republishes
 * them as @ref HotplugEvent objects. One thread serves the entire process
 * regardless of how many devices are connected.
 *
 * Implementation backends:
 *   - Linux:    libudev monitor on subsystem "usb"
 *   - Windows:  RegisterDeviceNotificationW on a message-only window
 *   - macOS:    IOServiceAddMatchingNotification on IOKit
 *
 * @see docs/architecture/HOTPLUG.md
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ajazz::core {

/// Direction of a USB device change.
enum class HotplugAction : std::uint8_t {
    Arrived = 0, ///< Device just plugged in or became visible to the OS.
    Removed = 1, ///< Device unplugged or otherwise made unavailable.
};

/**
 * @brief One observed USB-device change.
 *
 * The monitor only fills the fields it can extract cheaply from the OS event;
 * `serial` may be empty if the platform did not surface the serial string.
 */
struct HotplugEvent {
    HotplugAction action{HotplugAction::Arrived};
    std::uint16_t vid{0}; ///< USB Vendor ID
    std::uint16_t pid{0}; ///< USB Product ID
    std::string serial;   ///< USB serial string, possibly empty
};

/**
 * @brief Pumps OS hot-plug events on a background thread.
 *
 * Construct, pass a callback, then call @ref start. The callback is invoked
 * from the monitor thread — keep it cheap and post heavy work to your own
 * event queue.
 *
 * @code
 *   HotplugMonitor mon{[](HotplugEvent const& ev){
 *       qInfo() << "USB" << (ev.action == HotplugAction::Arrived ? "+" : "-")
 *               << QString::asprintf("%04x:%04x", ev.vid, ev.pid);
 *   }};
 *   mon.start();
 *   ...
 *   mon.stop();
 * @endcode
 *
 * @note Safe to construct with no listener; the monitor simply silences the
 *       events. This keeps the API testable on CI runners without USB.
 */
class HotplugMonitor {
public:
    using Callback = std::function<void(HotplugEvent const&)>;

    /// Construct without starting. @p cb may be null; set later with @ref setCallback.
    explicit HotplugMonitor(Callback cb = {});

    HotplugMonitor(HotplugMonitor const&) = delete;
    HotplugMonitor& operator=(HotplugMonitor const&) = delete;

    /// Stops and joins the monitor thread if running.
    ~HotplugMonitor();

    /// Replace the callback. Safe to call before or after @ref start.
    void setCallback(Callback cb);

    /**
     * @brief Start the OS event source on a dedicated thread.
     *
     * @return true on success. Returns false (and logs) if the platform
     *         backend cannot initialise — typical causes: udev not running
     *         in a container, or no privileges to register the WND_PROC.
     */
    bool start();

    /// Request stop and join. Idempotent.
    void stop();

    /// True between a successful @ref start and a subsequent @ref stop.
    [[nodiscard]] bool isRunning() const noexcept;

    /**
     * @brief Opaque platform state owned by the monitor.
     *
     * Forward-declared as public so the per-OS event-loop helpers in the
     * .cpp file can take @c Impl& parameters and call @c snapshotCallback()
     * without being friend-listed individually. The full definition lives
     * in hotplug_monitor.cpp.
     */
    struct Impl;

private:
    std::unique_ptr<Impl> p_;
};

} // namespace ajazz::core
