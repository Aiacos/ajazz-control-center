// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file process_attributes_impl_win32.hpp
 * @brief Private definition of @ref ProcessAttributes::Impl for the Win32
 *        backend. Included by every translation unit that touches the
 *        pimpl payload directly: @c sandbox.cpp (for the rule-of-five
 *        members + helper exports), @c windows_app_container_sandbox.cpp
 *        (populates the struct from @c configureProcessAttributes) and
 *        @c out_of_process_plugin_host_win32.cpp (consumes it when
 *        building @c STARTUPINFOEX for @c CreateProcessW).
 *
 * Defining @c Impl in a private header — instead of inlining it in one
 * cpp and forward-declaring across the others — is the only way the
 * pimpl can be inspected by the consumers without making the public
 * @c sandbox.hpp include @c <windows.h>. Each consumer that needs
 * complete-type access just @c #includes this file; consumers that
 * only hold @c std::unique_ptr<Impl> by value (the pimpl itself) get
 * the forward declaration from the public header and do not need it.
 *
 * Inclusion is guarded by @c _WIN32; on POSIX the file expands to
 * nothing, and the empty @c Impl placeholder still defined in
 * @c sandbox.cpp (under @c \#else) is what the unique_ptr destructor
 * sees. POSIX backends never #include this file.
 */
#pragma once

#ifdef _WIN32

#include "ajazz/plugins/sandbox.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <vector>

#include <windows.h>

namespace ajazz::plugins {

/// Per-spawn Windows process-attribute state. Populated by
/// @ref WindowsAppContainerSandbox::configureProcessAttributes and
/// consumed by @c out_of_process_plugin_host_win32.cpp when it builds
/// the @c STARTUPINFOEX for @c CreateProcessW.
///
/// Ownership notes:
///   - @c appContainerSid and each entry in @c capabilitySids are
///     allocated with @c AllocateAndInitializeSid and freed with
///     @c FreeSid in the destructor.
///   - @c restrictedToken is a kernel handle owned by this struct;
///     @c CloseHandle is called from the destructor.
///
/// All members are null / empty by default ("no AppContainer
/// configured, no restricted token") so a plain value-initialised
/// @c ProcessAttributes is indistinguishable from a POSIX one and
/// the win32 host backend falls back to a plain @c CreateProcessW.
struct ProcessAttributes::Impl {
    PSID appContainerSid{nullptr};
    std::vector<SID_AND_ATTRIBUTES> capabilities;
    std::vector<PSID> capabilitySids; ///< mirrors @c capabilities for FreeSid
    HANDLE restrictedToken{nullptr};

    Impl() = default;

    ~Impl() {
        if (restrictedToken != nullptr) {
            CloseHandle(restrictedToken);
            restrictedToken = nullptr;
        }
        for (PSID sid : capabilitySids) {
            if (sid != nullptr) {
                FreeSid(sid);
            }
        }
        capabilitySids.clear();
        capabilities.clear();
        if (appContainerSid != nullptr) {
            FreeSid(appContainerSid);
            appContainerSid = nullptr;
        }
    }

    Impl(Impl const&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
};

} // namespace ajazz::plugins

#endif // _WIN32
