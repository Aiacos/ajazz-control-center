// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sandbox.cpp
 * @brief Out-of-line special members for @ref ProcessAttributes +
 *        pimpl definition.
 *
 * @ref ProcessAttributes uses pimpl so the header (included from
 * every platform's host backend) can avoid dragging in
 * `<windows.h>`. The pimpl's @c Impl struct has per-OS payload:
 *
 *   - **POSIX**: empty — the full sandbox is expressed as argv
 *     decoration in @ref LinuxBwrapSandbox / @ref MacosSandboxExecSandbox,
 *     so there is no per-spawn process-attribute state to carry.
 *   - **Windows**: owns the AppContainer SID, the capability SID
 *     array, and the restricted-token HANDLE produced by
 *     @ref WindowsAppContainerSandbox. The win32 host backend reads
 *     these via @ref windowsProcessAttributes below.
 *
 * Defining @c Impl here (rather than in the per-OS sandbox source)
 * lets the rule-of-five members live next to the complete type so
 * `unique_ptr<Impl>::~unique_ptr()` always sees it. The Windows
 * sandbox mutates @c Impl through @ref windowsProcessAttributesMut
 * and the host reads it through @ref windowsProcessAttributes; those
 * helpers are the only public handshake across the pimpl boundary.
 */
#include "ajazz/plugins/sandbox.hpp"

#ifdef _WIN32
// Brings in the complete @c ProcessAttributes::Impl definition required by
// the helpers below. The header sets NOMINMAX / WIN32_LEAN_AND_MEAN and
// includes @c <windows.h>; consumers that need to read or mutate the pimpl
// payload (windows_app_container_sandbox.cpp, out_of_process_plugin_host_win32.cpp)
// include the same header so all translation units agree on the layout.
#include "process_attributes_impl_win32.hpp"
#endif

namespace ajazz::plugins {

#ifdef _WIN32

/// Read-only view on the Windows pimpl. Returns null if the
/// @ref ProcessAttributes instance carries no win32 state (either
/// because it was produced by a POSIX sandbox, because the sandbox
/// fell back to passthrough, or because the struct was default-
/// constructed without a sandbox). The win32 host backend consults
/// this when deciding whether to use `STARTUPINFOEX` or a plain
/// `STARTUPINFO`.
ProcessAttributes::Impl const* windowsProcessAttributes(ProcessAttributes const& attrs) noexcept {
    return attrs.impl.get();
}

/// Mutable accessor, used by @ref WindowsAppContainerSandbox to
/// lazily install the pimpl during @c configureProcessAttributes.
/// Creates the @c Impl on first call so most instances stay cheap.
ProcessAttributes::Impl& windowsProcessAttributesMut(ProcessAttributes& attrs) {
    if (!attrs.impl) {
        attrs.impl = std::make_unique<ProcessAttributes::Impl>();
    }
    return *attrs.impl;
}

#else // !_WIN32

// On POSIX there is no platform-specific process-attribute state —
// the full sandbox is expressed through argv decoration. The @c Impl
// struct is still defined (empty) so `unique_ptr<Impl>` can destroy
// it cleanly.
struct ProcessAttributes::Impl {};

#endif // _WIN32

ProcessAttributes::ProcessAttributes() = default;
ProcessAttributes::~ProcessAttributes() = default;
ProcessAttributes::ProcessAttributes(ProcessAttributes&&) noexcept = default;
ProcessAttributes& ProcessAttributes::operator=(ProcessAttributes&&) noexcept = default;

} // namespace ajazz::plugins
