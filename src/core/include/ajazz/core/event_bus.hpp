// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thread-safe event bus used by the app layer to dispatch device events to
// the profile engine, the plugin host and the UI. Single-threaded subscribers
// can wrap their callback in QMetaObject::invokeMethod if needed.
//
#pragma once

#include "ajazz/core/device.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ajazz::core {

class EventBus {
public:
    using Subscription = std::uint64_t;
    using Handler      = std::function<void(DeviceId const&, DeviceEvent const&)>;

    Subscription subscribe(Handler handler);
    void unsubscribe(Subscription token) noexcept;

    /// Publish an event. Handlers are called synchronously on the calling
    /// thread. The bus copies the subscriber list under a lock then releases
    /// it before invoking handlers, so handlers are free to subscribe or
    /// unsubscribe during dispatch without deadlocking.
    void publish(DeviceId const& id, DeviceEvent const& event) const;

private:
    mutable std::mutex                     m_mutex;
    std::unordered_map<Subscription, Handler> m_handlers;
    Subscription                           m_nextToken{1};
};

}  // namespace ajazz::core
