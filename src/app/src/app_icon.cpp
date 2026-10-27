// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_icon.cpp
 * @brief Implementation of @ref ajazz::app::makeAppIcon.
 */
#include "app_icon.hpp"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QSvgRenderer>
#include <Qt>

#include <algorithm>
#include <array>

namespace ajazz::app {

namespace {

/// Render the SVG into a single sz × sz ARGB32 pixmap.
QPixmap renderAt(QSvgRenderer& renderer, int sz) {
    QImage img(sz, sz, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&p);
    p.end();
    return QPixmap::fromImage(img);
}

} // namespace

QIcon makeAppIcon(QString const& svgResourcePath, QString const& fallbackResourcePath) {
    // Sizes Qt actually asks QIcon for in this app: tray (16/22/24 — most
    // Wayland status bars use 22), window/taskbar (32/48/64), and the About /
    // splash surfaces (128/256/512). Pre-rendering each one means QIcon
    // returns the matching pixmap without scaling, which is the only way to
    // keep crisp edges at the small tray sizes where bilinear downscaling of
    // a single 256 px raster turns the artwork into a blurry smudge.
    static constexpr std::array<int, 9> Sizes = {16, 22, 24, 32, 48, 64, 128, 256, 512};

    QSvgRenderer renderer;
    if (!renderer.load(svgResourcePath) && !fallbackResourcePath.isEmpty()) {
        renderer.load(fallbackResourcePath);
    }
    if (!renderer.isValid()) {
        return {};
    }

    QIcon icon;
    for (int const sz : Sizes) {
        icon.addPixmap(renderAt(renderer, sz));
    }
    return icon;
}

QIcon makeBatteryStatusIcon(QIcon const& baseIcon, int percent) {
    if (baseIcon.isNull()) {
        return {};
    }

    static constexpr std::array<int, 9> Sizes = {16, 22, 24, 32, 48, 64, 128, 256, 512};
    int const charge = std::clamp(percent, 0, 100);
    QColor const chargeColor = charge < 20   ? QColor(231, 76, 60)
                               : charge < 40 ? QColor(241, 196, 15)
                                             : QColor(46, 204, 113);
    QIcon icon;
    for (int const size : Sizes) {
        QPixmap const source = baseIcon.pixmap(size, size);
        if (source.isNull()) {
            continue;
        }

        QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        int const extent = image.width();
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        qreal const scale = static_cast<qreal>(extent) / 32.0;
        qreal const badgeWidth = std::max(8.0, extent * 0.47);
        qreal const badgeHeight = std::max(6.0, extent * 0.27);
        qreal const margin = std::max(0.5, extent * 0.04);
        QRectF const badge(
            extent - badgeWidth - margin, extent - badgeHeight - margin, badgeWidth, badgeHeight);
        painter.setPen(QPen(QColor(255, 255, 255), std::max(1.0, scale)));
        painter.setBrush(QColor(24, 26, 30, 245));
        painter.drawRoundedRect(badge, extent * 0.06, extent * 0.06);

        QRectF const body(badge.x() + scale,
                          badge.y() + scale,
                          badge.width() - 3.0 * scale,
                          badge.height() - 2.0 * scale);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 90));
        painter.drawRect(body);
        QRectF const fill(body.x(), body.y(), body.width() * charge / 100.0, body.height());
        painter.setBrush(chargeColor);
        painter.drawRect(fill);
        QRectF const terminal(
            body.right() + scale, body.y() + body.height() * 0.28, scale, body.height() * 0.44);
        painter.drawRect(terminal);
        painter.end();
        icon.addPixmap(QPixmap::fromImage(image));
    }
    return icon;
}

} // namespace ajazz::app
