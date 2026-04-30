// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file theme_service.hpp
 * @brief Light / dark / auto appearance switch for the QML UI.
 *
 * The class is exposed to QML as the `themeService` context property and
 * provides three modes:
 *
 *   - Auto:  follow the OS color scheme via Qt::ColorScheme.
 *   - Light: load the bundled `theme-light.json`.
 *   - Dark:  load the bundled `theme.json` (canonical AJAZZ palette).
 *
 * The selection is persisted via QSettings under the `Appearance/Mode` key so
 * the next launch restores the user's choice.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

class BrandingService;

/**
 * @brief QObject-derived appearance toggle.
 *
 * Owns no state of its own; delegates to BrandingService::loadThemeFile() to
 * swap palettes at runtime. Emits @ref modeChanged whenever the mode flips.
 *
 * @note Construct after BrandingService and pass it as the @p branding
 *       argument so the two share the same object lifetime.
 */
class ThemeService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ThemeService)
    QML_SINGLETON
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString effectiveMode READ effectiveMode NOTIFY effectiveModeChanged)

public:
    /// Appearance modes the service can be in.
    enum class Mode { Auto, Light, Dark };
    Q_ENUM(Mode)

    /**
     * @brief Construct a ThemeService bound to a BrandingService.
     * @param branding Non-null branding service whose loadThemeFile() will
     *                 be called when the mode changes.
     * @param parent   Optional Qt parent.
     */
    explicit ThemeService(BrandingService* branding, QObject* parent = nullptr);

    /// Return the current mode as the lowercase string "auto", "light" or "dark".
    [[nodiscard]] QString mode() const noexcept;

    /// Return the resolved theme polarity as "light" or "dark" — never "auto".
    /// When mode is "auto", resolves via QStyleHints::colorScheme() (Unknown
    /// maps to Dark, matching applyMode's existing behaviour). Bind QML
    /// `Material.theme` and any other theme-polarity-driven code to this
    /// property — it is the single source of truth for "is the UI light or
    /// dark right now". Auto-fires `effectiveModeChanged` when the OS color
    /// scheme changes (relevant only in Auto mode).
    [[nodiscard]] QString effectiveMode() const noexcept;

    /// Set the mode by string ("auto" / "light" / "dark"). Persists to QSettings.
    void setMode(QString const& mode);

    /// QML singleton factory. Returns the @ref Application-owned instance
    /// previously handed in via @ref registerInstance with CppOwnership.
    static ThemeService* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory. Application calls
    /// this with @c m_themeService.get() before the engine loads QML.
    static void registerInstance(ThemeService* instance) noexcept;

signals:
    /// Emitted after a successful mode change.
    void modeChanged();
    /// Emitted whenever the resolved theme polarity changes — either because
    /// the user picked a different mode, or (in Auto mode) the OS color
    /// scheme flipped.
    void effectiveModeChanged();

private:
    /// Apply the given mode by calling BrandingService::loadThemeFile() with
    /// the matching qrc path (and resolving "auto" via Qt::ColorScheme).
    void applyMode(Mode mode);

    BrandingService* branding_ = nullptr; ///< Non-owning pointer.
    Mode mode_ = Mode::Auto;
};

} // namespace ajazz::app
