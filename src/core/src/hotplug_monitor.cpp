// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hotplug_monitor.cpp
 * @brief Cross-platform implementation of @ref ajazz::core::HotplugMonitor.
 *
 * One translation unit guarded by per-OS `#ifdef` blocks. Each OS provides:
 *   1. A `runImpl(stop_token, callback)` function that pumps events.
 *   2. A way to wake the pump for graceful shutdown.
 *
 * The shared @ref Impl owns a `std::jthread`. Stopping the thread relies on
 * the per-OS wake mechanism (eventfd on Linux, PostMessage on Windows,
 * CFRunLoopStop on macOS) to break out of any blocking poll.
 */
#include "ajazz/core/hotplug_monitor.hpp"

#include "ajazz/core/logger.hpp"

#include <atomic>
#include <thread>

#if defined(__linux__)
#include <fcntl.h>
#include <libudev.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
// Must come after <windows.h>
#include <dbt.h>
#include <hidclass.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#endif

namespace ajazz::core {

struct HotplugMonitor::Impl {
    Callback cb;
    std::atomic<bool> running{false};
    std::jthread worker;

#if defined(__linux__)
    int wakeFd{-1}; ///< eventfd used to interrupt poll() on stop()
#elif defined(_WIN32)
    HWND hidden{nullptr}; ///< message-only window receiving WM_DEVICECHANGE
    DWORD threadId{0};    ///< worker's thread id, for PostThreadMessage
#elif defined(__APPLE__)
    CFRunLoopRef runLoop{nullptr}; ///< worker's run loop, owned by the worker
#endif
};

namespace {

#if defined(__linux__)

/**
 * @brief Linux event loop: poll a udev_monitor + a wake eventfd until stop.
 *
 * Filters on subsystem "usb" so we only see device-level (not interface-level)
 * events. The kernel is the event source; userspace events from networkd or
 * elogind are deliberately ignored.
 */
void runLinux(std::stop_token st, HotplugMonitor::Callback const& cb, int wakeFd) {
    udev* ctx = udev_new();
    if (!ctx) {
        AJAZZ_LOG_WARN("hotplug", "udev_new failed; hotplug disabled");
        return;
    }
    udev_monitor* mon = udev_monitor_new_from_netlink(ctx, "kernel");
    if (!mon) {
        udev_unref(ctx);
        AJAZZ_LOG_WARN("hotplug", "udev_monitor_new_from_netlink failed; hotplug disabled");
        return;
    }
    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", "usb_device");
    udev_monitor_enable_receiving(mon);

    int const monFd = udev_monitor_get_fd(mon);
    pollfd fds[2] = {
        {monFd, POLLIN, 0},
        {wakeFd, POLLIN, 0},
    };

    while (!st.stop_requested()) {
        int const rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            break; // EINTR or fatal
        }
        if (fds[1].revents & POLLIN) {
            std::uint64_t drain = 0;
            ssize_t n = ::read(wakeFd, &drain, sizeof(drain));
            (void)n; // intentionally ignored — eventfd semantics
            break;
        }
        if (!(fds[0].revents & POLLIN)) {
            continue;
        }
        udev_device* dev = udev_monitor_receive_device(mon);
        if (!dev) {
            continue;
        }
        char const* action = udev_device_get_action(dev);
        char const* vidStr = udev_device_get_sysattr_value(dev, "idVendor");
        char const* pidStr = udev_device_get_sysattr_value(dev, "idProduct");
        if (action && vidStr && pidStr) {
            HotplugEvent ev;
            ev.action =
                (std::string{action} == "add") ? HotplugAction::Arrived : HotplugAction::Removed;
            ev.vid = static_cast<std::uint16_t>(std::stoul(vidStr, nullptr, 16));
            ev.pid = static_cast<std::uint16_t>(std::stoul(pidStr, nullptr, 16));
            char const* serial = udev_device_get_sysattr_value(dev, "serial");
            if (serial) {
                ev.serial = serial;
            }
            if (cb) {
                cb(ev);
            }
        }
        udev_device_unref(dev);
    }

