// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file encoder_layout_renderer.hpp
 * @brief Built-in Elgato dial/touch-strip layout renderer ($X1/$A0/$A1/$B1/$B2/$C1).
 *
 * Pure QImage composition for the encoder feedback surface (the touch-strip
 * zone above each dial). The Stream Deck SDK "Dials & Touch Strip" guide
 * defines six built-in layouts a dial action selects via the manifest
 * `Encoder.layout` field or at runtime via `setFeedbackLayout`, and updates
 * item-by-item via `setFeedback`:
 *
 *   $X1 — icon (canvas) + title
 *   $A0 — title + full-width image canvas
 *   $A1 — title + icon left + value text right
 *   $B1 — $A1 + plain progress bar
 *   $B2 — $A1 + gradient progress bar
 *   $C1 — two icon+bar rows (e.g. an audio mixer pair)
 *
 * Feedback items follow the SDK shapes: a bare string/number shorthand
 * (`{"value": "42%"}`, `{"indicator": 55}`) or the object form
 * (`{"indicator": {"value": 55, "opacity": 1, "bar_fill_c": "#ff0000"}}`,
 * `{"title": {"value": "Volume", "color": "#ffffff"}}`). Unknown keys are
 * ignored. The icon item accepts a data URI or an ABSOLUTE file path — the
 * caller (PluginDeviceBridge) resolves plugin-relative paths first.
 *
 * Elgato renders these on a 200x100 logical zone; the AKP05 strip zone is
 * square (128x128, hardware-pinned 2026-05-31), so the renderer lays out
 * proportionally against an arbitrary target size instead of hardcoding
 * Elgato pixels. Rendering is crash-safe in headless test runs: text drawing
 * degrades gracefully when no font backend exists (mirrors compositeTitle).
 */
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>

namespace ajazz::app {

/**
 * @brief Render one encoder feedback surface.
 *
 * @param layoutId  "$X1"|"$A0"|"$A1"|"$B1"|"$B2"|"$C1". Unknown/empty ids fall
 *                  back to $X1 (the SDK default for dial actions).
 * @param feedback  Merged setFeedback item bag (see file doc for item shapes).
 *                  Recognised keys: "title", "value", "icon", "indicator",
 *                  "icon2", "indicator2" (the *2 variants feed $C1's second row).
 * @param target    Output size in pixels (the device zone, e.g. 128x128 AKP05).
 * @return          Opaque composed frame, target-sized, dark background.
 */
[[nodiscard]] QImage
renderEncoderLayout(QString const& layoutId, QJsonObject const& feedback, QSize target);

} // namespace ajazz::app
