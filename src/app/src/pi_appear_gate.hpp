// SPDX-License-Identifier: GPL-3.0-or-later
//
// PiAppearGate -- de-duplicates propertyInspectorDidAppear / …DidDisappear.
//
// The embedded OpenDeck SPA's Property Inspector registers its OWN WebSocket
// connection — surfaced as SdPluginServer::propertyInspectorRegistered — which
// funnels through Application, which sends the plugin propertyInspectorDidAppear.
// A registration can fire more than once per visible PI; without a gate the host
// would emit the event more than once per open.
//
// The Elgato contract is one appear per visible PI, keyed on the action-instance
// `context`. This gate is the single source of truth the emit site consults:
// the first appear for a context wins; duplicates are suppressed until a matching
// disappear resets it. It is a plain value type (no Qt object, no signals) so it
// is trivially unit-testable in isolation.
#pragma once

#include <QSet>
#include <QString>

namespace ajazz::app {

class PiAppearGate {
public:
    /// Record that the PI for @p context became visible.
    /// @return true if the caller SHOULD emit propertyInspectorDidAppear (this is
    ///         the first appearance for @p context); false if it is a duplicate
    ///         (a second emit site firing for the same open) or @p context is
    ///         empty (an unresolved context — nothing to notify).
    [[nodiscard]] bool noteAppear(QString const& context) {
        if (context.isEmpty() || m_appeared.contains(context)) {
            return false;
        }
        m_appeared.insert(context);
        return true;
    }

    /// Record that the PI for @p context was hidden / torn down.
    /// @return true if the caller SHOULD emit propertyInspectorDidDisappear (the
    ///         context was previously appeared); false if it never appeared (a
    ///         dangling disappear — e.g. the second emit site closing) so the
    ///         host does not emit an unpaired DidDisappear.
    [[nodiscard]] bool noteDisappear(QString const& context) { return m_appeared.remove(context); }

    /// Whether @p context is currently considered appeared. Test/inspection only.
    [[nodiscard]] bool isAppeared(QString const& context) const {
        return m_appeared.contains(context);
    }

private:
    QSet<QString> m_appeared;
};

} // namespace ajazz::app
