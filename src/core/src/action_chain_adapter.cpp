// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file action_chain_adapter.cpp
 * @brief instanceChildrenToChain -- ActionInstance.children -> ActionChain.
 *
 * Pure-core implementation of the Multi Action dispatch seam (Phase 32,
 * BIND-04/BIND-05). COD-031: no JSON library, no Qt -- standard library only.
 */
#include "ajazz/core/action_chain_adapter.hpp"

namespace ajazz::core {
namespace {

/// Maximum recursion depth for the children flatten. Mirrors the profile
/// reader's DepthGuard cap (CR-01, kMaxDepth=64): deep enough for any
/// legitimate Multi Action nesting, shallow enough that the C++ call stack can
/// never overflow before the cap fires. Children beyond this depth are silently
/// truncated (T-32-01 DoS mitigation) -- the adapter operates on already-parsed
/// data, so unlike the reader it cannot reject; it caps instead.
constexpr int kMaxDepth = 64;

/// Append the pre-order flattening of @p parent.children into @p out. Each child
/// is emitted as a Plugin Action carrying its id / settings / delayMs, then its
/// own children recurse inline. @p depth bounds the recursion at @ref kMaxDepth.
void flatten(ActionInstance const& parent, ActionChain& out, int depth) {
    if (depth >= kMaxDepth) {
        return; // T-32-01: truncate a hostile deep nest rather than overflow.
    }
    for (auto const& child : parent.children) {
        out.push_back(Action{
            /*kind=*/ActionKind::Plugin,
            /*id=*/child.id,
            /*settingsJson=*/child.settings,
            /*label=*/std::string{},
            /*delayMs=*/child.delayMs,
        });
        if (!child.children.empty()) {
            flatten(child, out, depth + 1); // depth-first inline recurse.
        }
    }
}

} // namespace

ActionChain instanceChildrenToChain(ActionInstance const& parent) {
    ActionChain chain;
    flatten(parent, chain, /*depth=*/0);
    return chain;
}

} // namespace ajazz::core
