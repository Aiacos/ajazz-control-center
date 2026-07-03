// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_control_facade.hpp
 * @brief Registers the application's control/observation methods onto a
 *        @ref DebugControlServer.
 *
 * Kept separate from the transport so the JSON-RPC plumbing stays generic
 * and the surface (which subsystems are reachable, with what parameters)
 * lives in one auditable place. The facade holds only a non-owning reference
 * to @ref Application and reaches subsystems through its accessors.
 *
 * Phase 2a wires the observation surface (log tail/level, system state,
 * device inventory). Device/profile/plugin/action *control* and the QML
 * driving + screenshot surface are layered on in later commits.
 */
#pragma once

class QQmlApplicationEngine;

namespace ajazz::app {

class DebugControlServer;
class Application;

/// Register the log / state / device / input / profile / plugin / action
/// methods onto @p server, bound to @p app. Call once, after
/// Application::bootstrap() (the log ring must exist).
void registerDebugControlMethods(DebugControlServer& server, Application& app);

/// Register the QML introspection + driving + screenshot methods
/// (qml.tree / qml.get / qml.set / qml.invoke / qml.click / screenshot)
/// onto @p server, bound to @p engine. Call after the QML root has loaded.
/// Handlers run on the GUI thread (the control server's event loop), so it
/// is safe for them to touch QML objects and grab the window.
void registerQmlControlMethods(DebugControlServer& server, QQmlApplicationEngine& engine);

} // namespace ajazz::app
