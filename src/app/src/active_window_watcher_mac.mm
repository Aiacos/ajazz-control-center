// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_mac.mm
 * @brief macOS foreground-application backend for @ref ajazz::core::IActiveWindowWatcher
 *        (NSWorkspace.frontmostApplication + didActivateApplicationNotification).
 *
 * Observes @c NSWorkspaceDidActivateApplicationNotification on the shared
 * workspace's notification center and resolves the newly-frontmost application's
 * identity (bundle id when present, falling back to the localized name), feeding
 * the shared trailing-edge debounce. macOS reports app-level activation (not
 * per-window focus), which is exactly the granularity the per-app profile switch
 * needs.
 *
 * App-identity contract: prefer the reverse-DNS @c bundleIdentifier (stable,
 * e.g. @c "org.mozilla.firefox"); fall back to the lowercased localized name. The
 * value is lowercased so it matches the case-insensitive matcher (Plan 04). Note
 * the macOS token differs in shape from the Wayland/X11 app_id; the parity doc
 * records the per-platform mapping (ACTIVE-WINDOW-IDENTITY).
 *
 * COMPILE-GUARDED + unit-tested only: there is no macOS device in this dev
 * environment, so this Obj-C++ TU is validated by compiling clean and by the
 * stub-driven debounce/factory unit tests on Linux. The live focus walk is a
 * macOS-host task (deferred). The .mm TU is added to the target only in the
 * Darwin branch of src/app/CMakeLists.txt (OBJCXX enabled there).
 *
 * Apple-clang -Werror compliance (CLAUDE.md cross-platform strictness):
 *  - No @c inline constexpr at file scope (triggers -Wunused-const-variable).
 *
 * Gated by @c __APPLE__ && @c AJAZZ_FEATURE_ACTIVE_WINDOW; the whole TU is empty
 * otherwise so it compiles cleanly on every platform.
 */
#if defined(__APPLE__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)

#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/logger.hpp"

#include "active_window_debounce.hpp"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <memory>
#include <string>
#include <utility>

namespace ajazz::core {

namespace {

/// Resolve an NSRunningApplication to the app-identity token (lowercased bundle id
/// or localized name).
std::string identityFor(NSRunningApplication* app) {
    if (app == nil) {
        return {};
    }
    NSString* bundleId = app.bundleIdentifier;
    if (bundleId != nil && bundleId.length > 0) {
        return std::string(bundleId.lowercaseString.UTF8String);
    }
    NSString* name = app.localizedName;
    if (name != nil && name.length > 0) {
        return std::string(name.lowercaseString.UTF8String);
    }
    return {};
}

/**
 * @brief macOS NSWorkspace @ref IActiveWindowWatcher implementation.
 */
class MacActiveWindowWatcher final : public IActiveWindowWatcher {
public:
    MacActiveWindowWatcher() : debounce_(std::make_unique<ActiveWindowDebouncer>()) {}

    ~MacActiveWindowWatcher() override { removeObserver(); }

    MacActiveWindowWatcher(MacActiveWindowWatcher const&) = delete;
    MacActiveWindowWatcher& operator=(MacActiveWindowWatcher const&) = delete;
    MacActiveWindowWatcher(MacActiveWindowWatcher&&) = delete;
    MacActiveWindowWatcher& operator=(MacActiveWindowWatcher&&) = delete;

    void start(std::function<void(ActiveWindowInfo)> onChange) override {
        debounce_->setCallback(std::move(onChange));
        removeObserver();
        NSNotificationCenter* center = NSWorkspace.sharedWorkspace.notificationCenter;
        observer_ = [center addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                        object:nil
                                         queue:NSOperationQueue.mainQueue
                                    usingBlock:^(NSNotification* note) {
                                      NSRunningApplication* app =
                                          note.userInfo[NSWorkspaceApplicationKey];
                                      this->emitFor(app);
                                    }];
        // Seed with the current frontmost application.
        emitFor(NSWorkspace.sharedWorkspace.frontmostApplication);
    }

    void stop() override {
        removeObserver();
        debounce_->setCallback(nullptr);
    }

    // NSWorkspace always exposes the frontmost application on macOS.
    [[nodiscard]] bool capabilityAvailable() const override { return true; }

private:
    void emitFor(NSRunningApplication* app) {
        std::string appId = identityFor(app);
        if (appId.empty()) {
            return;
        }
        std::string title =
            (app.localizedName != nil) ? std::string(app.localizedName.UTF8String) : std::string{};
        debounce_->submit(ActiveWindowInfo{std::move(appId), std::move(title)});
    }

    void removeObserver() {
        if (observer_ != nil) {
            [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer_];
            observer_ = nil;
        }
    }

    std::unique_ptr<ActiveWindowDebouncer> debounce_;
    id observer_{nil};
};

} // namespace

std::unique_ptr<IActiveWindowWatcher> makeMacActiveWindowWatcher() {
    return std::make_unique<MacActiveWindowWatcher>();
}

} // namespace ajazz::core

#endif // defined(__APPLE__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
