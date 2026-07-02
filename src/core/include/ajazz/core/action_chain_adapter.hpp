// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file action_chain_adapter.hpp
 * @brief Multi Action bridge: ActionInstance.children -> core::ActionChain.
 *
 * Phase 31 delivered the @ref ajazz::core::ActionInstance model only. This
 * adapter is the missing dispatch seam (Phase 32, BIND-04/BIND-05): it converts
 * the typed `ActionInstance.children` tree into the flat @ref
 * ajazz::core::ActionChain that @ref ajazz::core::ActionEngine already walks
 * sequentially, carrying each child's per-step @ref ActionInstance::delayMs onto
 * the generated @ref ajazz::core::Action so the engine defers it between
 * siblings. No new async runner is introduced -- the existing
 * ActionEngine::run sequential walk is reused.
 *
 * This header is an installed PUBLIC core header. Per the COD-031 boundary it is
 * intentionally nlohmann-free and Qt-free: only the C++ standard library and the
 * core model headers are used.
 *
 * @see ajazz::core::ActionInstance, ajazz::core::ActionChain, ActionEngine::run
 */
#pragma once

#include "ajazz/core/action_engine.hpp"   // ActionChain alias.
#include "ajazz/core/action_instance.hpp" // ActionInstance.

namespace ajazz::core {

/**
 * @brief Flatten a Multi Action instance's children into an ActionChain.
 *
 * Each child becomes one @ref Action step with `kind == ActionKind::Plugin`,
 * copying the child's `id` -> `Action::id`, `settings` -> `Action::settingsJson`
 * and `delayMs` -> `Action::delayMs`. Nested children recurse inline depth-first
 * (a child's own children are appended immediately after that child), so the
 * resulting chain is a pre-order flattening of the children tree.
 *
 * Recursion is bounded by an internal depth cap (mirroring the profile reader's
 * CR-01 DepthGuard, kMaxDepth=64): a pathologically deep `children` nest is
 * truncated at the cap rather than overflowing the call stack (T-32-01). The
 * top-level `parent` node itself is NOT emitted -- only its descendants.
 *
 * @param parent The Multi Action instance whose children drive the chain.
 * @return An ordered, delay-carrying ActionChain (empty when there are no
 *         children).
 */
[[nodiscard]] ActionChain instanceChildrenToChain(ActionInstance const& parent);

} // namespace ajazz::core
