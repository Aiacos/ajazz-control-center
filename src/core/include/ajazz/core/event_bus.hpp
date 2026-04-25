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

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ajazz::core {

/**
 * @brief Thread-safe publish/subscribe event dispatcher.
 *
 * Multiple subsystems subscribe once at startup; the device backend calls
 * publish() from its I/O thread. The bus takes a snapshot of the subscriber
 * map under a lock and then releases the lock before invoking handlers, so
 * handlers can safely subscribe or unsubscribe during dispatch.
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
     * Takes a snapshot of the handler map under a mutex, then releases the
     * lock before invoking each handler. Handlers may therefore safely call
     * subscribe() or unsubscribe() without deadlocking.
     *
     * @param id    Runtime identifier of the device that generated the event.
     * @param event The input event to dispatch.
     */
    void publish(DeviceId const& id, DeviceEvent const& event) const;

private:
    mutable std::mutex m_mutex;                           ///< Guards m_handlers and m_nextToken.
    std::unordered_map<Subscription, Handler> m_handlers; ///< Active subscriptions.
    Subscription m_nextToken{1}; ///< Monotonically increasing token counter.
};

} // namespace ajazz::core
