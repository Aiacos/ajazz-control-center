// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_wayland.cpp
 * @brief Wayland foreground-window backend for @ref ajazz::core::IActiveWindowWatcher
 *        (zwlr_foreign_toplevel_management_unstable_v1 via Qt6::WaylandClient).
 *
 * Binds the wlroots foreign-toplevel-management global via
 * @c QWaylandClientExtensionTemplate<T> over the qtwaylandscanner-generated
 * @c QtWayland::zwlr_foreign_toplevel_manager_v1 base. Each compositor-reported
 * toplevel is wrapped in a per-handle @c QtWayland::zwlr_foreign_toplevel_handle_v1
 * subclass that tracks its @c app_id / @c title and the @c activated bit of its
 * latest @c state array. The focused application is the handle whose committed
 * (@c done) state contains @c state_activated.
 *
 * ## Capability gating (APROF-03)
 *
 * @c QWaylandClientExtension::isActive() is @c true once the global binds and
 * @c false when the compositor does not advertise
 * @c zwlr_foreign_toplevel_manager_v1 (GNOME/KDE without the global). The watcher
 * maps that flag straight to @ref capabilityAvailable(), so the UI warning chip
 * (Plan 05) lights up on a non-wlroots desktop and the watcher emits no automatic
 * changes there (graceful degradation; never crash, never fail open).
 *
 * ## Wire trust boundary
 *
 * The compositor-reported @c app_id / @c title strings are untrusted match
 * tokens (threat T-34-03-03): they are carried as plain strings to the
 * @ref ActiveWindowInfo callback and never eval'd, exec'd, or shelled out.
 *
 * Pitfalls handled (34-RESEARCH):
 *  - Pitfall 1: bind wlr ONLY (carries @c activated); never ext-foreign-toplevel-list.
 *  - Pitfall 4: on a @c done with no activated handle, do NOT emit a spurious
 *    empty change — keep the last foreground (debounce absorbs transient gaps).
 *  - Pitfall 5: constructed from Application init on the GUI thread after
 *    QGuiApplication exists (the factory enforces this — never static-init).
 *
 * Gated by @c __linux__ && @c AJAZZ_FEATURE_ACTIVE_WINDOW; the whole TU is empty
 * otherwise so it compiles cleanly on every platform.
 */
#if defined(__linux__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)

#include "active_window_debounce.hpp"
#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QString>
#include <QtWaylandClient/QWaylandClientExtension>

#include <memory>
#include <unordered_map>
#include <utility>

// qtwaylandscanner-generated client bases (PRIVATE_CODE; emitted into the build
// dir as qwayland-<basename>.h). Provides QtWayland::zwlr_foreign_toplevel_manager_v1
// + _handle_v1 with the protected app_id/title/state/done/closed virtuals.
#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

namespace ajazz::core {

namespace {

class WlrToplevelManager; // fwd

/**
 * @brief Per-toplevel handle wrapper. Tracks identity (@c app_id / @c title) and
 *        the @c activated bit of the latest committed @c state array.
 *
 * The manager owns one of these per live toplevel; @c done() pushes the current
 * snapshot to the manager so it can recompute which handle is focused.
 */
class WlrToplevelHandle final : public QtWayland::zwlr_foreign_toplevel_handle_v1 {
public:
    WlrToplevelHandle(::zwlr_foreign_toplevel_handle_v1* object, WlrToplevelManager* manager)
        : QtWayland::zwlr_foreign_toplevel_handle_v1(object), manager_(manager) {}

    [[nodiscard]] QString appId() const { return appId_; }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] bool activated() const { return activated_; }

protected:
    void zwlr_foreign_toplevel_handle_v1_app_id(QString const& appId) override { appId_ = appId; }
    void zwlr_foreign_toplevel_handle_v1_title(QString const& title) override { title_ = title; }

    // The state event carries a wl_array of uint32 enum values; the activated bit
    // is present iff the toplevel currently has compositor focus. We stage it into
    // pendingActivated_ and only commit it on done() (the protocol's atomicity).
    void zwlr_foreign_toplevel_handle_v1_state(wl_array* stateArray) override {
        pendingActivated_ = false;
        if (stateArray == nullptr || stateArray->data == nullptr) {
            return;
        }
        auto const* values = static_cast<std::uint32_t const*>(stateArray->data);
        std::size_t const count = stateArray->size / sizeof(std::uint32_t);
        for (std::size_t i = 0; i < count; ++i) {
            if (values[i] == static_cast<std::uint32_t>(state_activated)) {
                pendingActivated_ = true;
                break;
            }
        }
    }

