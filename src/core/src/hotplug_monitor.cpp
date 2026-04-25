// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hotplug_monitor.cpp
 * @brief Cross-platform implementation of @ref ajazz::core::HotplugMonitor.
 *
 * One translation unit guarded by per-OS `#ifdef` blocks. Each OS provides:
 *   1. A `runImpl(stop, callback)` function that pumps events.
 *   2. A way to wake the pump for graceful shutdown.
 *
 * The shared @ref Impl owns a `std::thread`. Stopping the thread relies on
 * the per-OS wake mechanism (eventfd on Linux, PostThreadMessage on Windows,
 * CFRunLoopStop on macOS) to break out of any blocking poll. We deliberately
 * avoid `std::jthread` because Apple Clang did not ship `<stop_token>` until
 * macOS 13.3 / Xcode 14.3 and we still target macOS 12 runners on CI.
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
    std::atomic<bool> stopFlag{false};
    std::thread worker;

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
void runLinux(std::atomic<bool> const& stop, HotplugMonitor::Callback const& cb, int wakeFd) {
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

    while (!stop.load(std::memory_order_acquire)) {
        int const rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            break; // EINTR or fatal
        }
        if (fds[1].revents & POLLIN) {
            std::uint64_t drain = 0;
            ssize_t n = ::read(wakeFd, &drain, sizeof(drain));
            (void)n;
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
    if (!devName) {
        return false;
    }
    std::wstring s = devName;
    auto const v = s.find(L"VID_");
    auto const p = s.find(L"PID_");
    if (v == std::wstring::npos || p == std::wstring::npos) {
        return false;
    }
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
                auto* impl =
                    reinterpret_cast<HotplugMonitor::Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
                if (impl) {
                    if (auto cb = impl->snapshotCallback()) {
                        cb(ev);
                    }
                }
            }
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void runWindows(std::atomic<bool> const& stop, HotplugMonitor::Impl& impl, HWND& outHwnd) {
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
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&impl));

    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    static GUID const kHidGuid = {
        0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    filter.dbcc_classguid = kHidGuid;
    auto const reg = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    outHwnd = hwnd;

    MSG msg;
    while (!stop.load(std::memory_order_acquire) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (reg) {
        UnregisterDeviceNotification(reg);
    }
    DestroyWindow(hwnd);
}

#elif defined(__APPLE__)

/// Per-IOService callback bound to a HotplugMonitor::Impl via refcon.
void iokitCb(void* refcon, io_iterator_t it, HotplugAction action) {
    auto* impl = reinterpret_cast<HotplugMonitor::Impl*>(refcon);
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
        SInt32 vid = 0;
        SInt32 pid = 0;
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
        if (impl && ev.vid != 0) {
            if (auto cb = impl->snapshotCallback()) {
                cb(ev);
            }
        }
        IOObjectRelease(s);
    }
}

void runMacos(std::atomic<bool> const& stop, HotplugMonitor::Impl& impl, CFRunLoopRef& outLoop) {
    // kIOMasterPortDefault was deprecated on macOS 12; use kIOMainPortDefault when available.
#if defined(MAC_OS_VERSION_12_0) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0
    auto port = IONotificationPortCreate(kIOMainPortDefault);
#else
    auto port = IONotificationPortCreate(kIOMasterPortDefault);
#endif
    if (!port) {
        AJAZZ_LOG_WARN("hotplug", "IONotificationPortCreate failed; hotplug disabled");
        return;
    }
    outLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(outLoop, IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);

    io_iterator_t addedIter = 0;
    io_iterator_t removedIter = 0;
    auto matching = IOServiceMatching(kIOUSBDeviceClassName);
    CFRetain(matching); // We register the dict twice (added + removed).

    void* implPtr = &impl;

    IOServiceAddMatchingNotification(
        port,
        kIOFirstMatchNotification,
        matching,
        [](void* r, io_iterator_t it) { iokitCb(r, it, HotplugAction::Arrived); },
        implPtr,
        &addedIter);
    iokitCb(implPtr, addedIter, HotplugAction::Arrived); // drain initial set

    IOServiceAddMatchingNotification(
        port,
        kIOTerminatedNotification,
        matching,
        [](void* r, io_iterator_t it) { iokitCb(r, it, HotplugAction::Removed); },
        implPtr,
        &removedIter);
    iokitCb(implPtr, removedIter, HotplugAction::Removed);

    while (!stop.load(std::memory_order_acquire)) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
    }

    if (addedIter) {
        IOObjectRelease(addedIter);
    }
    if (removedIter) {
        IOObjectRelease(removedIter);
    }
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
    std::lock_guard lock(p_->cbMu);
    p_->cb = std::move(cb);
}

bool HotplugMonitor::isRunning() const noexcept {
    return p_->running.load();
}

bool HotplugMonitor::start() {
    if (p_->running.exchange(true)) {
        return true;
    }
    p_->stopFlag.store(false);

#if defined(__linux__)
    p_->wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (p_->wakeFd < 0) {
        p_->running = false;
        return false;
    }
    int wake = p_->wakeFd;
    Impl* impl = p_.get();
    p_->worker = std::thread([impl, wake] { runLinux(impl->stopFlag, impl->cb, wake); });
    return true;

#elif defined(_WIN32)
    Impl* impl = p_.get();
    p_->worker = std::thread([impl] {
        impl->threadId = GetCurrentThreadId();
        runWindows(impl->stopFlag, *impl, impl->hidden);
    });
    return true;

#elif defined(__APPLE__)
    Impl* impl = p_.get();
    p_->worker = std::thread([impl] { runMacos(impl->stopFlag, *impl, impl->runLoop); });
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
    p_->stopFlag.store(true, std::memory_order_release);

    if (p_->worker.joinable()) {
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
