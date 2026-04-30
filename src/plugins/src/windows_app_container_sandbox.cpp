// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file windows_app_container_sandbox.cpp
 * @brief Implementation of @ref WindowsAppContainerSandbox (audit
 *        finding A4 — slice 3d-ii).
 *
 * The Windows AppContainer API is split across several DLLs:
 *
 *   - `userenv.dll` for @c CreateAppContainerProfile /
 *     @c DeriveAppContainerSidFromAppContainerName.
 *   - `advapi32.dll` for @c CreateRestrictedToken, @c GetTokenInformation,
 *     @c OpenProcessToken, @c AllocateAndInitializeSid, @c FreeSid.
 *
 * We delay-load @c userenv.dll via @c LoadLibraryW / @c GetProcAddress
 * so the import surface stays small and the class gracefully degrades
 * to passthrough on Windows builds where AppContainer has been
 * disabled by group policy. @c advapi32 is always available (it ships
 * with every Windows since NT 3.1) so we link against it directly.
 *
 * @par SID allocation lifetime
 *
 * Every SID we produce via @c AllocateAndInitializeSid /
 * @c CreateAppContainerProfile is owned by the @c ProcessAttributes::Impl
 * that we install into the caller's @c ProcessAttributes out-param.
 * That struct's destructor (defined in `sandbox.cpp`) calls
 * @c FreeSid on every entry. The sandbox itself does not retain any
 * SIDs between @c configureProcessAttributes calls — each call
 * produces a fresh set, which matches the POSIX "per-spawn state"
 * posture of the other backends.
 */
#ifdef _WIN32

#include "ajazz/plugins/windows_app_container_sandbox.hpp"

#include "ajazz/core/logger.hpp"
#include "ajazz/plugins/sandbox.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sddl.h>
#include <userenv.h>
#include <windows.h>

// Link against advapi32 for SID + restricted-token primitives. userenv
// is delay-loaded at runtime instead of via the import lib so the DLL
// absence path stays graceful.
#pragma comment(lib, "advapi32.lib")

namespace ajazz::plugins {

// windowsProcessAttributesMut is defined in sandbox.cpp; forward-declare
// here so we can call it without exposing the pimpl definition.
extern ProcessAttributes::Impl& windowsProcessAttributesMut(ProcessAttributes& attrs);

namespace {

/// Default human-readable container name used when the caller does
/// not supply one. Stable across invocations so
/// `DeriveAppContainerSidFromAppContainerName` produces the same SID
/// on every run — handy for capability-grant ACLs that persist on
/// disk. Lowercase + dotted form matches Microsoft's convention.
constexpr wchar_t kDefaultContainerName[] = L"ajazz-control-center.plugin-host";

/// Resolve the AppContainer profile API entry points at runtime.
/// Returns non-null only on Windows 8+ with AppContainer support
/// enabled. Caller does not need to `FreeLibrary` the handle — it
/// stays loaded for the lifetime of the host process, which is the
/// same handling we use for every other system DLL.
struct AppContainerApi {
    using CreateProfileFn =
        HRESULT(WINAPI*)(PCWSTR, PCWSTR, PCWSTR, PSID_AND_ATTRIBUTES, DWORD, PSID*);
    using DeleteProfileFn = HRESULT(WINAPI*)(PCWSTR);
    using DeriveSidFn = HRESULT(WINAPI*)(LPCWSTR, PSID*);

    CreateProfileFn createProfile{nullptr};
    DeleteProfileFn deleteProfile{nullptr};
    DeriveSidFn deriveSid{nullptr};

