// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file branding_service.cpp
 * @brief Implementation of @ref ajazz::app::BrandingService.
 */
#include "branding_service.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSettings>

#ifndef AJAZZ_PRODUCT_NAME
/// Default product name when the build did not set one.
#define AJAZZ_PRODUCT_NAME "AJAZZ Control Center"
#endif
#ifndef AJAZZ_VENDOR_NAME
#define AJAZZ_VENDOR_NAME "Aiacos"
#endif
#ifndef AJAZZ_APP_ID
#define AJAZZ_APP_ID "io.github.Aiacos.AjazzControlCenter"
#endif
#ifndef AJAZZ_APP_ICON_QRC
/// QRC URL of the application icon. The build copies AJAZZ_BRAND_DIR/app.svg
/// (or resources/branding/app.svg as fallback) to a stable QRC alias so this
/// default works in vanilla and rebranded builds alike.
#define AJAZZ_APP_ICON_QRC "qrc:/qt/qml/AjazzControlCenter/branding/app.svg"
#endif
#ifndef AJAZZ_TRAY_ICON_QRC
#define AJAZZ_TRAY_ICON_QRC "qrc:/qt/qml/AjazzControlCenter/branding/tray.svg"
#endif
#ifndef AJAZZ_THEME_QRC
/// Resource path (note the leading colon, not qrc:) for QFile.
#define AJAZZ_THEME_QRC ":/qt/qml/AjazzControlCenter/branding/theme.json"
#endif

namespace ajazz::app {

namespace {

/// Pointer set by @ref BrandingService::registerInstance and consumed by the
/// @ref BrandingService::create QML factory. Stays @c nullptr in test contexts
/// where Application is not constructed; QML never loads in tests so the
/// factory is never called.
BrandingService* s_brandingInstance = nullptr;

/// Parse a hex color string, leaving @p target unchanged on failure.
void readColor(QJsonObject const& doc, char const* key, QColor& target) {
    auto const v = doc.value(QString::fromUtf8(key));
    if (v.isString()) {
        QColor c(v.toString());
        if (c.isValid()) {
            target = c;
        }
    }
}

} // namespace

BrandingService* BrandingService::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_brandingInstance != nullptr,
               "BrandingService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_brandingInstance, QQmlEngine::CppOwnership);
    return s_brandingInstance;
}

void BrandingService::registerInstance(BrandingService* instance) noexcept {
    s_brandingInstance = instance;
}

BrandingService::BrandingService(QObject* parent) : QObject(parent) {
    loadEmbeddedDefaults();

    QSettings settings;
    auto const overridePath = settings.value("Branding/ThemeOverride").toString();
    if (!overridePath.isEmpty()) {
        loadThemeFile(overridePath);
    }
}

void BrandingService::loadEmbeddedDefaults() {
    productName_ = QStringLiteral(AJAZZ_PRODUCT_NAME);
    vendorName_ = QStringLiteral(AJAZZ_VENDOR_NAME);
    appId_ = QStringLiteral(AJAZZ_APP_ID);
    appIconUrl_ = QUrl(QStringLiteral(AJAZZ_APP_ICON_QRC));
    trayIconUrl_ = QUrl(QStringLiteral(AJAZZ_TRAY_ICON_QRC));

    // Fallback palette matches the AJAZZ dark theme baked into the QML.
    accent_ = QColor("#41CD52");
    accent2_ = QColor("#0A82FA");
    bgBase_ = QColor("#14141a");
    bgSidebar_ = QColor("#1e1e23");
    bgRowHover_ = QColor("#2c2c34");
    fgPrimary_ = QColor("#f0f0f0");
    fgMuted_ = QColor("#888888");

    QFile embedded(QStringLiteral(AJAZZ_THEME_QRC));
    if (embedded.open(QIODevice::ReadOnly)) {
        auto const doc = QJsonDocument::fromJson(embedded.readAll());
        if (doc.isObject()) {
            applyThemeDocument(doc.object());
        }
    }
}

bool BrandingService::loadThemeFile(QString const& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    auto const doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return false;
    }
    applyThemeDocument(doc.object());
    emit themeChanged();
    return true;
}

QString BrandingService::changelogText() const {
    QFile f(QStringLiteral(":/qt/qml/AjazzControlCenter/CHANGELOG.md"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(f.readAll());
    }
    return QStringLiteral(
        "# Changelog\n\nSee [CHANGELOG.md on GitHub]"
        "(https://github.com/Aiacos/ajazz-control-center/blob/main/CHANGELOG.md).");
}

QString BrandingService::privacyText() const {
    QFile f(QStringLiteral(":/qt/qml/AjazzControlCenter/PRIVACY.md"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(f.readAll());
    }
    return QStringLiteral(
        "# Privacy\n\nSee [PRIVACY.md on GitHub]"
        "(https://github.com/Aiacos/ajazz-control-center/blob/main/docs/PRIVACY.md).");
}

void BrandingService::applyThemeDocument(QJsonObject const& doc) {
    readColor(doc, "accent", accent_);
    readColor(doc, "accent2", accent2_);
    readColor(doc, "bgBase", bgBase_);
    readColor(doc, "bgSidebar", bgSidebar_);
    readColor(doc, "bgRowHover", bgRowHover_);
    readColor(doc, "fgPrimary", fgPrimary_);
    readColor(doc, "fgMuted", fgMuted_);
}

} // namespace ajazz::app
