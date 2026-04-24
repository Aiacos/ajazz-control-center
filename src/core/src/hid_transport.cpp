// SPDX-License-Identifier: GPL-3.0-or-later
//
// HID transport backed by libhidapi. Implemented as a thin RAII wrapper so
// backend code can stay platform-agnostic.
//
#include "ajazz/core/logger.hpp"
#include "ajazz/core/transport.hpp"

#include <array>
#include <atomic>
#include <stdexcept>

#include <hidapi/hidapi.h>

namespace ajazz::core {
namespace {

class HidTransport final : public ITransport {
public:
    HidTransport(std::uint16_t vid, std::uint16_t pid, std::string serial)
        : m_vid(vid), m_pid(pid), m_serial(std::move(serial)) {}

    ~HidTransport() override { HidTransport::close(); }

    void open() override {
        if (m_handle) {
            return;
        }
        std::wstring const wserial(m_serial.begin(), m_serial.end());
        m_handle = ::hid_open(m_vid, m_pid, m_serial.empty() ? nullptr : wserial.c_str());
        if (!m_handle) {
            throw std::runtime_error("hid_open failed");
        }
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
            ++m_stats.errors;
            throw std::runtime_error("hid_write failed");
        }
        m_stats.bytesSent += static_cast<std::uint64_t>(n);
        return static_cast<std::size_t>(n);
    }

    std::size_t read(std::span<std::uint8_t> out, std::chrono::milliseconds timeout) override {
        ensureOpen();
        auto const n =
            ::hid_read_timeout(m_handle, out.data(), out.size(), static_cast<int>(timeout.count()));
        if (n < 0) {
            ++m_stats.errors;
            throw std::runtime_error("hid_read_timeout failed");
        }
        m_stats.bytesReceived += static_cast<std::uint64_t>(n);
        return static_cast<std::size_t>(n);
    }

    std::size_t writeFeature(std::span<std::uint8_t const> data) override {
        ensureOpen();
        auto const n = ::hid_send_feature_report(m_handle, data.data(), data.size());
        if (n < 0) {
            ++m_stats.errors;
            throw std::runtime_error("hid_send_feature_report failed");
        }
        return static_cast<std::size_t>(n);
    }

    std::size_t readFeature(std::span<std::uint8_t> out) override {
        ensureOpen();
        auto const n = ::hid_get_feature_report(m_handle, out.data(), out.size());
        if (n < 0) {
            ++m_stats.errors;
            throw std::runtime_error("hid_get_feature_report failed");
        }
        return static_cast<std::size_t>(n);
    }

    [[nodiscard]] TransportStats stats() const noexcept override { return m_stats; }

private:
    void ensureOpen() const {
        if (!m_handle) {
            throw std::runtime_error("HidTransport: device is not open");
        }
    }

    std::uint16_t m_vid{0};
    std::uint16_t m_pid{0};
    std::string m_serial;
    ::hid_device* m_handle{nullptr};
    TransportStats m_stats{};
};

std::atomic<int> gInitCount{0};

} // namespace

// Factory visible to the rest of the core.
TransportPtr makeHidTransport(std::uint16_t vid, std::uint16_t pid, std::string serial) {
    if (gInitCount.fetch_add(1) == 0) {
        ::hid_init();
    }
    return std::make_unique<HidTransport>(vid, pid, std::move(serial));
}

} // namespace ajazz::core
