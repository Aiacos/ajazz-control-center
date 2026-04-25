// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_icon.hpp
 * @brief Build a multi-resolution QIcon from the canonical app SVG.
 */
#pragma once

#include <QIcon>
#include <QString>

namespace ajazz::app {

/**
 * @brief Rasterize the canonical app SVG into a QIcon containing one pixmap
 *        per size that Qt actually consumes.
 *
 * The same artwork shipped as the README hero (resources/branding/app.svg) is
 * the single source of truth. Each target size is rendered natively by
 * QSvgRenderer instead of letting QIcon downscale a single high-res raster:
 * the system tray (16-24 px), the window decoration (32-64 px) and the About
 * dialog (128-512 px) each get a pixmap drawn at its actual pixel grid, which
 * keeps edges crisp at every surface — especially the tray, where blurry
 * downscaling of a 256 px source previously made the icon look invisible
 * against dark Wayland panels.
 *
 * @param svgResourcePath Qt resource path of the SVG (":/...").
 * @param fallbackResourcePath Optional second SVG tried if the first is
 *                             invalid (e.g. AJAZZ_BRAND_DIR overrides that
 *                             drop the branded asset).
 * @return Multi-pixmap QIcon, or an empty QIcon if both sources fail.
 */
[[nodiscard]] QIcon makeAppIcon(QString const& svgResourcePath,
                                QString const& fallbackResourcePath = {});

} // namespace ajazz::app
