// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file windows_app_container_sandbox.hpp
 * @brief Windows AppContainer + restricted-token sandbox
 *        (audit finding A4 — slice 3d-ii).
 *
 * Windows counterpart of @ref LinuxBwrapSandbox /
 * @ref MacosSandboxExecSandbox. Isolates the @ref OutOfProcessPluginHost
 * child inside an AppContainer — the same OS primitive UWP apps and
 * WinRT processes use — and applies a restricted access token so the
 * child runs with a drastically reduced capability set compared to
 * the host.
 *
 * AppContainer on Windows is fundamentally different from `bwrap` /
 * `sandbox-exec`: it is NOT configured via a wrapper executable. A
 * parent creates the child with `CreateProcessW` and passes a
 * `STARTUPINFOEX` whose `lpAttributeList` carries a
 * `SECURITY_CAPABILITIES` blob (container SID + allowed capability
 * SIDs). The Windows kernel isolates the child based on that blob.
 *
 * To fit this into the OS-agnostic @ref Sandbox interface we:
 *
 *   - leave @ref decorate as a passthrough (AppContainer does not
 *     need a wrapper);
 *   - override the new @ref configureProcessAttributes hook, which
 *     populates the opaque @ref ProcessAttributes pimpl with the
 *     container SID, capability SIDs and a restricted token handle;
 *   - the win32 host backend reads the pimpl via the
 *     `windowsProcessAttributes` helper (defined in @c sandbox.cpp)
 *     and feeds the blob to `UpdateProcThreadAttribute`.
 *
 * @par Default profile (empty permissions set)
 *
 * The child runs under a freshly-minted AppContainer SID derived
 * from the per-plugin profile name. Capabilities granted by default:
 * none. The restricted token drops every SID that is not mandatory,
 * mirroring the closed posture of bwrap's `--unshare-*` set.
 *
 * @par Permission-driven relaxations
 *
 * Mirrors the Linux / macOS rules so plugin authors see one
 * consistent permission model across OSes:
 *
 *   - `obs-websocket`, `spotify`, `discord-rpc` (any of) ⇒ grant
 *     the `internetClient` AppContainer capability (SID well-known
 *     as `SECURITY_CAPABILITY_INTERNET_CLIENT`). Without this the
 *     child cannot open any outbound TCP/UDP socket.
 *   - `notifications`, `media-control`, `system-power` (any of) ⇒
 *     grant the `internetClient` capability AND add the
 *     `UserNotificationListener` capability (SID
 *     `SECURITY_CAPABILITY_INTERNET_CLIENT_SERVER`) so the child
 *     can talk to the Windows notification broker / MediaRemote
 *     equivalents. (Windows lumps several cross-process broker
 *     surfaces under this coarse cap; a future slice narrows this
 *     to per-broker named capabilities once we have measurements.)
 *
 * @par AppContainer availability
 *
 * The AppContainer subsystem has been in Windows since 8.0 (Desktop
 * Bridge shipped with 10.0.10240). The sandbox checks runtime
 * availability of `CreateAppContainerProfile` via a delay-loaded
 * symbol lookup; if the host kernel is too old or the API is
 * disabled by policy the sandbox falls back to passthrough —
 * equivalent to @ref NoOpSandbox — so the plugin host keeps working
 * on unusual Windows builds. @ref hasAppContainer exposes the real
 * state for diagnostics / UI.
 *
 * @par What this slice does NOT cover
 *
 * - Per-plugin named AppContainer profiles on disk. The current
 *   design produces an ephemeral SID-only container; Windows can
 *   alternatively register the profile with a friendly name via
 *   `CreateAppContainerProfile`, which persists across host
 *   launches and lets the user inspect / revoke capabilities via
 *   the standard UAC dialogs. A future slice adds the named-profile
 *   path once we have a UX for revoke.
 * - Job Object wrapping. An outer Job Object would let the host
 *   enforce memory / CPU quotas and kill-on-parent-exit. The
 *   interaction with AppContainer is straightforward (both can be
 *   applied to the same process) but we leave it to a future slice
 *   so the minimum-viable slice stays reviewable.
 * - Low Integrity Level + WRITE_RESTRICTED. AppContainer integrity
 *   is already low by default, so we do not override it here; that
 *   matches the bwrap / sandbox-exec default posture.
 */
#pragma once

#ifdef _WIN32

#include "ajazz/plugins/sandbox.hpp"

#include <filesystem>
#include <set>
#include <string>

namespace ajazz::plugins {

/**
 * @brief Windows-specific sandbox using AppContainer + restricted token.
 *
 * Construct once at app boot with the union of permissions the host
 * is willing to grant the child (same contract as @ref LinuxBwrapSandbox),
 * then move the resulting @c std::unique_ptr<Sandbox> into
 * @c OutOfProcessHostConfig::sandbox.
 *
 * The constructor probes for AppContainer API availability at
 * runtime (via `LoadLibraryW("userenv.dll")` + `GetProcAddress`) so
 * the static link surface stays minimal and the class still compiles
 * against the bare Windows SDK without importing the userenv import
 * library.
 */
class WindowsAppContainerSandbox final : public Sandbox {
public:
    /**
     * @brief Construct an AppContainer sandbox for the given permission set.
     *
     * @param grantedPermissions Strings from the
     *        @c Ajazz.Permissions enum that the host is willing to
     *        grant the child. Unknown strings are silently ignored —
     *        the schema validator catches them at manifest time.
     * @param containerName Optional human-readable container name.
     *        Used only as a diagnostic label; AppContainer SIDs are
     *        derived deterministically from this string via
     *        `DeriveAppContainerSidFromAppContainerName`. Empty
     *        (default) produces a sensible fixed label —
     *        `"ajazz-control-center.plugin-host"` — so identical
     *        hosts across machines produce identical SIDs, which in
     *        turn makes capability-grant ACLs portable.
     */
    explicit WindowsAppContainerSandbox(std::set<std::string> grantedPermissions,
                                        std::string containerName = {});

    /// True if the AppContainer API was located at runtime. False
    /// puts the sandbox into passthrough mode — @ref decorate returns
    /// the plain argv and @ref configureProcessAttributes is a no-op,
    /// leaving the host to `CreateProcessW` normally.
    [[nodiscard]] bool hasAppContainer() const noexcept { return m_hasAppContainer; }

    /// Human-readable container name. Stable across calls; derived
    /// from the constructor argument or the default label.
    [[nodiscard]] std::string const& containerName() const noexcept { return m_containerName; }

    /// Granted-permission set, immutable after construction. Kept
    /// for diagnostics + tests.
    [[nodiscard]] std::set<std::string> const& grantedPermissions() const noexcept {
        return m_grantedPermissions;
    }

    /// AppContainer isolation is configured at `CreateProcessW` time,
    /// not via a wrapper executable — `decorate` simply returns the
    /// plain `(pythonExe, [pythonExe, scriptPath])` pair. The
    /// capability list moves through @ref configureProcessAttributes
    /// instead.
    [[nodiscard]] DecoratedSpawn decorate(std::string const& pythonExe,
                                          std::filesystem::path const& scriptPath) const override;

    /// Populate @p attributes with the AppContainer SID, capability
    /// SIDs, and a restricted token handle. No-op if the sandbox is
    /// in passthrough mode (`hasAppContainer() == false`).
    void configureProcessAttributes(ProcessAttributes& attributes) const override;

private:
    std::set<std::string> m_grantedPermissions;
    std::string m_containerName;
    bool m_hasAppContainer{false};
};

} // namespace ajazz::plugins

#endif // _WIN32