    udev_monitor_unref(mon);
    udev_unref(ctx);
}

#elif defined(_WIN32)

/// Window-class atom used for the hidden message-only window. Registered lazily.
LPCWSTR const kWndClass = L"AjazzHotplugMonitor";

/// Parse "USB#VID_0300&PID_1001#..." into vid/pid.
bool parseVidPid(WCHAR const* devName, std::uint16_t& vid, std::uint16_t& pid) {
    if (!devName)
        return false;
    std::wstring s = devName;
    auto const v = s.find(L"VID_");
    auto const p = s.find(L"PID_");
    if (v == std::wstring::npos || p == std::wstring::npos)
        return false;
    vid = static_cast<std::uint16_t>(std::wcstoul(s.c_str() + v + 4, nullptr, 16));
    pid = static_cast<std::uint16_t>(std::wcstoul(s.c_str() + p + 4, nullptr, 16));
    return true;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DEVICECHANGE && (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE)) {
        auto const* hdr = reinterpret_cast<DEV_BROADCAST_HDR const*>(lp);
        if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            auto const* iface = reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W const*>(lp);
            HotplugEvent ev;
            ev.action = (wp == DBT_DEVICEARRIVAL) ? HotplugAction::Arrived : HotplugAction::Removed;
            if (parseVidPid(iface->dbcc_name, ev.vid, ev.pid)) {
                auto* cb = reinterpret_cast<HotplugMonitor::Callback*>(
                    GetWindowLongPtrW(h, GWLP_USERDATA));
                if (cb && *cb) {
                    (*cb)(ev);
                }
            }
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void runWindows(std::stop_token st, HotplugMonitor::Callback const& cb, HWND& outHwnd) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, kWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        AJAZZ_LOG_WARN("hotplug", "CreateWindowEx failed; hotplug disabled");
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&cb));

    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    static GUID const kHidGuid = {
        0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    filter.dbcc_classguid = kHidGuid;
    auto const reg = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    outHwnd = hwnd;

    MSG msg;
    while (!st.stop_requested() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (reg) {
        UnregisterDeviceNotification(reg);
    }
    DestroyWindow(hwnd);
}

#elif defined(__APPLE__)

/// Per-IOService callback bound to a HotplugMonitor::Callback via refcon.
void iokitCb(void* refcon, io_iterator_t it, HotplugAction action) {
    auto* cb = reinterpret_cast<HotplugMonitor::Callback*>(refcon);
    io_service_t s;
    while ((s = IOIteratorNext(it))) {
        HotplugEvent ev;
        ev.action = action;
        CFTypeRef vidRef = IORegistryEntrySearchCFProperty(s,
                                                           kIOServicePlane,
                                                           CFSTR(kUSBVendorID),
                                                           kCFAllocatorDefault,
                                                           kIORegistryIterateRecursively);
        CFTypeRef pidRef = IORegistryEntrySearchCFProperty(s,
                                                           kIOServicePlane,
                                                           CFSTR(kUSBProductID),
                                                           kCFAllocatorDefault,
                                                           kIORegistryIterateRecursively);
        SInt32 vid = 0, pid = 0;
        if (vidRef) {
            CFNumberGetValue(static_cast<CFNumberRef>(vidRef), kCFNumberSInt32Type, &vid);
            CFRelease(vidRef);
        }
        if (pidRef) {
            CFNumberGetValue(static_cast<CFNumberRef>(pidRef), kCFNumberSInt32Type, &pid);
            CFRelease(pidRef);
        }
        ev.vid = static_cast<std::uint16_t>(vid);
        ev.pid = static_cast<std::uint16_t>(pid);
        if (cb && *cb && ev.vid != 0) {
            (*cb)(ev);
        }
        IOObjectRelease(s);
    }
}

void runMacos(std::stop_token st, HotplugMonitor::Callback const& cb, CFRunLoopRef& outLoop) {
    auto port = IONotificationPortCreate(kIOMasterPortDefault);
    if (!port) {
        AJAZZ_LOG_WARN("hotplug", "IONotificationPortCreate failed; hotplug disabled");
        return;
    }
    outLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(outLoop, IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);

    io_iterator_t addedIter = 0, removedIter = 0;
    auto matching = IOServiceMatching(kIOUSBDeviceClassName);
    CFRetain(matching); // We register the dict twice (added + removed).

    auto* cbPtr = const_cast<HotplugMonitor::Callback*>(&cb);

    IOServiceAddMatchingNotification(
        port,
        kIOFirstMatchNotification,
        matching,
        [](void* r, io_iterator_t it) { iokitCb(r, it, HotplugAction::Arrived); },
        cbPtr,
        &addedIter);
    iokitCb(cbPtr, addedIter, HotplugAction::Arrived); // drain initial set

    IOServiceAddMatchingNotification(
        port,
        kIOTerminatedNotification,
        matching,
        [](void* r, io_iterator_t it) { iokitCb(r, it, HotplugAction::Removed); },
        cbPtr,
        &removedIter);
    iokitCb(cbPtr, removedIter, HotplugAction::Removed);

    while (!st.stop_requested()) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
    }

    if (addedIter)
        IOObjectRelease(addedIter);
    if (removedIter)
        IOObjectRelease(removedIter);
    IONotificationPortDestroy(port);
}

