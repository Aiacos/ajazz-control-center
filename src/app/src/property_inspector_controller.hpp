// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file property_inspector_controller.hpp
 * @brief Controller that hosts plugin-authored Property Inspector HTML pages.
 *
 * The Property Inspector ("PI") is the per-action settings panel a plugin
 * author ships as HTML + JS, modelled on the Stream Deck SDK so that
 * existing Stream Deck / OpenDeck plugins keep their PI assets untouched.
 *
 * This controller owns the Qt WebEngine plumbing — one isolated
 * @c QWebEngineProfile per plugin UUID, a @c QWebChannel that exposes a
 * @c \$SD bridge object to the page's JS, and a URL request interceptor
 * scoped to the PI directory. It is exposed to QML as the
 * @c propertyInspectorController context property and consumed by
 * @c PropertyInspector.qml.
 *
 * Build gating: the actual WebEngine code paths only compile when
 * @c AJAZZ_HAVE_WEBENGINE is defined (driven by a successful
 * @c find_package(Qt6 ... WebEngineQuick) probe in CMake). When the macro is
 * absent every method becomes a no-op so the rest of the application links
 * unchanged on minimal Qt installs that don't ship Qt WebEngine.
 *
 * @see docs/architecture/PLUGIN-SDK.md (the over-the-wire protocol the
 *      bridge eventually forwards messages to)
 * @see TODO.md "Property Inspector embedding" — milestone tracking.
 */
#pragma once

#include <QObject>
#include <QString>

namespace ajazz::app {

/**
 * @class PropertyInspectorController
 * @brief Lifecycle owner of the Property Inspector WebEngine surface.
 *
 * M1 status: stub. The controller compiles and is wired into
 * @ref Application so QML bindings to @c propertyInspectorController don't
 * break, but every public slot is currently a no-op. Subsequent milestones
 * (see TODO.md) implement: M2 minimal HTML load, M3 QWebChannel bridge,
 * M4 settings persistence, M5 plugin-host WebSocket bridge.
 */
class PropertyInspectorController : public QObject {
    Q_OBJECT

    /// True iff the build linked Qt WebEngine and the controller can host
    /// HTML PI pages. QML uses this to swap between the WebEngine renderer
    /// and the schema-driven native fallback.
    Q_PROPERTY(bool webEngineAvailable READ webEngineAvailable CONSTANT)

    /// True while a PI HTML page is loaded and visible. Cleared by
    /// @ref closeInspector.
    Q_PROPERTY(bool hasHtmlInspector READ hasHtmlInspector NOTIFY hasHtmlInspectorChanged)

public:
    explicit PropertyInspectorController(QObject* parent = nullptr);
    ~PropertyInspectorController() override;

    [[nodiscard]] bool webEngineAvailable() const noexcept;
    [[nodiscard]] bool hasHtmlInspector() const noexcept { return hasHtmlInspector_; }

    /**
     * @brief Load the Property Inspector for an action context.
     *
     * @param pluginUuid    The plugin's UUID (manifest @c UUID); used to scope
     *                      the WebEngineProfile so plugins cannot read each
     *                      other's cookies / storage.
     * @param htmlAbsPath   Absolute filesystem path to the PI HTML entry
     *                      (resolved by the manifest reader from
     *                      @c PropertyInspectorPath, action-level override
     *                      preferred over the top-level one).
     * @param actionUuid    The action's UUID (manifest @c Actions[].UUID).
     * @param contextUuid   The per-key/per-encoder context UUID minted by
     *                      the host so settings persistence is keyed
     *                      uniquely.
     */
    Q_INVOKABLE void loadInspector(QString const& pluginUuid,
                                   QString const& htmlAbsPath,
                                   QString const& actionUuid,
                                   QString const& contextUuid);

    /// Tear down the active PI page (selection cleared / inspector closed).
    Q_INVOKABLE void closeInspector();

signals:
    void hasHtmlInspectorChanged();

private:
    bool hasHtmlInspector_ = false;
};

} // namespace ajazz::app
