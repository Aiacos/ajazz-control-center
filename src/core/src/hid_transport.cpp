// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hid_transport.cpp
 * @brief libhidapi-backed ITransport implementation.
 *
 * HidTransport is a thin RAII wrapper around hid_open/hid_close/hid_write/
 * hid_read_timeout so that all device backends remain platform-agnostic.
 * A process-wide reference count (gInitCount) ensures hid_init() is called
 * exactly once regardless of how many transports are created.
 */
#include "ajazz/core/logger.hpp"
#include "ajazz/core/transport.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// hidapi's top-level CMake target exports its headers without the "hidapi/"
// prefix (this matches Debian's /usr/include/hidapi symlink and the upstream
// Windows headers). Use the unprefixed form.
#include <hidapi.h>

namespace ajazz::core {
namespace {

/**
 * @brief Concrete ITransport implementation backed by libhidapi.
 *
 * All I/O is synchronous (non-blocking mode is enabled on open so that
 * read() with a zero-millisecond timeout returns immediately). The RAII
 * destructor calls close() so the device handle is always freed.
 *
 * @note Not copyable or movable; always held via TransportPtr.
 */
class HidTransport final : public ITransport {
public:
    /**
     * @brief Construct a closed transport for the given USB device.
     *
     * @param vid    USB Vendor ID.
     * @param pid    USB Product ID.
     * @param serial Serial number string; empty means first matching device.
     */
    HidTransport(std::uint16_t vid, std::uint16_t pid, std::string serial)
        : m_vid(vid), m_pid(pid), m_serial(std::move(serial)) {}

    ~HidTransport() override { HidTransport::close(); }

    void open() override {
        if (m_handle) {
            return;
        }
        // libhidapi takes a wchar_t* serial; convert from UTF-8 -> UTF-32/UTF-16
        // properly so non-ASCII serials (rare but possible on some firmwares)
        // round-trip correctly. The naive char->wchar_t copy was wrong: it
        // truncated multi-byte sequences and produced unmatched filters.
        std::wstring const wserial = utf8ToWide(m_serial);
        m_handle = ::hid_open(m_vid, m_pid, m_serial.empty() ? nullptr : wserial.c_str());
        if (!m_handle) {
            throw std::runtime_error("hid_open failed");
        }
        // Enable non-blocking mode so zero-timeout reads return immediately.
        ::hid_set_nonblocking(m_handle, 1);
        AJAZZ_LOG_INFO("hid", "opened VID={:04x} PID={:04x}", m_vid, m_pid);
    }

    void close() override {
        if (m_handle) {
            ::hid_close(m_handle);
            m_handle = nullptr;
            AJAZZ_LOG_INFO("hid", "closed VID={:04x} PID={:04x}", m_vid, m_pid);
        }
    }

    [[nodiscard]] bool isOpen() const noexcept override { return m_handle != nullptr; }

    std::size_t write(std::span<std::uint8_t const> data) override {
        ensureOpen();
        auto const n = ::hid_write(m_handle, data.data(), data.size());
        if (n < 0) {
            m_errors.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("hid_write failed");
        }
        m_bytesSent.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        return static_cast<std::size_t>(n);
    }

    std::size_t read(std::span<std::uint8_t> out, std::chrono::milliseconds timeout) override {
        ensureOpen();
        auto const n =
            ::hid_read_timeout(m_handle, out.data(), out.size(), static_cast<int>(timeout.count()));
        if (n < 0) {
            m_errors.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("hid_read_timeout failed");
        }
        m_bytesReceived.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        return static_cast<std::size_t>(n);
    }

    std::size_t writeFeature(std::span<std::uint8_t const> data) override {
        ensureOpen();
        auto const n = ::hid_send_feature_report(m_handle, data.data(), data.size());
        if (n < 0) {
            m_errors.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("hid_send_feature_report failed");
        }
        m_bytesSent.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        return static_cast<std::size_t>(n);
    }

    std::size_t readFeature(std::span<std::uint8_t> out) override {
        ensureOpen();
        auto const n = ::hid_get_feature_report(m_handle, out.data(), out.size());
        if (n < 0) {
            m_errors.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("hid_get_feature_report failed");
        }
        m_bytesReceived.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        return static_cast<std::size_t>(n);
    }

    [[nodiscard]] TransportStats stats() const noexcept override {
        TransportStats s;
        s.bytesSent = m_bytesSent.load(std::memory_order_relaxed);
        s.bytesReceived = m_bytesReceived.load(std::memory_order_relaxed);
        s.errors = m_errors.load(std::memory_order_relaxed);
        return s;
    }

private:
    /// Throw if the device handle is null (i.e. not yet opened).
    void ensureOpen() const {
        if (!m_handle) {
            throw std::runtime_error("HidTransport: device is not open");
        }
    }

