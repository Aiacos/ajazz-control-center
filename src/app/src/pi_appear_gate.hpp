// SPDX-License-Identifier: GPL-3.0-or-later
//
// PiAppearGate -- de-duplicates propertyInspectorDidAppear / …DidDisappear.
//
// A *modern* Property Inspector both (a) opens a WebEngine page — surfaced as
// PropertyInspectorController::inspectorOpened — AND (b) registers its OWN
// WebSocket connection — surfaced as SdPluginServer::propertyInspectorRegistered.
// Both paths funnel through Application, which sends the plugin
// propertyInspectorDidAppear. Without a gate the host emits the event TWICE per
// open (research D1: "application.cpp:736 + :948").
//
// The Elgato contract is one appear per visible PI, keyed on the action-instance
// `context`. This gate is the single source of truth both emit sites consult:
// the first appear for a context wins; duplicates are suppressed until a matching
// disappear resets it. It is a plain value type (no Qt object, no signals) so it
// is trivially unit-testable in isolation (test_pi_bridge.cpp), unlike the
// Application wiring it de-tangles.
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
