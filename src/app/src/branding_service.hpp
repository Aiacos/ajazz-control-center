// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file branding_service.hpp
 * @brief Build- and runtime-configurable branding (product name, theme, icons).
 *
 * The class is exposed to QML as the `branding` context property. QML files
 * read accent colors and product strings from here exclusively, so a custom
 * `AJAZZ_BRAND_DIR` at build time (or a `Branding/ThemeOverride` setting at
 * runtime) is enough to re-skin the app without touching any source.
 */
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QUrl>

namespace ajazz::app {

/**
 * @brief Holds product / vendor strings, accent colors and asset URLs.
 *
 * Build-layer values (product name, vendor name, app id, brand directory) are
 * baked in via CMake `target_compile_definitions` of the form
 * `AJAZZ_PRODUCT_NAME=...`. Runtime values (theme JSON) are read from
 * `QSettings("Branding/ThemeOverride")`, falling back to the embedded default.
 *
 * @see docs/architecture/BRANDING.md
 */
class BrandingService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString productName READ productName CONSTANT)
    Q_PROPERTY(QString vendorName READ vendorName CONSTANT)
    Q_PROPERTY(QString appId READ appId CONSTANT)
    Q_PROPERTY(QUrl appIconUrl READ appIconUrl CONSTANT)
    Q_PROPERTY(QUrl trayIconUrl READ trayIconUrl CONSTANT)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor accent2 READ accent2 NOTIFY themeChanged)
    Q_PROPERTY(QColor bgBase READ bgBase NOTIFY themeChanged)
    Q_PROPERTY(QColor bgSidebar READ bgSidebar NOTIFY themeChanged)
    Q_PROPERTY(QColor bgRowHover READ bgRowHover NOTIFY themeChanged)
    Q_PROPERTY(QColor fgPrimary READ fgPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor fgMuted READ fgMuted NOTIFY themeChanged)

public:
    /**
     * @brief Construct and load the embedded theme.
     *
     * If `QSettings` contains a `Branding/ThemeOverride` path pointing at a
     * readable JSON file, the override is loaded instead.
     */
    explicit BrandingService(QObject* parent = nullptr);

    [[nodiscard]] QString productName() const noexcept { return productName_; }
    [[nodiscard]] QString vendorName() const noexcept { return vendorName_; }
    [[nodiscard]] QString appId() const noexcept { return appId_; }

    [[nodiscard]] QUrl appIconUrl() const noexcept { return appIconUrl_; }
    [[nodiscard]] QUrl trayIconUrl() const noexcept { return trayIconUrl_; }

    [[nodiscard]] QColor accent() const noexcept { return accent_; }
    [[nodiscard]] QColor accent2() const noexcept { return accent2_; }
    [[nodiscard]] QColor bgBase() const noexcept { return bgBase_; }
    [[nodiscard]] QColor bgSidebar() const noexcept { return bgSidebar_; }
    [[nodiscard]] QColor bgRowHover() const noexcept { return bgRowHover_; }
    [[nodiscard]] QColor fgPrimary() const noexcept { return fgPrimary_; }
    [[nodiscard]] QColor fgMuted() const noexcept { return fgMuted_; }

    /**
     * @brief Replace the active theme from a JSON file.
     *
     * Emits @ref themeChanged on success. On failure (missing file, malformed
     * JSON, missing required keys) the previous theme is kept and `false` is
     * returned.
     *
     * @param path Absolute path to a theme.json conforming to the schema in
     *             docs/architecture/BRANDING.md.
     * @return true on success.
     */
    Q_INVOKABLE bool loadThemeFile(QString const& path);

    /**
     * @brief Return the bundled CHANGELOG.md content as plain Markdown.
     *
     * Closes #37 (in-app changelog viewer). The text is read from the
     * embedded resource `:/changelog/CHANGELOG.md`; if the resource is not
     * available a short fallback string is returned.
     */
    Q_INVOKABLE QString changelogText() const;

    /**
     * @brief Return the bundled PRIVACY.md as plain Markdown.
     *
     * Used by the "Privacy" link in About / Settings.
     */
    Q_INVOKABLE QString privacyText() const;

signals:
    /// Emitted whenever any color property changes.
    void themeChanged();

private:
    /// Load defaults from compile-time defines and embedded resources.
    void loadEmbeddedDefaults();

    /// Apply a parsed theme document. No-op for missing fields.
    void applyThemeDocument(class QJsonObject const& doc);

    QString productName_;
    QString vendorName_;
    QString appId_;
    QUrl appIconUrl_;
    QUrl trayIconUrl_;

    QColor accent_;
    QColor accent2_;
    QColor bgBase_;
    QColor bgSidebar_;
    QColor bgRowHover_;
    QColor fgPrimary_;
    QColor fgMuted_;
};

} // namespace ajazz::app
