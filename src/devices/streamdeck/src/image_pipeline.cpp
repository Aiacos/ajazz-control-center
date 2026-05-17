// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file image_pipeline.cpp
 * @brief Qt6 implementation of the AKP-family RGBA→JPEG/PNG pipeline (ARCH-04 / Phase 09).
 */
#include "image_pipeline.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageWriter>
#include <QTransform>

#include <cstring>
#include <stdexcept>
#include <string>

namespace ajazz::streamdeck {

namespace {

/**
 * @brief Apply the rotation + mirror dictated by @p transform to @p img.
 *
 * Rotation is rounded to the nearest 90° step; non-90° angles would require a costly
 * smooth-rotation pass that no AJAZZ device protocol needs. Mirror is horizontal-only
 * (the vendor protocols all use horizontal flips).
 */
QImage applyOrientation(QImage img, ImageTransform const& transform) {
    int rot = transform.rotationDegrees % 360;
    if (rot < 0) {
        rot += 360;
    }
    rot = ((rot + 45) / 90) * 90; // round to nearest 90°
    rot %= 360;
    if (rot != 0) {
        img = img.transformed(QTransform{}.rotate(rot), Qt::SmoothTransformation);
    }
    if (transform.mirror) {
        // QImage::mirrored() is available on Qt 6.0+ (deprecated in Qt 6.13 in
        // favour of `flipped(Qt::Orientations)`, but that replacement was only
        // added in Qt 6.9 and our CI matrix still pins Qt 6.8.3). Stay on
        // mirrored() until the CI Qt baseline bumps past 6.9 — the deprecation
        // warning only fires on the local Qt 6.11.1 dev box and is suppressed
        // by /external:W0 on MSVC + the deprecation annotation tolerance on
        // the GCC/Clang CI paths. Horizontal flip matches the AKP153 mirror
        // requirement documented in docs/protocols/streamdeck/akp153.md.
        img = img.mirrored(/*horizontally=*/true, /*vertically=*/false);
    }
    return img;
}

/**
 * @brief Encode @p img to JPEG or PNG bytes using @c QImageWriter.
 *
 * Quality is set ONCE before write — `QImageWriter::setQuality()` per Qt docs is
 * a soft hint for the underlying codec; for the libjpeg-turbo path it maps to the
 * Independent JPEG Group quality scale 1..100. The default of 85 matches what
 * mishamyrt/ajazz-sdk and 4ndv/opendeck-akp03 emit on their Rust paths.
 */
std::vector<std::uint8_t> encodeImage(QImage const& img, ImageTransform const& transform) {
    QByteArray buffer;
    QBuffer device(&buffer);
    if (!device.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("image_pipeline: QBuffer open failed");
    }

    QImageWriter writer(&device, transform.format == ImageFormat::Jpeg ? "jpeg" : "png");
    if (transform.format == ImageFormat::Jpeg) {
        int q = transform.jpegQuality;
        if (q < 1) {
            q = 1;
        }
        if (q > 100) {
            q = 100;
        }
        writer.setQuality(q);
    }
    if (!writer.write(img)) {
        throw std::runtime_error("image_pipeline: QImageWriter::write failed: " +
                                 writer.errorString().toStdString());
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(buffer.size()));
    std::memcpy(out.data(), buffer.constData(), out.size());
    return out;
}

void validateTarget(ImageTransform const& transform) {
    if (transform.targetWidth == 0 || transform.targetHeight == 0) {
        throw std::invalid_argument("image_pipeline: target dimensions must be non-zero");
    }
}

} // namespace

std::vector<std::uint8_t>
encodeForDevice(std::span<std::uint8_t const> rgbaBytes,
                std::uint16_t srcWidth,
                std::uint16_t srcHeight,
                ImageTransform const& transform) {
    validateTarget(transform);
    if (srcWidth == 0 || srcHeight == 0) {
        throw std::invalid_argument("image_pipeline: source dimensions must be non-zero");
    }
    auto const expected = static_cast<std::size_t>(srcWidth) * srcHeight * 4u;
    if (rgbaBytes.size() != expected) {
        throw std::invalid_argument("image_pipeline: rgbaBytes.size() != srcWidth * srcHeight * 4");
    }

    // Wrap the caller's RGBA buffer without copying (QImage non-owning ctor). copy() forces
    // a deep copy before we transform — required because the caller's span may outlive
    // shorter than our QImage processing chain (rotation/mirror produce new QImages anyway,
    // but `scaled()` returns a fresh QImage that re-reads the source on demand if we don't
    // detach now).
    QImage src(rgbaBytes.data(),
               srcWidth,
               srcHeight,
               static_cast<qsizetype>(srcWidth) * 4,
               QImage::Format_RGBA8888);
    QImage detached = src.copy();

    QImage scaled = detached.scaled(transform.targetWidth,
                                    transform.targetHeight,
                                    Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
    QImage oriented = applyOrientation(std::move(scaled), transform);

    // JPEG can't carry alpha; convert before encoding so the writer doesn't silently
    // fall back to a strategy that may differ across Qt platforms.
    QImage final = transform.format == ImageFormat::Jpeg
                       ? oriented.convertToFormat(QImage::Format_RGB888)
                       : std::move(oriented);
    return encodeImage(final, transform);
}

std::vector<std::uint8_t>
encodeSolid(core::Rgb color, ImageTransform const& transform) {
    validateTarget(transform);

    QImage img(transform.targetWidth, transform.targetHeight,
               transform.format == ImageFormat::Jpeg ? QImage::Format_RGB888
                                                     : QImage::Format_RGBA8888);
    img.fill(qRgb(color.r, color.g, color.b));

    return encodeImage(img, transform);
}

} // namespace ajazz::streamdeck