    /**
     * @brief Convert a UTF-8 string to wstring as expected by libhidapi.
     *
     * On systems with 4-byte wchar_t (Linux, macOS) decodes UTF-8 to UTF-32.
     * On Windows (2-byte wchar_t) decodes UTF-8 to UTF-16 with surrogate
     * pairs for code points above the BMP. Invalid bytes are replaced by
     * U+FFFD.
     */
    static std::wstring utf8ToWide(std::string_view in) {
        std::wstring out;
        out.reserve(in.size());
        std::size_t i = 0;
        while (i < in.size()) {
            auto b0 = static_cast<unsigned char>(in[i]);
            std::uint32_t cp = 0xFFFD;
            std::size_t step = 1;
            if (b0 < 0x80) {
                cp = b0;
            } else if ((b0 & 0xE0) == 0xC0 && i + 1 < in.size()) {
                cp = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3F);
                step = 2;
            } else if ((b0 & 0xF0) == 0xE0 && i + 2 < in.size()) {
                cp = ((b0 & 0x0F) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(in[i + 2]) & 0x3F);
                step = 3;
            } else if ((b0 & 0xF8) == 0xF0 && i + 3 < in.size()) {
                cp = ((b0 & 0x07) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12) |
                     ((static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(in[i + 3]) & 0x3F);
                step = 4;
            }
            if constexpr (sizeof(wchar_t) >= 4) {
                out.push_back(static_cast<wchar_t>(cp));
            } else {
                if (cp <= 0xFFFF) {
                    out.push_back(static_cast<wchar_t>(cp));
                } else {
                    // UTF-16 surrogate pair.
                    cp -= 0x10000;
                    out.push_back(static_cast<wchar_t>(0xD800 | (cp >> 10)));
                    out.push_back(static_cast<wchar_t>(0xDC00 | (cp & 0x3FF)));
                }
            }
            i += step;
        }
        return out;
    }

    std::uint16_t m_vid{0};          ///< USB Vendor ID.
    std::uint16_t m_pid{0};          ///< USB Product ID.
    std::string m_serial;            ///< Serial number filter; empty = first match.
    ::hid_device* m_handle{nullptr}; ///< libhidapi device handle; nullptr when closed.
    /// Atomic counters; reads happen on threads other than the I/O thread (UI/diagnostics).
    std::atomic<std::uint64_t> m_bytesSent{0};
    std::atomic<std::uint64_t> m_bytesReceived{0};
    std::atomic<std::uint64_t> m_errors{0};
};

/**
 * @brief Process-wide hidapi init/exit lifetime guard.
 *
 * libhidapi requires `hid_init()` to be balanced by `hid_exit()`. We model
 * that with a strict reference-count: the first transport created bumps the
 * count to 1 and calls hid_init(); when the last transport is destroyed the
 * count drops to 0 and hid_exit() runs. A static instance ensures hid_exit()
 * runs even if the user forgets to release transports before main() returns.
 */
class HidLibraryGuard {
public:
    /// Increment refcount; call hid_init on the 0->1 transition.
    void acquire() {
        if (m_count.fetch_add(1, std::memory_order_acq_rel) == 0) {
            if (::hid_init() != 0) {
                m_count.fetch_sub(1, std::memory_order_acq_rel);
                throw std::runtime_error("hid_init failed");
            }
            AJAZZ_LOG_INFO("hid", "hidapi initialised");
        }
    }
    /// Decrement refcount; call hid_exit on the 1->0 transition.
    void release() noexcept {
        if (m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ::hid_exit();
            AJAZZ_LOG_INFO("hid", "hidapi shut down");
        }
    }

private:
    std::atomic<int> m_count{0};
};

/// Singleton guard; lifetime tied to the process.
HidLibraryGuard& hidLibrary() {
    static HidLibraryGuard g;
    return g;
}

} // namespace

/**
 * @brief Instantiate an HID transport, initialising hidapi on first call.
 *
 * Uses HidLibraryGuard's reference count to balance hid_init() and hid_exit()
 * across the process lifetime. The transport's destructor releases the
 * reference, so hid_exit() runs once the last transport is gone.
 *
 * @param vid    USB Vendor ID.
 * @param pid    USB Product ID.
 * @param serial Optional serial number; empty means first matching device.
 * @return Closed TransportPtr; call open() before I/O.
 */
TransportPtr makeHidTransport(std::uint16_t vid, std::uint16_t pid, std::string serial) {
    /**
     * @brief Decorator that holds a HidLibraryGuard reference for the
     *        transport's lifetime, balancing hid_init / hid_exit.
     */
    class GuardedHidTransport final : public HidTransport {
    public:
        using HidTransport::HidTransport;
        ~GuardedHidTransport() override { hidLibrary().release(); }
    };
    hidLibrary().acquire();
    return std::make_unique<GuardedHidTransport>(vid, pid, std::move(serial));
}

} // namespace ajazz::core
