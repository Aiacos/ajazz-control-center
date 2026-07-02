// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_encoder_layout_renderer.cpp
 * @brief Unit tests for the built-in dial layout renderer ($X1/$A0/$A1/$B1/$B2/$C1).
 *
 * Pixel-presence smoke tests (no font backend in headless runs, so TEXT is not
 * asserted — only geometry-driven pixels: bars, icons, background).
 */
#include "encoder_layout_renderer.hpp"

#include <QBuffer>
#include <QImage>
#include <QJsonObject>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Count pixels in a horizontal band whose red channel dominates (the bar fill
/// is the accent red); used to assert bar presence + rough fill fraction.
int redDominantInBand(QImage const& img, int yTop, int yBottom) {
    int n = 0;
    for (int y = yTop; y < yBottom; ++y) {
        for (int x = 0; x < img.width(); ++x) {
            QColor const c = img.pixelColor(x, y);
            if (c.red() > 150 && c.red() > c.green() + 60 && c.red() > c.blue() + 60) {
                ++n;
            }
        }
    }
    return n;
}

/// A tiny solid-green icon as a base64 data URI.
QString greenIconDataUri() {
    QImage icon(8, 8, QImage::Format_ARGB32);
    icon.fill(QColor(0, 255, 0));
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    icon.save(&buf, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

int greenishCount(QImage const& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            QColor const c = img.pixelColor(x, y);
            if (c.green() > 180 && c.red() < 100 && c.blue() < 100) {
                ++n;
            }
        }
    }
    return n;
}

} // namespace

TEST_CASE("encoder_layout_renderer B1 bar fill scales with indicator value", "[encoder-layout]") {
    QJsonObject fb;
    fb.insert(QStringLiteral("indicator"), 100);
    QImage const full = ajazz::app::renderEncoderLayout(QStringLiteral("$B1"), fb, QSize(128, 128));
    REQUIRE_FALSE(full.isNull());
    CHECK(full.size() == QSize(128, 128));

    fb.insert(QStringLiteral("indicator"), 25);
    QImage const quarter =
        ajazz::app::renderEncoderLayout(QStringLiteral("$B1"), fb, QSize(128, 128));

    // The bar lives in the bottom band; full fill paints ~4x the red pixels.
    int const bandTop = 128 - 30;
    int const fullRed = redDominantInBand(full, bandTop, 128);
    int const quarterRed = redDominantInBand(quarter, bandTop, 128);
    REQUIRE(fullRed > 0);
    REQUIRE(quarterRed > 0);
    CHECK(fullRed > quarterRed * 2);
}

TEST_CASE("encoder_layout_renderer indicator absent draws no bar", "[encoder-layout]") {
    QImage const img =
        ajazz::app::renderEncoderLayout(QStringLiteral("$B1"), QJsonObject{}, QSize(128, 128));
    CHECK(redDominantInBand(img, 128 - 30, 128) == 0);
}

TEST_CASE("encoder_layout_renderer object-form indicator with custom fill colour",
          "[encoder-layout]") {
    QJsonObject ind;
    ind.insert(QStringLiteral("value"), 80);
    ind.insert(QStringLiteral("bar_fill_c"), QStringLiteral("#00ff00"));
    QJsonObject fb;
    fb.insert(QStringLiteral("indicator"), ind);
    QImage const img = ajazz::app::renderEncoderLayout(QStringLiteral("$B1"), fb, QSize(128, 128));
    // Custom green fill instead of the accent red.
    CHECK(greenishCount(img) > 50);
    CHECK(redDominantInBand(img, 128 - 30, 128) == 0);
}

TEST_CASE("encoder_layout_renderer X1 renders the icon item from a data URI", "[encoder-layout]") {
    QJsonObject fb;
    fb.insert(QStringLiteral("icon"), greenIconDataUri());
    QImage const img = ajazz::app::renderEncoderLayout(QStringLiteral("$X1"), fb, QSize(128, 128));
    CHECK(greenishCount(img) > 500); // icon scaled up to the canvas area
}

TEST_CASE("encoder_layout_renderer unknown layout falls back to X1 and never crashes",
          "[encoder-layout]") {
    QJsonObject fb;
    fb.insert(QStringLiteral("icon"), greenIconDataUri());
    QImage const a =
        ajazz::app::renderEncoderLayout(QStringLiteral("weird.json"), fb, QSize(128, 128));
    QImage const b = ajazz::app::renderEncoderLayout(QString{}, fb, QSize(128, 128));
    CHECK(greenishCount(a) > 500);
    CHECK(greenishCount(b) > 500);
    // Empty target size falls back to 128x128.
    QImage const c = ajazz::app::renderEncoderLayout(QStringLiteral("$X1"), fb, QSize());
    CHECK(c.size() == QSize(128, 128));
}

TEST_CASE("encoder_layout_renderer C1 renders two independent bars", "[encoder-layout]") {
    QJsonObject fb;
    fb.insert(QStringLiteral("indicator"), 100);
    fb.insert(QStringLiteral("indicator2"), 100);
    QImage const both = ajazz::app::renderEncoderLayout(QStringLiteral("$C1"), fb, QSize(128, 128));
    int const topRed = redDominantInBand(both, 0, 64);
    int const bottomRed = redDominantInBand(both, 64, 128);
    CHECK(topRed > 0);
    CHECK(bottomRed > 0);

    QJsonObject onlyFirst;
    onlyFirst.insert(QStringLiteral("indicator"), 100);
    QImage const one =
        ajazz::app::renderEncoderLayout(QStringLiteral("$C1"), onlyFirst, QSize(128, 128));
    CHECK(redDominantInBand(one, 0, 64) > 0);
    CHECK(redDominantInBand(one, 64, 128) == 0);
}
