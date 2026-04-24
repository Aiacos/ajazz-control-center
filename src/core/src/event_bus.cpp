// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/event_bus.hpp"

namespace ajazz::core {

EventBus::Subscription EventBus::subscribe(Handler handler) {
    std::lock_guard const lock(m_mutex);
    auto const token = m_nextToken++;
    m_handlers.emplace(token, std::move(handler));
    return token;
}

void EventBus::unsubscribe(Subscription token) noexcept {
    std::lock_guard const lock(m_mutex);
    m_handlers.erase(token);
}

void EventBus::publish(DeviceId const& id, DeviceEvent const& event) const {
    std::unordered_map<Subscription, Handler> snapshot;
    {
        std::lock_guard const lock(m_mutex);
        snapshot = m_handlers;
    }
    for (auto const& [token, handler] : snapshot) {
        handler(id, event);
    }
}

} // namespace ajazz::core
