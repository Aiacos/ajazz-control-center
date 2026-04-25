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

EventBus::EventBus() {
    m_handlers.store(std::make_shared<HandlerVec const>(), std::memory_order_release);
}

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
    std::lock_guard const lock(m_writeMutex);
    auto const current = m_handlers.load(std::memory_order_acquire);
    auto next =
        cowCopy(current, [&](HandlerVec& vec) { vec.emplace_back(token, std::move(handler)); });
    m_handlers.store(std::move(next), std::memory_order_release);
    return token;
}

void EventBus::unsubscribe(Subscription token) noexcept {
    std::lock_guard const lock(m_writeMutex);
    auto const current = m_handlers.load(std::memory_order_acquire);
    // Fast path: if the token isn't present we don't allocate a new
    // vector. Saves an allocation on the (common) case where a caller
    // unsubscribes twice.
    auto const found = std::any_of(
        current->begin(), current->end(), [token](auto const& kv) { return kv.first == token; });
    if (!found) {
        return;
    }
    auto next = cowCopy(current, [token](HandlerVec& vec) {
        vec.erase(std::remove_if(vec.begin(),
                                 vec.end(),
                                 [token](auto const& kv) { return kv.first == token; }),
                  vec.end());
    });
    m_handlers.store(std::move(next), std::memory_order_release);
}

void EventBus::publish(DeviceId const& id, DeviceEvent const& event) const {
    // Atomic-load: lock-free; the returned shared_ptr keeps the vector
    // alive even if a concurrent writer swaps a new one in immediately
    // after this load.
    auto const snapshot = m_handlers.load(std::memory_order_acquire);
    if (!snapshot) {
        return;
    }
    for (auto const& [token, handler] : *snapshot) {
        (void)token;
        handler(id, event);
    }
}

} // namespace ajazz::core
