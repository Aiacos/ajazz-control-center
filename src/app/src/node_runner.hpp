// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file node_runner.hpp
 * @brief Node.js spawn primitives: argv builder + injectable node >= 20 resolver.
 *
 * Provides two pure/injectable functions for the Wave-3 PluginManager:
 *
 *   - `buildNodeArgv()` — emits the exact CLI token list from akp_plugin_sdk.md §3:
 *     `<codePath> -port <p> -pluginUUID <u> -registerEvent registerPlugin -info <info>`.
 *     `codePath` is `argv[0]`; the node binary is NOT in the list (Pitfall 3).
 *     The caller uses: `QProcess::start(nodeExe, buildNodeArgv(...))`.
 *
 *   - `resolveNode20Plus(NodeProbe)` — detects a system `node` >= 20 via an injectable
 *     probe (find + version-query functors). Returns `std::nullopt` when node is absent
 *     OR the reported version is below 20. Never bundles node — anti-feature (CONTEXT.md).
 *
 *   - `makeDefaultNodeProbe()` — wires the real `QStandardPaths::findExecutable` +
 *     a `QProcess --version` query so the manager (18-04) gets a ready-to-use probe.
 *     Tests construct a fake `NodeProbe` directly to inject controlled outputs.
 *
 * COD-031: `src/app/src/` — Qt-Core only, no nlohmann::json.
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-02 (PLUGIN-08)
 */
#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace ajazz::app {

/**
 * @brief Injectable probe for node discovery and version querying.
 *
 * Both members are `std::function` so callers can inject fakes:
 *
 *   - Tests: pass lambdas returning controlled strings.
 *   - Production: use `makeDefaultNodeProbe()` which wires the real OS APIs.
 *
 * The DEFAULT functors are NOT set by the struct — callers must either assign
 * them explicitly or use `makeDefaultNodeProbe()`.  This keeps the struct
 * itself free of Qt runtime dependencies in header context.
 */
struct NodeProbe {
    /// Return the absolute path to the `node` executable, or an empty QString if absent.
    std::function<QString()> findNode;

    /// Run `<nodeExe> --version` and return the raw stdout (e.g. `"v26.0.0\n"`).
    std::function<QString(QString const&)> queryVersion;
};

/**
 * @brief Create a `NodeProbe` wired to the real OS / `QProcess` implementations.
 *
 * - `findNode`     = `QStandardPaths::findExecutable("node")` (empty if not on PATH).
 * - `queryVersion` = launches `<node> --version` via `QProcess` (blocking, 5 s timeout)
 *                    and returns the trimmed stdout string.
 *
 * The manager (18-04) calls this once at startup and passes the probe to
 * `resolveNode20Plus()`.  Tests build a fake `NodeProbe` directly.
 */
[[nodiscard]] NodeProbe makeDefaultNodeProbe();

/**
 * @brief Build the node spawn argv list per akp_plugin_sdk.md §3 step 3.
 *
 * Returns exactly nine tokens:
 * ```
 * { codePath, "-port", QString::number(port), "-pluginUUID", pluginUuid,
 *   "-registerEvent", "registerPlugin", "-info", infoJson }
 * ```
 *
 * @note `codePath` is `argv[0]` — the **script path**, NOT the node binary.
 *       Use `QProcess::start(nodeExe, buildNodeArgv(...))` where `nodeExe` is
 *       the path returned by `resolveNode20Plus()`.
 *
 * @param codePath   Path to the plugin's entry-point script (the Elgato `CodePath`).
 * @param port       WebSocket server port minted by `SdPluginServer`.
 * @param pluginUuid Plugin UUID (reverse-DNS), e.g. `"com.example.myplugin"`.
 * @param infoJson   JSON-encoded application-info envelope (`-info` argument).
 * @return           Nine-element `QStringList` ready for `QProcess::start`.
 */
[[nodiscard]] QStringList buildNodeArgv(QString const& codePath,
                                        quint16 port,
                                        QString const& pluginUuid,
                                        QString const& infoJson);

/**
 * @brief Detect a system Node.js >= 20 via an injectable probe.
 *
 * Algorithm:
 *   1. Call `probe.findNode()`. If empty -> `std::nullopt` (node absent).
 *   2. Call `probe.queryVersion(exe)` to get the raw version string (e.g. `"v26.0.0\n"`).
 *   3. Strip a leading `'v'` and trim whitespace.
 *   4. Parse with `QVersionNumber::fromString`.
 *   5. If `majorVersion() < 20` -> `std::nullopt` (too old).
 *   6. Otherwise return the exe path.
 *
 * `resolveNode20Plus` itself never calls `QProcess` — it delegates to `probe.queryVersion`.
 * This keeps the function pure for testing purposes.
 *
 * @param probe   Injectable finder + version-query. Use `makeDefaultNodeProbe()` in production.
 * @return        Absolute path to an acceptable node executable, or `std::nullopt`.
 */
[[nodiscard]] std::optional<QString> resolveNode20Plus(NodeProbe const& probe);

} // namespace ajazz::app
