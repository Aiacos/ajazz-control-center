// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file event_bus.cpp
 * @brief EventBus implementation — copy-on-write immutable subscriber list.
 *
 * subscribe() / unsubscribe() take @c m_writeMutex, build a fresh
 * immutable @c HandlerVec from the current one with the entry added /
 * removed, and atomically swap the new @c shared_ptr in.
 *
 * publish() is lock-free: atomically loads the @c shared_ptr, iterates
 * the immutable vector. The shared_ptr keeps the vector alive past any
 * concurrent writer's swap, so handlers may safely subscribe() /
 * unsubscribe() during dispatch.
 *
 * The cost shifts from per-publish (was: copy unordered_map<Subscription,
 * Handler> on every event) to per-write (now: copy vector once per
 * subscribe / unsubscribe). Subscriptions are rare; events are >100 Hz
 * on encoder-heavy devices.
 */
#include "ajazz/core/event_bus.hpp"

#include <algorithm>

namespace ajazz::core {

EventBus::EventBus() : m_handlers(std::make_shared<HandlerVec const>()) {}

template <typename Mutator>
std::shared_ptr<EventBus::HandlerVec const>
EventBus::cowCopy(std::shared_ptr<HandlerVec const> const& current, Mutator&& mutator) {
    auto fresh = std::make_shared<HandlerVec>(*current); // deep copy of immutable old list
    std::forward<Mutator>(mutator)(*fresh);
    // Convert from shared_ptr<HandlerVec> to shared_ptr<HandlerVec const>
    // by aliasing — same control block, const view of the data.
    return std::shared_ptr<HandlerVec const>(fresh, fresh.get());
}

EventBus::Subscription EventBus::subscribe(Handler handler) {
    auto const token = m_nextToken.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock const lock(m_handlersMutex);
    auto next =
        cowCopy(m_handlers, [&](HandlerVec& vec) { vec.emplace_back(token, std::move(handler)); });
    m_handlers = std::move(next);
    return token;
}

void EventBus::unsubscribe(Subscription token) noexcept {
    std::unique_lock const lock(m_handlersMutex);
    // Fast path: if the token isn't present we don't allocate a new
    // vector. Saves an allocation on the (common) case where a caller
    // unsubscribes twice.
    auto const found = std::any_of(m_handlers->begin(), m_handlers->end(), [token](auto const& kv) {
        return kv.first == token;
    });
    if (!found) {
        return;
    }
    auto next = cowCopy(m_handlers, [token](HandlerVec& vec) {
        vec.erase(std::remove_if(vec.begin(),
                                 vec.end(),
                                 [token](auto const& kv) { return kv.first == token; }),
                  vec.end());
    });
    m_handlers = std::move(next);
}

void EventBus::publish(DeviceId const& id, DeviceEvent const& event) const {
    // Take a shared_ptr copy under shared lock, then release the lock
    // before invoking handlers. The shared_ptr keeps the vector alive
    // even if a concurrent writer swaps a new one in; handlers may
    // safely subscribe / unsubscribe during dispatch.
    std::shared_ptr<HandlerVec const> snapshot;
    {
        std::shared_lock const lock(m_handlersMutex);
        snapshot = m_handlers;
    }
    if (!snapshot) {
        return;
    }
    for (auto const& [token, handler] : *snapshot) {
        (void)token;
        handler(id, event);
    }
}

} // namespace ajazz::core
