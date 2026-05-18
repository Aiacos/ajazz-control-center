// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series_tft_pipeline.cpp
 * @brief Implementation of @ref aj_series_tft_pipeline.hpp.
 */
#include "aj_series_tft_pipeline.hpp"

#include "aj_series_protocol.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace ajazz::mouse {

namespace {

/// Compose the two-line text payload: HH:MM clock + "<dpi> DPI".
/// Caller-side helper kept in the .cpp so the header has no Qt-string dep.
[[nodiscard]] QString formatTimeLine(std::chrono::system_clock::time_point now) {
    auto const tt = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    if (::localtime_s(&local, &tt) != 0) {
        return QStringLiteral("--:--");
    }
#else
    if (::localtime_r(&tt, &local) == nullptr) {
        return QStringLiteral("--:--");
    }
#endif
    return QStringLiteral("%1:%2")
        .arg(local.tm_hour, 2, 10, QChar(u'0'))
        .arg(local.tm_min, 2, 10, QChar(u'0'));
}

} // namespace

QImage renderClockDpiFace(QSize panelSize,
                          std::chrono::system_clock::time_point now,
                          std::uint32_t activeDpi) {
    if (panelSize.isEmpty()) {
        return {};
    }
    // Format_RGB16 is the canonical RGB565 layout in Qt — bits
    //   15..11 R, 10..5 G, 4..0 B — exactly what the vendor firmware
    // expects when receiving via opcode 0x25. No per-pixel swap needed
    // because Qt 6 stores Format_RGB16 in native little-endian order.
    QImage frame(panelSize, QImage::Format_RGB16);
    frame.fill(QColor(20, 20, 24)); // dark grey — matches vendor's "dark"
                                    // background variant; the AJ-series
                                    // dock has a black bezel that hides
                                    // any anti-aliasing seam.

    QPainter painter(&frame);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(240, 240, 246));

    auto const timeText = formatTimeLine(now);
    QString const dpiText =
        (activeDpi == 0) ? QString{} : QStringLiteral("%1 DPI").arg(activeDpi);

    // Layout: clock takes the top half (large font), DPI label takes the
    // bottom (small font). Font sizes scale with panel height so the
    // same renderer works for 128x128 dock panels and 240x240 AJ159
    // panels without tweaking constants per device.
    int const clockPx = std::max(16, panelSize.height() / 3);
    int const dpiPx = std::max(10, panelSize.height() / 8);

    QFont clockFont;
    clockFont.setPixelSize(clockPx);
    clockFont.setWeight(QFont::Bold);
    painter.setFont(clockFont);
    QRect clockRect(0, 0, panelSize.width(),
                    dpiText.isEmpty() ? panelSize.height() : (2 * panelSize.height() / 3));
    painter.drawText(clockRect, Qt::AlignCenter, timeText);

    if (!dpiText.isEmpty()) {
        QFont dpiFont;
        dpiFont.setPixelSize(dpiPx);
        painter.setFont(dpiFont);
        painter.setPen(QColor(160, 200, 255)); // light blue — matches AJAZZ accent
        QRect dpiRect(0, 2 * panelSize.height() / 3, panelSize.width(),
                      panelSize.height() / 3);
        painter.drawText(dpiRect, Qt::AlignCenter, dpiText);
    }
    painter.end();
    return frame;
}

std::vector<std::vector<std::uint8_t>> encodeRgb565Chunks(QImage const& frame) {
    if (frame.isNull() || frame.format() != QImage::Format_RGB16) {
        return {};
    }
    auto const totalBytes =
        static_cast<std::size_t>(frame.width()) * static_cast<std::size_t>(frame.height()) * 2;
    if (totalBytes == 0) {
        return {};
    }
    std::vector<std::vector<std::uint8_t>> chunks;
    chunks.reserve((totalBytes + aj_series::kTftChunkPayloadBytes - 1) /
                   aj_series::kTftChunkPayloadBytes);

    // Walk the framebuffer scanline by scanline so we honour the per-row
    // bytesPerLine() stride (Qt may pad rows for alignment; we copy only
    // the meaningful prefix width()*2 bytes per row).
    std::vector<std::uint8_t> linearPixels;
    linearPixels.reserve(totalBytes);
    for (int y = 0; y < frame.height(); ++y) {
        std::uint8_t const* const row = frame.constScanLine(y);
        linearPixels.insert(linearPixels.end(), row,
                            row + static_cast<std::ptrdiff_t>(frame.width()) * 2);
    }

    std::size_t offset = 0;
    while (offset < linearPixels.size()) {
        std::size_t const n =
            std::min(linearPixels.size() - offset, aj_series::kTftChunkPayloadBytes);
        chunks.emplace_back(linearPixels.begin() + static_cast<std::ptrdiff_t>(offset),
                            linearPixels.begin() + static_cast<std::ptrdiff_t>(offset + n));
        offset += n;
    }
    return chunks;
}

} // namespace ajazz::mouse
