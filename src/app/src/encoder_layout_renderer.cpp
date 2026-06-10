// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file encoder_layout_renderer.cpp
 * @brief Implementation of the built-in dial layout renderer (see header).
 *
 * COD-031: QtCore/QtGui only — no nlohmann::json in src/app.
 */
#include "encoder_layout_renderer.hpp"

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonValue>
#include <QLinearGradient>
#include <QPainter>
#include <QRect>

#include <algorithm>

namespace ajazz::app {

namespace {

/// Theme constants for the dark strip surface (match the app's dark+red palette).
constexpr QRgb kBackground = qRgb(0x14, 0x14, 0x1a);
constexpr QRgb kTextColor = qRgb(0xf0, 0xf0, 0xf0);
constexpr QRgb kBarBackground = qRgb(0x33, 0x33, 0x3a);
constexpr QRgb kBarBorder = qRgb(0x66, 0x66, 0x6e);
constexpr QRgb kBarFill = qRgb(0xe9, 0x4b, 0x4b);

/// True when a font backend exists (a QGuiApplication is up). Headless unit
/// tests run on QCoreApplication — QPainter::drawText would abort there, so
/// text drawing degrades to a no-op (same guard as compositeTitle).
bool canDrawText() {
    return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr &&
           !QFontDatabase::families().isEmpty();
}

/// SDK item shorthand: a feedback item is either a bare scalar or an object
/// whose payload lives under "value". textOf("title") accepts both
/// {"title":"Vol"} and {"title":{"value":"Vol"}}.
QString textOf(QJsonObject const& fb, QString const& key) {
    QJsonValue const v = fb.value(key);
    if (v.isObject()) {
        QJsonValue const inner = v.toObject().value(QStringLiteral("value"));
        return inner.isDouble() ? QString::number(inner.toDouble()) : inner.toString();
    }
    if (v.isDouble()) {
        return QString::number(v.toDouble());
    }
    return v.toString();
}

/// Numeric item (indicator value 0..100). Returns -1 when absent/invalid so
/// callers can distinguish "no bar" from "bar at 0".
double numOf(QJsonObject const& fb, QString const& key) {
    QJsonValue const v = fb.value(key);
    if (v.isObject()) {
        QJsonValue const inner = v.toObject().value(QStringLiteral("value"));
        return inner.isDouble() ? inner.toDouble() : -1.0;
    }
    return v.isDouble() ? v.toDouble() : -1.0;
}

/// Optional per-item colour override (e.g. indicator.bar_fill_c). Falls back
/// to @p fallback when absent or unparsable.
QColor colorOf(QJsonObject const& fb, QString const& key, QString const& sub, QColor fallback) {
    QJsonValue const v = fb.value(key);
    if (v.isObject()) {
        QColor const c(v.toObject().value(sub).toString());
        if (c.isValid()) {
            return c;
        }
    }
    return fallback;
}

/// Load the icon item: data URI ("data:image/...;base64,....") or an absolute
/// file path (the bridge resolves plugin-relative paths before merging).
QImage iconOf(QJsonObject const& fb, QString const& key) {
    QString const s = textOf(fb, key);
    if (s.isEmpty()) {
        return {};
    }
    if (s.startsWith(QStringLiteral("data:"))) {
        qsizetype const comma = s.indexOf(QLatin1Char(','));
        if (comma < 0) {
            return {};
        }
        QByteArray const raw =
            QByteArray::fromBase64(s.mid(comma + 1).toLatin1(), QByteArray::Base64Encoding);
        QImage img;
        img.loadFromData(raw);
        return img;
    }
    return QImage(s);
}

void drawTextIn(QPainter& p, QRect const& rect, QString const& text, int pixelSize, int flags) {
    if (text.isEmpty() || !canDrawText()) {
        return;
    }
    QFont f = p.font();
    f.setPixelSize(pixelSize);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(kTextColor));
    p.drawText(rect, flags, text);
}

void drawIconIn(QPainter& p, QRect const& rect, QImage const& icon) {
    if (icon.isNull()) {
        return;
    }
    QImage const scaled = icon.scaled(rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QRect dst(QPoint(0, 0), scaled.size());
    dst.moveCenter(rect.center());
    p.drawImage(dst, scaled);
}

/// Horizontal progress bar; value clamped to 0..100. @p gradient selects the
/// $B2 look (left-to-right hue ramp) over the flat $B1 fill.
void drawBarIn(QPainter& p,
               QRect const& rect,
               double value,
               bool gradient,
               QColor const& fill,
               QColor const& bg,
               QColor const& border) {
    double const v = std::clamp(value, 0.0, 100.0);
    p.setPen(border);
    p.setBrush(bg);
    p.drawRoundedRect(rect, 3, 3);
    QRect fillRect = rect.adjusted(2, 2, -2, -2);
    fillRect.setWidth(static_cast<int>(fillRect.width() * v / 100.0));
    if (fillRect.width() <= 0) {
        return;
    }
    p.setPen(Qt::NoPen);
    if (gradient) {
        QLinearGradient g(rect.topLeft(), rect.topRight());
        g.setColorAt(0.0, QColor(0x2e, 0xcc, 0x71)); // green
        g.setColorAt(0.5, QColor(0xf3, 0x9c, 0x12)); // amber
        g.setColorAt(1.0, QColor(0xe9, 0x4b, 0x4b)); // red
        p.setBrush(g);
    } else {
        p.setBrush(fill);
    }
    p.drawRoundedRect(fillRect, 2, 2);
}

} // namespace

QImage renderEncoderLayout(QString const& layoutId, QJsonObject const& fb, QSize target) {
    if (target.isEmpty()) {
        target = QSize(128, 128);
    }
    QImage img(target, QImage::Format_RGBA8888);
    img.fill(QColor(kBackground));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    int const w = target.width();
    int const h = target.height();
    int const pad = std::max(2, w / 24);
    int const titleH = h / 4;
    int const titlePx = std::max(8, h / 6);
    int const valuePx = std::max(10, h / 5);
    QRect const titleRect(pad, 0, w - 2 * pad, titleH);
    QString const title = textOf(fb, QStringLiteral("title"));
    QString const value = textOf(fb, QStringLiteral("value"));
    QImage const icon = iconOf(fb, QStringLiteral("icon"));

    QString const id = layoutId.isEmpty() ? QStringLiteral("$X1") : layoutId;

    if (id == QStringLiteral("$A0")) {
        // Title top + full-width image canvas underneath.
        drawTextIn(p, titleRect, title, titlePx, Qt::AlignHCenter | Qt::AlignVCenter);
        drawIconIn(p, QRect(pad, titleH, w - 2 * pad, h - titleH - pad), icon);
    } else if (id == QStringLiteral("$A1") || id == QStringLiteral("$B1") ||
               id == QStringLiteral("$B2")) {
        // Title top; icon left + value right; $B1/$B2 add the bar at the bottom.
        bool const hasBar = (id != QStringLiteral("$A1"));
        int const barH = hasBar ? std::max(8, h / 8) : 0;
        int const bodyTop = titleH;
        int const bodyH = h - titleH - (hasBar ? (barH + 2 * pad) : pad);
        drawTextIn(p, titleRect, title, titlePx, Qt::AlignHCenter | Qt::AlignVCenter);
        drawIconIn(p, QRect(pad, bodyTop, w / 2 - 2 * pad, bodyH), icon);
        drawTextIn(p,
                   QRect(w / 2, bodyTop, w / 2 - pad, bodyH),
                   value,
                   valuePx,
                   Qt::AlignHCenter | Qt::AlignVCenter);
        if (hasBar) {
            double const v = numOf(fb, QStringLiteral("indicator"));
            if (v >= 0) {
                drawBarIn(p,
                          QRect(pad, h - barH - pad, w - 2 * pad, barH),
                          v,
                          id == QStringLiteral("$B2"),
                          colorOf(fb,
                                  QStringLiteral("indicator"),
                                  QStringLiteral("bar_fill_c"),
                                  QColor(kBarFill)),
                          QColor(kBarBackground),
                          QColor(kBarBorder));
            }
        }
    } else if (id == QStringLiteral("$C1")) {
        // Two icon+bar rows (mixer pair): icon1+indicator1 / icon2+indicator2.
        int const rowH = (h - 3 * pad) / 2;
        int const iconW = std::min(rowH, w / 4);
        for (int row = 0; row < 2; ++row) {
            QString const iconKey = row == 0 ? QStringLiteral("icon") : QStringLiteral("icon2");
            QString const indKey =
                row == 0 ? QStringLiteral("indicator") : QStringLiteral("indicator2");
            int const top = pad + row * (rowH + pad);
            drawIconIn(p, QRect(pad, top, iconW, rowH), iconOf(fb, iconKey));
            double const v = numOf(fb, indKey);
            if (v >= 0) {
                int const barH = std::max(8, rowH / 3);
                drawBarIn(
                    p,
                    QRect(iconW + 2 * pad, top + (rowH - barH) / 2, w - iconW - 4 * pad, barH),
                    v,
                    false,
                    colorOf(fb, indKey, QStringLiteral("bar_fill_c"), QColor(kBarFill)),
                    QColor(kBarBackground),
                    QColor(kBarBorder));
            }
        }
    } else {
        // $X1 (and any unknown id): centred icon canvas + title overlay.
        drawIconIn(p, QRect(pad, titleH, w - 2 * pad, h - titleH - pad), icon);
        drawTextIn(p, titleRect, title, titlePx, Qt::AlignHCenter | Qt::AlignVCenter);
        if (icon.isNull()) {
            // No icon yet (fresh mount): show the value, if any, as the canvas.
            drawTextIn(p,
                       QRect(pad, titleH, w - 2 * pad, h - titleH - pad),
                       value,
                       valuePx,
                       Qt::AlignHCenter | Qt::AlignVCenter);
        }
    }

    p.end();
    return img;
}

} // namespace ajazz::app