    /// True if every required entry point was found.
    [[nodiscard]] bool usable() const noexcept { return deriveSid != nullptr; }
};

AppContainerApi const& loadAppContainerApi() {
    // Static local so the DLL + GetProcAddress happen exactly once per
    // process, thread-safely. std::call_once would also work; a static
    // local is terser and matches how the rest of the codebase lazy-
    // loads optional system libs.
    static AppContainerApi const api = []() -> AppContainerApi {
        AppContainerApi out;
        HMODULE const userenv = LoadLibraryW(L"userenv.dll");
        if (userenv == nullptr) {
            return out;
        }
        out.createProfile = reinterpret_cast<AppContainerApi::CreateProfileFn>(
            GetProcAddress(userenv, "CreateAppContainerProfile"));
        out.deleteProfile = reinterpret_cast<AppContainerApi::DeleteProfileFn>(
            GetProcAddress(userenv, "DeleteAppContainerProfile"));
        out.deriveSid = reinterpret_cast<AppContainerApi::DeriveSidFn>(
            GetProcAddress(userenv, "DeriveAppContainerSidFromAppContainerName"));
        return out;
    }();
    return api;
}

/// Convert a UTF-8 `std::string` to a `std::wstring` for Win32 APIs.
/// Plain ASCII passes straight through; non-ASCII is handled via
/// `MultiByteToWideChar(CP_UTF8)`. We keep this tiny helper inline
/// rather than pulling in `<codecvt>` (deprecated since C++17).
std::wstring toWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    int const size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

/// True if the granted set requests outbound network access. Mirrors
/// @c grantsNetwork in @c linux_bwrap_sandbox.cpp so the three
/// backends gate the same permission strings identically.
bool grantsNetwork(std::set<std::string> const& granted) {
    static constexpr std::array<std::string_view, 3> kNetworkPerms{
        "obs-websocket",
        "spotify",
        "discord-rpc",
    };
    for (auto const& perm : kNetworkPerms) {
        if (granted.count(std::string{perm}) != 0) {
            return true;
        }
    }
    return false;
}

/// True if the granted set requests any broker-surface permission
/// (notifications / media control / power). On Linux/macOS these
/// gate DBus / mach-lookup; on Windows they map to the coarse
/// `internetClientServer` capability.
bool grantsBroker(std::set<std::string> const& granted) {
    static constexpr std::array<std::string_view, 3> kBrokerPerms{
        "notifications",
        "media-control",
        "system-power",
    };
    for (auto const& perm : kBrokerPerms) {
        if (granted.count(std::string{perm}) != 0) {
            return true;
        }
    }
    return false;
}

/// Allocate a capability SID for a well-known capability RID
/// (`SECURITY_CAPABILITY_*`). Returns null on failure; the caller
/// logs and continues without that capability.
PSID allocCapabilitySid(DWORD rid) {
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_APP_PACKAGE_AUTHORITY;
    PSID sid = nullptr;
    // SECURITY_CAPABILITY_BASE_RID (3) is the first sub-authority for
    // every capability SID; the specific RID follows.
    if (!AllocateAndInitializeSid(&auth,
                                  SECURITY_BUILTIN_CAPABILITY_RID_COUNT,
                                  SECURITY_CAPABILITY_BASE_RID,
                                  rid,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  &sid)) {
        return nullptr;
    }
    return sid;
}

/// Create a restricted access token from the current process token.
/// The returned handle is owned by the caller. We pass
/// `DISABLE_MAX_PRIVILEGE` to drop every privilege, which is the
/// strongest portable restriction (matching bwrap's unprivileged
/// posture). Returns null on failure; caller falls back to the
/// normal primary token.
HANDLE makeRestrictedToken() {
    HANDLE primary = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &primary)) {
        return nullptr;
    }
    HANDLE restricted = nullptr;
    BOOL const ok = CreateRestrictedToken(primary,
                                          DISABLE_MAX_PRIVILEGE,
                                          0,       // no SIDs to disable (use default)
                                          nullptr, // "
                                          0,       // no privileges to delete beyond MAX
                                          nullptr,
                                          0, // no restricting SIDs
                                          nullptr,
                                          &restricted);
    CloseHandle(primary);
    return ok ? restricted : nullptr;
}

} // namespace

WindowsAppContainerSandbox::WindowsAppContainerSandbox(std::set<std::string> grantedPermissions,
                                                       std::string containerName)
    : m_grantedPermissions(std::move(grantedPermissions)),
      m_containerName(std::move(containerName)) {
    if (m_containerName.empty()) {
        // UTF-8 view of the wide default label. Kept as a string so
        // the diagnostic getters don't need to re-encode on every
        // call.
        m_containerName = "ajazz-control-center.plugin-host";
    }
    m_hasAppContainer = loadAppContainerApi().usable();
}

