// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/builtin_action_registry.hpp"

namespace ajazz::core {

void BuiltinActionRegistry::registerAction(std::string uuid, BuiltinHandler handler) {
    m_handlers.insert_or_assign(std::move(uuid), std::move(handler));
}

bool BuiltinActionRegistry::handles(std::string_view id) const {
    if (!id.starts_with(kBuiltinPrefix)) {
        return false;
    }
    return m_handlers.contains(std::string{id});
}

void BuiltinActionRegistry::dispatch(std::string_view id, std::string_view settingsJson) const {
    auto const it = m_handlers.find(std::string{id});
    if (it == m_handlers.end()) {
        return; // clean no-op for unregistered UUIDs
    }
    it->second(settingsJson);
}

} // namespace ajazz::core