    void zwlr_foreign_toplevel_handle_v1_done() override;
    void zwlr_foreign_toplevel_handle_v1_closed() override;

private:
    WlrToplevelManager* manager_;
    QString appId_;
    QString title_;
    bool activated_{false};
    bool pendingActivated_{false};
};

/**
 * @brief The wlr foreign-toplevel manager extension. Wraps each compositor
 *        toplevel and recomputes the focused app on every committed change,
 *        feeding the debounce.
 */
class WlrToplevelManager final : public QWaylandClientExtensionTemplate<WlrToplevelManager>,
                                 public QtWayland::zwlr_foreign_toplevel_manager_v1 {
public:
    explicit WlrToplevelManager(ActiveWindowDebouncer* debounce)
        : QWaylandClientExtensionTemplate<WlrToplevelManager>(/*version*/ 3), debounce_(debounce) {}

    // Called by a handle on done()/closed() to recompute the focused toplevel.
    void recomputeFocus() {
        WlrToplevelHandle* focused = nullptr;
        for (auto const& [object, handle] : handles_) {
            if (handle->activated()) {
                focused = handle.get();
                break;
            }
        }
        // Pitfall 4: no activated handle (e.g. focus on a layer-shell panel /
        // the compositor surface) -> keep the current foreground, do not emit a
        // spurious empty change.
        if (focused == nullptr) {
            return;
        }
        // WR-03: mirror the X11 backend's empty-app_id guard so behaviour is
        // uniform across the Linux backends. Some toplevels (splash surfaces,
        // certain Electron/Java windows) are activated before they set app_id;
        // an empty token has no usable identity, so skip and keep the current
        // foreground (Pitfall 4) rather than feed "" into the auto-switch and
        // the launch/terminate fan-out.
        QString const appId = focused->appId();
        if (appId.isEmpty()) {
            return;
        }
        debounce_->submit(ActiveWindowInfo{appId.toStdString(), focused->title().toStdString()});
    }

    void disposeHandle(::zwlr_foreign_toplevel_handle_v1* object) {
        handles_.erase(object);
        recomputeFocus();
    }

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(
        ::zwlr_foreign_toplevel_handle_v1* toplevel) override {
        handles_.emplace(toplevel, std::make_unique<WlrToplevelHandle>(toplevel, this));
    }

    void zwlr_foreign_toplevel_manager_v1_finished() override { handles_.clear(); }

private:
    ActiveWindowDebouncer* debounce_;
    std::unordered_map<::zwlr_foreign_toplevel_handle_v1*, std::unique_ptr<WlrToplevelHandle>>
        handles_;
};

void WlrToplevelHandle::zwlr_foreign_toplevel_handle_v1_done() {
    activated_ = pendingActivated_;
    manager_->recomputeFocus();
}

void WlrToplevelHandle::zwlr_foreign_toplevel_handle_v1_closed() {
    manager_->disposeHandle(object());
}

/**
 * @brief Wayland (wlr-foreign-toplevel) @ref IActiveWindowWatcher implementation.
 *
 * Owns the extension + the trailing-edge debounce. @ref capabilityAvailable()
 * tracks @c isActive() (re-evaluated live via @c activeChanged).
 */
class WaylandActiveWindowWatcher final : public IActiveWindowWatcher {
public:
    WaylandActiveWindowWatcher()
        : debounce_(std::make_unique<ActiveWindowDebouncer>()),
          manager_(std::make_unique<WlrToplevelManager>(debounce_.get())) {
        // QWaylandClientExtensionTemplate auto-initializes on construction when a
        // QGuiApplication is up; isActive() then reflects whether the global bound.
        capable_ = manager_->isActive();
        QObject::connect(
            manager_.get(), &QWaylandClientExtension::activeChanged, manager_.get(), [this]() {
                capable_ = manager_->isActive();
                AJAZZ_LOG_INFO("active_window",
                               "wayland wlr-foreign-toplevel active={} "
                               "(capabilityAvailable)",
                               capable_);
            });
        AJAZZ_LOG_INFO(
            "active_window", "WaylandActiveWindowWatcher constructed (active={})", capable_);
    }

    void start(std::function<void(ActiveWindowInfo)> onChange) override {
        debounce_->setCallback(std::move(onChange));
    }

    void stop() override { debounce_->setCallback(nullptr); }

    [[nodiscard]] bool capabilityAvailable() const override { return capable_; }

private:
    std::unique_ptr<ActiveWindowDebouncer> debounce_;
    std::unique_ptr<WlrToplevelManager> manager_;
    bool capable_{false};
};

} // namespace

std::unique_ptr<IActiveWindowWatcher> makeWaylandActiveWindowWatcher() {
    return std::make_unique<WaylandActiveWindowWatcher>();
}

} // namespace ajazz::core

#endif // defined(__linux__) && defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
