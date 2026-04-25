// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_icon.cpp
 * @brief Implementation of @ref ajazz::app::makeAppIcon.
 */
#include "app_icon.hpp"

#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>
#include <Qt>

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

} // namespace ajazz::app