#endif

} // namespace

HotplugMonitor::HotplugMonitor(Callback cb) : p_(std::make_unique<Impl>()) {
    p_->cb = std::move(cb);
}

HotplugMonitor::~HotplugMonitor() {
    stop();
}

void HotplugMonitor::setCallback(Callback cb) {
    p_->cb = std::move(cb);
}

bool HotplugMonitor::isRunning() const noexcept {
    return p_->running.load();
}

bool HotplugMonitor::start() {
    if (p_->running.exchange(true)) {
        return true;
    }

#if defined(__linux__)
    p_->wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (p_->wakeFd < 0) {
        p_->running = false;
        return false;
    }
    int wake = p_->wakeFd;
    auto const& cb = p_->cb;
    p_->worker = std::jthread([wake, &cb](std::stop_token st) { runLinux(st, cb, wake); });
    return true;

#elif defined(_WIN32)
    auto const& cb = p_->cb;
    HWND* outHwnd = &p_->hidden;
    DWORD* outTid = &p_->threadId;
    p_->worker = std::jthread([&cb, outHwnd, outTid](std::stop_token st) {
        *outTid = GetCurrentThreadId();
        runWindows(st, cb, *outHwnd);
    });
    return true;

#elif defined(__APPLE__)
    auto const& cb = p_->cb;
    CFRunLoopRef* outLoop = &p_->runLoop;
    p_->worker = std::jthread([&cb, outLoop](std::stop_token st) { runMacos(st, cb, *outLoop); });
    return true;

#else
    AJAZZ_LOG_WARN("hotplug", "HotplugMonitor: no backend for this platform");
    p_->running = false;
    return false;
#endif
}

void HotplugMonitor::stop() {
    if (!p_->running.exchange(false)) {
        return;
    }
    if (p_->worker.joinable()) {
        p_->worker.request_stop();

#if defined(__linux__)
        if (p_->wakeFd >= 0) {
            std::uint64_t one = 1;
            ssize_t n = ::write(p_->wakeFd, &one, sizeof(one));
            (void)n;
        }
#elif defined(_WIN32)
        if (p_->threadId) {
            PostThreadMessageW(p_->threadId, WM_QUIT, 0, 0);
        }
#elif defined(__APPLE__)
        if (p_->runLoop) {
            CFRunLoopStop(p_->runLoop);
        }
#endif

        p_->worker.join();
    }

#if defined(__linux__)
    if (p_->wakeFd >= 0) {
        ::close(p_->wakeFd);
        p_->wakeFd = -1;
    }
#endif
}

} // namespace ajazz::core
