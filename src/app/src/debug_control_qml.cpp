// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_control_qml.cpp
 * @brief QML introspection + driving + screenshot methods for the debug
 *        control channel -- the "interact with the UI" surface.
 *
 * Objects are addressed by their QML @c objectName (the Qt-standard runtime
 * handle; a QML @c id is compile-time only and not reachable at runtime).
 * Controls you want to drive must therefore set `objectName:` in QML. The
 * tree dump reports objectName + class for every node so the addressable
 * handles are discoverable.
 *
 * All handlers run on the GUI thread (the QLocalServer lives on the
 * Application's thread), so direct property access, method invocation, and
 * QQuickWindow::grabWindow() are all safe here.
 */
#include "debug_control_facade.hpp"
#include "debug_control_server.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QString>
#include <QVariant>

namespace ajazz::app {
namespace {

/// QObject children PLUS visual (QQuickItem) children, deduped.
///
/// Repeater / ListView / Loader create their delegates with a *visual*
/// parent but no QObject parent, so QObject::children() (and therefore
/// findChild) never sees them. Unioning childItems() makes the whole live
/// UI -- including every key cell and device row -- reachable.
QList<QObject*> allChildren(QObject* obj) {
    QList<QObject*> out = obj->children();
    if (auto* item = qobject_cast<QQuickItem*>(obj)) {
        for (QQuickItem* child : item->childItems()) {
            if (!out.contains(child)) {
                out.append(child);
            }
        }
    }
    return out;
}

/// Depth-limited JSON description of an object subtree (visual tree aware).
QJsonObject describe(QObject* obj, int depth) {
    auto const kids = allChildren(obj);
    QJsonObject node{
        {"objectName", obj->objectName()},
        {"class", QString::fromLatin1(obj->metaObject()->className())},
        {"childCount", static_cast<int>(kids.size())},
    };
    if (depth > 0 && !kids.isEmpty()) {
        QJsonArray children;
        for (QObject* child : kids) {
            children.append(describe(child, depth - 1));
        }
        node.insert("children", children);
    }
    return node;
}

/// Recursive objectName search across the QObject + visual child tree.
QObject* searchByName(QObject* obj, QString const& name, QSet<QObject*>& seen) {
    if (obj == nullptr || seen.contains(obj)) {
        return nullptr;
    }
    seen.insert(obj);
    if (obj->objectName() == name) {
        return obj;
    }
    for (QObject* child : allChildren(obj)) {
        if (QObject* hit = searchByName(child, name, seen)) {
            return hit;
        }
    }
    return nullptr;
}

/// Find the first object matching @p name across all root objects (a root
/// itself matches too). Returns nullptr if not found.
QObject* findByName(QQmlApplicationEngine& engine, QString const& name) {
    QSet<QObject*> seen;
    for (QObject* root : engine.rootObjects()) {
        if (QObject* hit = searchByName(root, name, seen)) {
            return hit;
        }
    }
    return nullptr;
}

/// First top-level QQuickWindow (the ApplicationWindow), or nullptr.
QQuickWindow* firstWindow(QQmlApplicationEngine& engine) {
    for (QObject* root : engine.rootObjects()) {
        if (auto* win = qobject_cast<QQuickWindow*>(root)) {
            return win;
        }
    }
    return nullptr;
}

} // namespace

void registerQmlControlMethods(DebugControlServer& server, QQmlApplicationEngine& engine) {
    // qml.tree {objectName?, depth?=3} -> object subtree (root forest if no name).
    server.registerMethod("qml.tree", [&engine](QJsonObject const& params, QString& err) {
        int const depth = params.value("depth").toInt(3);
        QString const name = params.value("objectName").toString();
        if (!name.isEmpty()) {
            QObject* obj = findByName(engine, name);
            if (obj == nullptr) {
                err = QStringLiteral("no object named '") + name + QStringLiteral("'");
                return QJsonObject{};
            }
            return QJsonObject{{"tree", describe(obj, depth)}};
        }
        QJsonArray roots;
        for (QObject* root : engine.rootObjects()) {
            roots.append(describe(root, depth));
        }
        return QJsonObject{{"roots", roots}};
    });

    // qml.get {objectName, property} -> value.
    server.registerMethod("qml.get", [&engine](QJsonObject const& params, QString& err) {
        QObject* obj = findByName(engine, params.value("objectName").toString());
        if (obj == nullptr) {
            err = QStringLiteral("object not found");
            return QJsonObject{};
        }
        QString const prop = params.value("property").toString();
        if (prop.isEmpty()) {
            err = QStringLiteral("missing 'property'");
            return QJsonObject{};
        }
        QVariant const value = obj->property(prop.toUtf8().constData());
        if (!value.isValid()) {
            err = QStringLiteral("no such property: ") + prop;
            return QJsonObject{};
        }
        return QJsonObject{{"property", prop}, {"value", QJsonValue::fromVariant(value)}};
    });

    // qml.set {objectName, property, value} -> applied value.
    server.registerMethod("qml.set", [&engine](QJsonObject const& params, QString& err) {
        QObject* obj = findByName(engine, params.value("objectName").toString());
        if (obj == nullptr) {
            err = QStringLiteral("object not found");
            return QJsonObject{};
        }
        QString const prop = params.value("property").toString();
        if (prop.isEmpty()) {
            err = QStringLiteral("missing 'property'");
            return QJsonObject{};
        }
        bool const ok =
            obj->setProperty(prop.toUtf8().constData(), params.value("value").toVariant());
        if (!ok) {
            // setProperty returns false for a dynamic property that did not
            // previously exist; treat a known meta-property as success.
            if (obj->metaObject()->indexOfProperty(prop.toUtf8().constData()) < 0) {
                err = QStringLiteral("no such property: ") + prop;
                return QJsonObject{};
            }
        }
        return QJsonObject{
            {"property", prop},
            {"value", QJsonValue::fromVariant(obj->property(prop.toUtf8().constData()))}};
    });

    // qml.invoke {objectName, method} -> invoked (zero-arg methods/signals).
    server.registerMethod("qml.invoke", [&engine](QJsonObject const& params, QString& err) {
        QObject* obj = findByName(engine, params.value("objectName").toString());
        if (obj == nullptr) {
            err = QStringLiteral("object not found");
            return QJsonObject{};
        }
        QString const method = params.value("method").toString();
        if (method.isEmpty()) {
            err = QStringLiteral("missing 'method'");
            return QJsonObject{};
        }
        bool const invoked =
            QMetaObject::invokeMethod(obj, method.toUtf8().constData(), Qt::DirectConnection);
        if (!invoked) {
            err = QStringLiteral("invoke failed (unknown or non-zero-arg method): ") + method;
            return QJsonObject{};
        }
        return QJsonObject{{"invoked", method}};
    });

    // qml.click {objectName} -> emits the control's clicked() signal.
    server.registerMethod("qml.click", [&engine](QJsonObject const& params, QString& err) {
        QObject* obj = findByName(engine, params.value("objectName").toString());
        if (obj == nullptr) {
            err = QStringLiteral("object not found");
            return QJsonObject{};
        }
        if (!QMetaObject::invokeMethod(obj, "clicked", Qt::DirectConnection)) {
            err = QStringLiteral("object has no clicked() signal/slot");
            return QJsonObject{};
        }
        return QJsonObject{{"clicked", obj->objectName()}};
    });

    // screenshot {path, objectName?} -> grabs the window to a PNG file so the
    // agent can SEE the rendered UI. With objectName, the window containing
    // that item is grabbed; otherwise the first ApplicationWindow.
    server.registerMethod("screenshot", [&engine](QJsonObject const& params, QString& err) {
        QString const path = params.value("path").toString();
        if (path.isEmpty()) {
            err = QStringLiteral("missing 'path' (absolute PNG output path)");
            return QJsonObject{};
        }
        QQuickWindow* window = nullptr;
        QString const name = params.value("objectName").toString();
        if (!name.isEmpty()) {
            QObject* obj = findByName(engine, name);
            if (obj == nullptr) {
                err = QStringLiteral("object not found: ") + name;
                return QJsonObject{};
            }
            window = qobject_cast<QQuickWindow*>(obj);
            if (window == nullptr) {
                // An Item: grab its owning window (whole-window capture).
                window = obj->property("window").value<QQuickWindow*>();
            }
        }
        if (window == nullptr) {
            window = firstWindow(engine);
        }
        if (window == nullptr) {
            err = QStringLiteral("no QQuickWindow available to capture");
            return QJsonObject{};
        }
        QImage const frame = window->grabWindow();
        if (frame.isNull()) {
            err = QStringLiteral("grabWindow returned a null image");
            return QJsonObject{};
        }
        if (!frame.save(path, "PNG")) {
            err = QStringLiteral("failed to write PNG to ") + path;
            return QJsonObject{};
        }
        return QJsonObject{{"path", path}, {"width", frame.width()}, {"height", frame.height()}};
    });

    // qml.drag {from, to, steps?=12} -> synthesizes a REAL pointer drag: a left-button
    // press at the source item's scene-centre, `steps` interpolated move events to the
    // target item's scene-centre, then a release. Delivered as QMouseEvents to the owning
    // QQuickWindow so the full delivery path (incl. PointerHandlers / DragHandler) runs —
    // unlike qml.invoke/qml.click which only emit signals (the Phase-28 trap). This is how
    // the Wayland drag-drop gesture is autonomously regression-tested.
    server.registerMethod("qml.drag", [&engine](QJsonObject const& params, QString& err) {
        QObject* from = findByName(engine, params.value("from").toString());
        QObject* to = findByName(engine, params.value("to").toString());
        if (from == nullptr || to == nullptr) {
            err = QStringLiteral("from/to object not found");
            return QJsonObject{};
        }
        auto* fromItem = qobject_cast<QQuickItem*>(from);
        auto* toItem = qobject_cast<QQuickItem*>(to);
        if (fromItem == nullptr || toItem == nullptr) {
            err = QStringLiteral("from/to is not a QQuickItem");
            return QJsonObject{};
        }
        QQuickWindow* win = fromItem->window();
        if (win == nullptr) {
            err = QStringLiteral("source item has no window");
            return QJsonObject{};
        }
        QPointF const p0 = fromItem->mapToScene(fromItem->boundingRect().center());
        QPointF const p1 = toItem->mapToScene(toItem->boundingRect().center());
        int steps = params.value("steps").toInt(12);
        if (steps < 2) {
            steps = 2;
        }
        auto deliver = [win](QEvent::Type t, QPointF wp, Qt::MouseButton b, Qt::MouseButtons bs) {
            QPointF const gp = win->mapToGlobal(wp);
            QMouseEvent ev(t, wp, wp, gp, b, bs, Qt::NoModifier);
            QCoreApplication::sendEvent(win, &ev);
        };
        deliver(QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= steps; ++i) {
            QPointF const wp = p0 + (p1 - p0) * (static_cast<double>(i) / steps);
            deliver(QEvent::MouseMove, wp, Qt::NoButton, Qt::LeftButton);
        }
        deliver(QEvent::MouseButtonRelease, p1, Qt::LeftButton, Qt::NoButton);
        return QJsonObject{{"from", from->objectName()},
                           {"to", to->objectName()},
                           {"steps", steps},
                           {"delivered", true}};
    });

    // input.pointer {action: press|move|release, x, y} -> posts a single QMouseEvent at
    // window-local (x,y) so a press->move->release drag can be scripted step-by-step. Same
    // real-event delivery as qml.drag; use when objectName centres are not the right targets.
    server.registerMethod("input.pointer", [&engine](QJsonObject const& params, QString& err) {
        QQuickWindow* win = firstWindow(engine);
        if (win == nullptr) {
            err = QStringLiteral("no QQuickWindow available");
            return QJsonObject{};
        }
        QString const action = params.value("action").toString();
        QPointF const wp(params.value("x").toDouble(), params.value("y").toDouble());
        QEvent::Type t{};
        Qt::MouseButton b{};
        Qt::MouseButtons bs{};
        if (action == QStringLiteral("press")) {
            t = QEvent::MouseButtonPress;
            b = Qt::LeftButton;
            bs = Qt::LeftButton;
        } else if (action == QStringLiteral("release")) {
            t = QEvent::MouseButtonRelease;
            b = Qt::LeftButton;
            bs = Qt::NoButton;
        } else if (action == QStringLiteral("move")) {
            t = QEvent::MouseMove;
            b = Qt::NoButton;
            bs = Qt::LeftButton;
        } else {
            err = QStringLiteral("action must be press|move|release");
            return QJsonObject{};
        }
        QPointF const gp = win->mapToGlobal(wp);
        QMouseEvent ev(t, wp, wp, gp, b, bs, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &ev);
        return QJsonObject{{"action", action}, {"x", wp.x()}, {"y", wp.y()}, {"delivered", true}};
    });
}

} // namespace ajazz::app
