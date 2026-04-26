// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file out_of_process_plugin_host.hpp
 * @brief Subprocess-based plugin host (audit finding A4 — slice 1).
 *
 * Runs the embedded Python interpreter and the user's plugins in a
 * **child process** instead of in-process via `pybind11::scoped_interpreter`.
 * A segfault in any C extension a plugin imports (numpy / opencv / mido)
 * now kills only the child — the host detects EOF on the child's stdout
 * and surfaces an error from the next operation, instead of taking the
 * whole AJAZZ Control Center process down.
 *
 * Slice 1 (this PR) ships only the IPC foundation:
 *   * spawn the child (POSIX `fork()` + `execvp()` of the system `python3`),
 *   * read the `{"event":"ready"}` handshake,
 *   * implement `list_plugins` (returns the plugins the child has loaded —
 *     empty in slice 1 because `load_all` is not wired yet),
 *   * implement graceful `shutdown` with timeout + SIGKILL fallback,
 *   * surface child crash / exit as an error from the in-flight call.
 *
 * Slice 2 (follow-up PR) adds `add_search_path`, `load_all` and
 * `dispatch`, then migrates @ref ajazz::plugins::PluginHost callers to
 * this class. Slice 3 adds OS-specific sandboxing (`bwrap` on Linux,
 * `sandbox-exec` on macOS, AppContainer on Windows).
 *
 * This class lives alongside the legacy in-process @ref PluginHost
 * during the migration. Both implement the same external contract; once
 * `OutOfProcessPluginHost` covers every use site, the in-process host
 * is deleted.
 *
 * @note POSIX-only in slice 1. Windows port goes through `_spawnvp` +
 *       anonymous pipes and lives in a follow-up PR; the header guards
 *       on `#ifndef _WIN32` so the rest of the build keeps working on
 *       Windows during the slice 1 → slice 2 window.
 */
#pragma once

#ifndef _WIN32

#include "ajazz/plugins/plugin_host.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ajazz::plugins {

/**
 * @brief Configuration for spawning the plugin-host child process.
 *
 * Defaults are sensible for the in-tree layout: the child script is
 * resolved relative to the repository's `python/ajazz_plugins/`
 * directory, and `pythonExecutable` defaults to the first `python3`
 * on `PATH`. Tests override both to point at a fake child for
 * deterministic IPC fixtures.
 */
struct OutOfProcessHostConfig {
    /// Absolute path to the Python interpreter the child runs under.
    /// Defaults to `python3` resolved via `PATH`.
    std::string pythonExecutable;

    /// Absolute path to the child script. Slice 1 uses the script
    /// shipped in `python/ajazz_plugins/_host_child.py`.
    std::filesystem::path childScript;

    /// Directories to add to the child's `PYTHONPATH`. The directory
    /// containing `ajazz_plugins` (the SDK package) MUST be one of
    /// them so the child can `import ajazz_plugins`.
    std::vector<std::filesystem::path> pythonPath;

    /// Maximum time to wait for the `{"event":"ready"}` handshake
    /// after spawning the child. Defaults to 5 s — Python startup is
    /// usually <500 ms, but cold caches / virus scanners on Windows
    /// (when the port lands) can stretch this out.
    std::chrono::milliseconds readyTimeout{5000};

    /// Maximum time to wait for the child to exit cleanly after a
    /// `shutdown` op. Defaults to 2 s; if the child has not exited
    /// by then, `shutdown()` sends SIGKILL.
    std::chrono::milliseconds shutdownTimeout{2000};
};

/**
 * @brief Subprocess-isolated plugin host.
 *
 * Same external contract as @ref PluginHost, but every plugin call
 * runs in a child process. The class is non-copyable and non-movable;
 * the destructor sends SIGKILL if the child is still running.
 *
 * Thread-safety: the constructor and destructor are NOT thread-safe
 * with each other (typical RAII contract). The other methods take an
 * internal mutex and may be called concurrently, but they serialise
 * on the IPC channel — calls do not pipeline.
 */
class OutOfProcessPluginHost {
public:
    /// Spawn the child and run the ready handshake. Throws
    /// `std::runtime_error` if the child fails to spawn, dies before
    /// emitting `ready`, or does not emit `ready` within
    /// `config.readyTimeout`.
    explicit OutOfProcessPluginHost(OutOfProcessHostConfig config);

    /// Send `shutdown` to the child, wait up to
    /// `config.shutdownTimeout` for clean exit, then SIGKILL if still
    /// alive. Always succeeds (best-effort cleanup).
    ~OutOfProcessPluginHost();

    OutOfProcessPluginHost(OutOfProcessPluginHost const&) = delete;
    OutOfProcessPluginHost& operator=(OutOfProcessPluginHost const&) = delete;
    OutOfProcessPluginHost(OutOfProcessPluginHost&&) = delete;
    OutOfProcessPluginHost& operator=(OutOfProcessPluginHost&&) = delete;

    /// True if the child process is still running. False after a
    /// `shutdown` round-trip, or if the child crashed / exited on its
    /// own (e.g. unhandled exception, segfault in a plugin C
    /// extension). Methods that would hit the dead child return
    /// errors instead of blocking.
    [[nodiscard]] bool isAlive() const noexcept;

    /// PID of the child process, for diagnostics. -1 once the child
    /// has been reaped.
    [[nodiscard]] int childPid() const noexcept;

    /// Ask the child for its plugin inventory. Returns an empty vector
    /// in slice 1 (because `load_all` is not implemented yet). In
    /// slice 2 this returns one entry per loaded plugin, mirroring
    /// the in-process @ref PluginHost::plugins().
    ///
    /// Throws `std::runtime_error` if the child has died or the IPC
    /// times out.
    [[nodiscard]] std::vector<PluginInfo> listPlugins();

    /// Trigger a deliberate crash inside the child. **Test-only entry
    /// point**: shipped in slice 1 to prove the crash-isolation
    /// claim — calling this kills the child but the host remains
    /// usable (the next `listPlugins()` returns an error, instead
    /// of the whole AJAZZ Control Center process going down).
    ///
    /// Returns `true` if the child has been observed dead after the
    /// crash op. `false` would indicate the child swallowed the
    /// signal — that is itself a test failure.
    bool crashChildForTest();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ajazz::plugins

#endif // !_WIN32