DecoratedSpawn WindowsAppContainerSandbox::decorate(std::string const& pythonExe,
                                                    std::filesystem::path const& scriptPath) const {
    // AppContainer isolation is applied at CreateProcessW time, not
    // via a wrapper executable. The decorated spawn is therefore
    // identical to NoOpSandbox; the real work happens in
    // configureProcessAttributes.
    DecoratedSpawn out;
    out.executable = pythonExe;
    out.args = {pythonExe, scriptPath.string()};
    return out;
}

void WindowsAppContainerSandbox::configureProcessAttributes(ProcessAttributes& attributes) const {
    if (!m_hasAppContainer) {
        // Passthrough: the host's CreateProcessW call won't consult
        // the pimpl (it's null), equivalent to NoOpSandbox.
        return;
    }

    auto& api = loadAppContainerApi();
    auto& impl = windowsProcessAttributesMut(attributes);

    // 1. Derive the AppContainer SID from the container name. This
    //    is deterministic — same name ⇒ same SID across machines —
    //    so ACLs granted to the SID on one host are portable. On
    //    failure we log and fall back to passthrough (no SID set);
    //    the host will still spawn, just without AppContainer
    //    isolation for this invocation.
    std::wstring const wideName = toWide(m_containerName);
    PSID containerSid = nullptr;
    HRESULT const hr = api.deriveSid(wideName.c_str(), &containerSid);
    if (FAILED(hr) || containerSid == nullptr) {
        AJAZZ_LOG_WARN("plugin-host-sandbox",
                       "DeriveAppContainerSidFromAppContainerName failed for '{}' (hr=0x{:x})",
                       m_containerName,
                       static_cast<unsigned>(hr));
        return;
    }
    impl.appContainerSid = containerSid;

    // 2. Populate capability SIDs based on the granted permission
    //    set. Each PSID is owned by the pimpl (FreeSid on destroy).
    auto pushCap = [&impl](DWORD rid, char const* label) {
        PSID const sid = allocCapabilitySid(rid);
        if (sid == nullptr) {
            AJAZZ_LOG_WARN("plugin-host-sandbox",
                           "AllocateAndInitializeSid failed for capability '{}'",
                           label);
            return;
        }
        SID_AND_ATTRIBUTES attr{};
        attr.Sid = sid;
        attr.Attributes = SE_GROUP_ENABLED;
        impl.capabilitySids.push_back(sid);
        impl.capabilities.push_back(attr);
    };

    if (grantsNetwork(m_grantedPermissions) || grantsBroker(m_grantedPermissions)) {
        // SECURITY_CAPABILITY_INTERNET_CLIENT (3). Required for any
        // outbound network socket under AppContainer.
        pushCap(SECURITY_CAPABILITY_INTERNET_CLIENT, "internetClient");
    }
    if (grantsBroker(m_grantedPermissions)) {
        // SECURITY_CAPABILITY_INTERNET_CLIENT_SERVER (2). Covers the
        // broker surfaces notifications / media / power use on
        // modern Windows.
        pushCap(SECURITY_CAPABILITY_INTERNET_CLIENT_SERVER, "internetClientServer");
    }

    // 3. Restricted token. Drops every privilege via
    //    DISABLE_MAX_PRIVILEGE. Stored in the pimpl; CloseHandle
    //    on destroy.
    impl.restrictedToken = makeRestrictedToken();
    if (impl.restrictedToken == nullptr) {
        AJAZZ_LOG_WARN("plugin-host-sandbox",
                       "CreateRestrictedToken failed (GetLastError={}); falling back to primary",
                       GetLastError());
        // Non-fatal: the host will spawn under the primary token,
        // still AppContainer-isolated, just without the privilege
        // strip. Partial hardening is better than no hardening.
    }
}

} // namespace ajazz::plugins

#endif // _WIN32
