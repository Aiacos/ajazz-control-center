// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_stub.cpp
 * @brief Always-compiled recording stub for @ref ajazz::core::IActiveWindowWatcher
 *        + the default-factory definition.
 *
 * @c StubActiveWindowWatcher is the default implementation when no native OS
 * backend is built/available (the default — every unit test, and any platform
 * without a supported foreground API). It records each injected foreground
 * change into a string log (for test assertions) and fires the @c start()
 * callback synchronously via @ref StubActiveWindowWatcher::injectForeground —
 * the Wave-0 "synthetic foreground change" seam that backs both the Catch2
 * scaffolds and the @c window.setForeground debug RPC.
 *
 * @ref makeDefaultActiveWindowWatcher is defined HERE so the default path is
 * always present even when no OS TU is compiled. When a platform backend is
 * available, the factory delegates to @c makePlatformActiveWindowWatcher()
 * (declared here, defined by the per-OS TUs in Plan 03, behind the
 * AJAZZ_FEATURE_ACTIVE_WINDOW gate). On Linux that backend picks
 * wayland-vs-x11 at RUNTIME by session type.
 *
 * Mirrors @c input_synthesizer_stub.cpp (recording stub + factory-here +
 * platform forward-decl). The class itself is declared in the public header
 * (unlike the anonymous input-synth stub) because the debug facade and the
 * unit tests need its @c injectForeground / @c log seam.
 */
#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/logger.hpp"

#include <utility>

namespace ajazz::core {

namespace {

// Forward declaration for the platform backend (defined by the per-OS TUs in
// Plan 03 when AJAZZ_FEATURE_ACTIVE_WINDOW is ON). Kept behind the same feature
// gate as the input-synth split so the default build stays hardware-free.
#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
std::unique_ptr<IActiveWindowWatcher> makePlatformActiveWindowWatcher();
#endif

} // namespace

void StubActiveWindowWatcher::start(std::function<void(ActiveWindowInfo)> onChange) {
    onChange_ = std::move(onChange);
    AJAZZ_LOG_INFO("active_window", "stub start (callback {})", onChange_ ? "set" : "null");
}

void StubActiveWindowWatcher::stop() {
    onChange_ = nullptr;
    AJAZZ_LOG_INFO("active_window", "stub stop");
}

bool StubActiveWindowWatcher::capabilityAvailable() const {
    return capabilityAvailable_;
}

void StubActiveWindowWatcher::setCapabilityAvailable(bool available) noexcept {
    capabilityAvailable_ = available;
}

void StubActiveWindowWatcher::injectForeground(ActiveWindowInfo info) {
    log_.push_back("fg:" + info.appId);
    AJAZZ_LOG_INFO("active_window", "stub injectForeground: {}", info.appId);
    if (onChange_) {
        onChange_(std::move(info));
    }
}

std::vector<std::string> const& StubActiveWindowWatcher::log() const noexcept {
    return log_;
}

void StubActiveWindowWatcher::clearLog() noexcept {
    log_.clear();
}

std::unique_ptr<IActiveWindowWatcher> makeDefaultActiveWindowWatcher() {
#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
    if (auto platform = makePlatformActiveWindowWatcher()) {
        return platform;
    }
    // Platform backend unavailable on this desktop -> fall back to the stub
    // (which reports capabilityAvailable()==true by default; the live app sets
    // it false via the runtime degradation path when appropriate).
    return std::make_unique<StubActiveWindowWatcher>();
#else
    return std::make_unique<StubActiveWindowWatcher>();
#endif
}

} // namespace ajazz::core
