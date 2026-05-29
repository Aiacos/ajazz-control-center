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

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaMethod>
#include <QMetaObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QString>
#include <QVariant>

namespace ajazz::app {
namespace {

/// Depth-limited JSON description of an object subtree.
QJsonObject describe(QObject const* obj, int depth) {
    QJsonObject node{
        {"objectName", obj->objectName()},
        {"class", QString::fromLatin1(obj->metaObject()->className())},
    };
    auto const& kids = obj->children();
    node.insert("childCount", static_cast<int>(kids.size()));
    if (depth > 0 && !kids.isEmpty()) {
        QJsonArray children;
        for (QObject const* child : kids) {
            children.append(describe(child, depth - 1));
        }
        node.insert("children", children);
    }
    return node;
}

/// Find the first object matching @p name across all root objects (a root
/// itself matches too). Returns nullptr if not found.
QObject* findByName(QQmlApplicationEngine& engine, QString const& name) {
    for (QObject* root : engine.rootObjects()) {
        if (root->objectName() == name) {
            return root;
        }
        if (auto* hit = root->findChild<QObject*>(name)) {
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
        for (QObject const* root : engine.rootObjects()) {
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
}

} // namespace ajazz::app
