// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file theme_service.cpp
 * @brief ThemeService implementation; persists choice via QSettings.
 */
#include "theme_service.hpp"

#include "branding_service.hpp"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QSettings>
#include <QStyleHints>

namespace ajazz::app {

namespace {

/// Pointer set by ThemeService::registerInstance, consumed by ::create.
ThemeService* s_themeServiceInstance = nullptr;

constexpr auto kSettingsKey = "Appearance/Mode";
constexpr auto kDarkPath = ":/qt/qml/AjazzControlCenter/branding/theme.json";
constexpr auto kLightPath = ":/qt/qml/AjazzControlCenter/branding/theme-light.json";

ThemeService::Mode parseMode(QString const& s) {
    if (s.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
        return ThemeService::Mode::Light;
    }
    if (s.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return ThemeService::Mode::Dark;
    }
    return ThemeService::Mode::Auto;
}

QString modeToString(ThemeService::Mode mode) {
    switch (mode) {
    case ThemeService::Mode::Light:
        return QStringLiteral("light");
    case ThemeService::Mode::Dark:
        return QStringLiteral("dark");
    case ThemeService::Mode::Auto:
    default:
        return QStringLiteral("auto");
    }
}
} // namespace

ThemeService* ThemeService::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_themeServiceInstance != nullptr,
               "ThemeService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_themeServiceInstance, QQmlEngine::CppOwnership);
    return s_themeServiceInstance;
}

void ThemeService::registerInstance(ThemeService* instance) noexcept {
    s_themeServiceInstance = instance;
}

ThemeService::ThemeService(BrandingService* branding, QObject* parent)
    : QObject(parent), branding_(branding) {
    QSettings settings;
    auto const stored = settings.value(kSettingsKey, QStringLiteral("auto")).toString();
    mode_ = parseMode(stored);
    applyMode(mode_);

    // Track OS color-scheme changes so Auto mode keeps the BrandingService
    // palette and the QML Material.theme attached property in lockstep with
    // the rest of the desktop. Connection is unconditional — we filter on
    // mode_ inside the slot so flipping in/out of Auto behaves correctly.
    if (auto* hints = QGuiApplication::styleHints()) {
        QObject::connect(
            hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme /*scheme*/) {
                if (mode_ == Mode::Auto) {
                    applyMode(mode_);
                    emit effectiveModeChanged();
                }
            });
    }
}

QString ThemeService::mode() const noexcept {
    return modeToString(mode_);
}

QString ThemeService::effectiveMode() const noexcept {
    auto resolved = mode_;
    if (resolved == Mode::Auto) {
        auto const* hints = QGuiApplication::styleHints();
        resolved = (hints != nullptr && hints->colorScheme() == Qt::ColorScheme::Light)
                       ? Mode::Light
                       : Mode::Dark;
    }
    return modeToString(resolved);
}

void ThemeService::setMode(QString const& mode) {
    auto const next = parseMode(mode);
    if (next == mode_) {
        return;
    }
    mode_ = next;
    QSettings settings;
    settings.setValue(kSettingsKey, modeToString(mode_));
    applyMode(mode_);
    emit modeChanged();
    emit effectiveModeChanged();
}

void ThemeService::applyMode(Mode mode) {
    if (branding_ == nullptr) {
        return;
    }
    auto const* hints = QGuiApplication::styleHints();
    auto resolved = mode;
    if (resolved == Mode::Auto && hints != nullptr) {
        resolved = hints->colorScheme() == Qt::ColorScheme::Light ? Mode::Light : Mode::Dark;
    }
    auto const path =
        (resolved == Mode::Light) ? QString::fromUtf8(kLightPath) : QString::fromUtf8(kDarkPath);
    (void)branding_->loadThemeFile(path);
}

} // namespace ajazz::app
