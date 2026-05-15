// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mock_transport.hpp
 * @brief CAPTURE-04 (Phase 09, Plan 09-04) — header-only `MockTransport`
 *        fixture for byte-level wire-format unit tests against any
 *        `ajazz::core::ITransport` consumer.
 *
 * MockTransport implements the full `ITransport` surface (open, close,
 * isOpen, write, read, writeFeature, readFeature, stats) and records every
 * `write()` and `writeFeature()` invocation as a separate entry in
 * `writes()`. Tests then assert exact byte equality against the captured
 * vectors — no real HID device is required.
 *
 * Parallels the existing `mock_hid_enumerator.hpp` precedent (same
 * `ajazz::tests` namespace, header-only, no Q_OBJECT, no Qt anywhere) and
 * sits one layer below: `MockHidEnumerator` mocks the device-enumeration
 * seam (which VID/PIDs are "currently connected"); `MockTransport` mocks
 * the per-device I/O seam (what bytes get sent over the wire).
 *
 * Header-only by design (Phase 09 D-04). No `.cpp` translation unit; every
 * member is defined inline so any test TU that includes this header gets
 * the full implementation without a separate CMake link entry.
 *
 * Usage:
 * @code
 *   auto transport = std::make_unique<ajazz::tests::MockTransport>();
 *   auto* observer = transport.get();   // observer ptr; ownership goes to backend
 *   transport->open();
 *
 *   auto device = ajazz::mouse::makeAjSeriesWithTransport(
 *       descriptor, id, std::move(transport));
 *   auto* dpi = dynamic_cast<ajazz::core::IMouseCapable*>(device.get());
 *   dpi->setActiveDpiStage(0);
 *
 *   REQUIRE(observer->writeFeatureCount() == 1);
 *   REQUIRE(observer->writes().at(0).size() == 64);
 *   CHECK(observer->writes().at(0)[1] == 0x21);  // cmd byte
 * @endcode
 *
 * @note ITransport explicitly deletes copy + move; MockTransport inherits
 *       that contract. Hold instances by pointer; transfer ownership via
 *       `TransportPtr = std::unique_ptr<ITransport>` to a device backend.
 * @note Single-threaded by design. Concurrent access from multiple threads
 *       is not supported (parent ITransport doesn't promise thread safety).
 *
 * @see CAPTURE-04, .planning/phases/09-research-captures-hygiene/09-04-PLAN.md
 * @see mock_hid_enumerator.hpp (sibling enumeration-layer mock)
 */
#pragma once

#include "ajazz/core/transport.hpp"

#include <queue>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace ajazz::tests {

/**
 * @brief Header-only `ITransport` mock that captures every `write` and
 *        `writeFeature` invocation for byte-level wire-format assertions.
 *
 * Inspection API (`writes()`, `writeCount()`, `writeFeatureCount()`,
 * `reset()`) is the test-facing surface. Inject canned input report
 * payloads with `enqueueRead()` / `enqueueReadFeature()` (FIFO; empty
 * queue returns 0 bytes from `read` / `readFeature`, simulating a
 * timeout / NAK).
 */
class MockTransport final : public ajazz::core::ITransport {
public:
    MockTransport() = default;

    // ------------------------------------------------------------------
    // Inspection API (test-facing) — none of these are virtual.
    // ------------------------------------------------------------------

    /// Every byte sequence written via `write()` OR `writeFeature()` in
    /// call order. Each entry is a separate invocation; bytes are copied.
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> const& writes() const noexcept {
        return m_writes;
    }

    /// Total number of `write` + `writeFeature` invocations.
    [[nodiscard]] std::size_t writeCount() const noexcept { return m_writes.size(); }

    /// Number of `writeFeature` invocations (subset of `writes()`).
    [[nodiscard]] std::size_t writeFeatureCount() const noexcept { return m_writeFeatureCount; }

    /// Inject a payload returned by the next `read()` call (FIFO).
    void enqueueRead(std::vector<std::uint8_t> bytes) { m_reads.push(std::move(bytes)); }

    /// Inject a payload returned by the next `readFeature()` call (FIFO).
    void enqueueReadFeature(std::vector<std::uint8_t> bytes) {
        m_readFeatures.push(std::move(bytes));
    }

    /// Reset all captured state (writes, queued reads, open flag, stats).
    void reset() noexcept {
        m_writes.clear();
        std::queue<std::vector<std::uint8_t>>{}.swap(m_reads);
        std::queue<std::vector<std::uint8_t>>{}.swap(m_readFeatures);
        m_stats = {};
        m_writeFeatureCount = 0;
        m_open = false;
    }

    // ------------------------------------------------------------------
    // ITransport overrides
    // ------------------------------------------------------------------

    void open() override { m_open = true; }
    void close() override { m_open = false; }
    [[nodiscard]] bool isOpen() const noexcept override { return m_open; }

    std::size_t write(std::span<std::uint8_t const> data) override {
        m_writes.emplace_back(data.begin(), data.end());
        m_stats.bytesSent += data.size();
        return data.size();
    }

    std::size_t writeFeature(std::span<std::uint8_t const> data) override {
        m_writes.emplace_back(data.begin(), data.end());
        m_stats.bytesSent += data.size();
        ++m_writeFeatureCount;
        return data.size();
    }

    std::size_t read(std::span<std::uint8_t> out, std::chrono::milliseconds /*timeout*/) override {
        if (m_reads.empty()) {
            return 0; // simulated timeout / no input report available
        }
        auto const& front = m_reads.front();
        std::size_t const n = std::min(out.size(), front.size());
        std::copy_n(front.begin(), n, out.begin());
        m_stats.bytesReceived += n;
        m_reads.pop();
        return n;
    }

    std::size_t readFeature(std::span<std::uint8_t> out) override {
        if (m_readFeatures.empty()) {
            return 0;
        }
        auto const& front = m_readFeatures.front();
        std::size_t const n = std::min(out.size(), front.size());
        std::copy_n(front.begin(), n, out.begin());
        m_stats.bytesReceived += n;
        m_readFeatures.pop();
        return n;
    }

    [[nodiscard]] ajazz::core::TransportStats stats() const noexcept override { return m_stats; }

private:
    std::vector<std::vector<std::uint8_t>> m_writes;
    std::queue<std::vector<std::uint8_t>> m_reads;
    std::queue<std::vector<std::uint8_t>> m_readFeatures;
    ajazz::core::TransportStats m_stats{};
    std::size_t m_writeFeatureCount{0};
    bool m_open{false};
};

// Lock in the rule-of-five contract inherited from ITransport. If a future
// ITransport edit relaxes copy / move semantics these asserts surface the
// drift as a compile error here, not as a silent ABI change at the call site.
static_assert(std::is_base_of_v<ajazz::core::ITransport, MockTransport>);
static_assert(!std::is_copy_constructible_v<MockTransport>);
static_assert(!std::is_move_constructible_v<MockTransport>);
static_assert(!std::is_copy_assignable_v<MockTransport>);
static_assert(!std::is_move_assignable_v<MockTransport>);

} // namespace ajazz::tests
