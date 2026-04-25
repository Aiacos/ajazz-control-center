// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file event_bus.hpp
 * @brief Thread-safe publish/subscribe bus for device input events.
 *
 * The app layer uses EventBus to fan out DeviceEvents from backend I/O
 * threads to the profile engine, plugin host, and QML UI. Qt-affine
 * subscribers should wrap their callback in QMetaObject::invokeMethod
 * to re-queue the call on the Qt event loop.
 *
 * @see DeviceEvent, IDevice
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace ajazz::core {

/**
 * @brief Thread-safe publish/subscribe event dispatcher.
 *
 * Multiple subsystems subscribe once at startup; the device backend calls
 * publish() from its I/O thread, often at >100 Hz on encoder-heavy
 * devices like the AKP05.
 *
 * Implementation: copy-on-write of an immutable subscriber vector,
 * exposed via @c std::atomic<std::shared_ptr<HandlerVec const>>.
 *
 *   * @c subscribe / @c unsubscribe — rare. Take a write mutex, copy the
 *     current vector into a new immutable one with the entry added /
 *     removed, atomically swap the shared_ptr.
 *
 *   * @c publish — hot. Atomic-load the current shared_ptr (lock-free),
 *     iterate the immutable vector. The handler list seen by any single
 *     publish() is consistent for the duration of the call: writers
 *     swap a new vector in but never mutate the one publish() is
 *     reading. The shared_ptr keeps it alive past the swap.
 *
 * Handlers may therefore safely call subscribe() / unsubscribe() during
 * dispatch — the publisher is iterating a snapshot the writer cannot
 * touch.
 *
 * @note Handlers are called synchronously on the publisher's thread.
 *       Avoid blocking or long-running work inside a handler.
 */
class EventBus {
public:
    /// Opaque token identifying a live subscription. Used to unsubscribe.
    using Subscription = std::uint64_t;

    /// Signature of an event handler; receives the originating device id
    /// and the event that occurred.
    using Handler = std::function<void(DeviceId const&, DeviceEvent const&)>;

    EventBus();
    ~EventBus() = default;

    EventBus(EventBus const&) = delete;
    EventBus& operator=(EventBus const&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /**
     * @brief Register an event handler.
     *
     * @param handler Callable invoked for every subsequent publish() call.
     * @return Opaque token; pass to unsubscribe() to cancel the subscription.
     */
    Subscription subscribe(Handler handler);

    /**
     * @brief Cancel a subscription.
     *
     * A no-op if `token` is unknown or has already been unsubscribed.
     *
     * @param token Token returned by the corresponding subscribe() call.
     */
    void unsubscribe(Subscription token) noexcept;

    /**
     * @brief Publish an event to all current subscribers.
     *
     * Lock-free in the read path: atomically loads the immutable
     * subscriber vector and iterates it. The shared_ptr keeps the
     * vector alive even if a concurrent writer publishes a replacement,
     * so handlers may freely call subscribe() / unsubscribe() during
     * dispatch.
     *
     * @param id    Runtime identifier of the device that generated the event.
     * @param event The input event to dispatch.
     */
    void publish(DeviceId const& id, DeviceEvent const& event) const;

private:
    /// Immutable list of `(token, handler)` pairs. Writers replace the
    /// whole vector; readers iterate it without locking.
    using HandlerVec = std::vector<std::pair<Subscription, Handler>>;

    /// Copy `*current`, mutate the copy via `mutator`, return the new
    /// shared pointer. Helper used by both subscribe() and unsubscribe()
    /// to keep the copy-on-write pattern in one place.
    template <typename Mutator>
    [[nodiscard]] static std::shared_ptr<HandlerVec const>
    cowCopy(std::shared_ptr<HandlerVec const> const& current, Mutator&& mutator);

    /// Reader/writer lock guarding @c m_handlers. Readers (publish) take
    /// shared ownership; writers (subscribe / unsubscribe) take exclusive
    /// ownership. Chosen over @c std::atomic<std::shared_ptr<...>>
    /// because libc++ on macOS still requires @c is_trivially_copyable
    /// for the atomic specialisation, which @c std::shared_ptr is not.
    /// The shared-mutex path is portable, gives near-lock-free read
    /// throughput on contention-free workloads, and never blocks
    /// concurrent readers.
    mutable std::shared_mutex m_handlersMutex;

    /// Immutable subscriber list. Writers replace it under exclusive
    /// lock; readers copy the shared_ptr under shared lock and iterate
    /// the immutable vector after releasing the lock.
    std::shared_ptr<HandlerVec const> m_handlers;

    /// Monotonically increasing token counter. `std::atomic` so it is
    /// safe to fetch_add without holding @c m_handlersMutex.
    std::atomic<Subscription> m_nextToken{1};
};

} // namespace ajazz::core
