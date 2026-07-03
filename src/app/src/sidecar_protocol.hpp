// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sidecar_protocol.hpp
 * @brief Pure encode/decode for the streamdock-host sidecar wire protocol.
 *
 * The Rust sidecar (`streamdock-host/`, built on the mirajazz crate) owns the
 * AKP05/N4 HID device out-of-process and speaks newline-delimited JSON over
 * stdin/stdout. This header holds the **pure** (Qt6::Core-only, no QProcess,
 * no QObject) half of the C++ side: command encoders that produce one JSON
 * line, and an event parser that turns one JSON line into a typed struct.
 *
 * Keeping this layer pure makes it unit-testable without spawning a process —
 * mirroring the `node_runner` NodeProbe pattern. The QProcess lifecycle and
 * the (code,state) -> core::DeviceEvent mapping live in the device layer
 * (SidecarStreamDockDevice, Slice 3b), not here.
 *
 * COD-031: Qt-Core only, no nlohmann::json (QJsonDocument is used).
 */
#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::app::sidecar {

// -----------------------------------------------------------------------------
// Command encoders — each returns one newline-terminated JSON line for stdin.
// -----------------------------------------------------------------------------

/// `{"cmd":"ping"}` — liveness probe; sidecar replies with a Pong event.
[[nodiscard]] QByteArray buildPing();

/// `{"cmd":"set_brightness","serial":..,"percent":..}` (0..100, clamped).
[[nodiscard]] QByteArray buildSetBrightness(QString const& serial, std::uint8_t percent);

/// `{"cmd":"keep_alive","serial":..}` — sidecar sends mirajazz keep_alive() (CRT CONNECT) to
/// hold the persistent handle alive while idle (prevents the panel wedging).
[[nodiscard]] QByteArray buildKeepAlive(QString const& serial);

/**
 * @brief `{"cmd":"set_image",...}` with the RGBA payload base64-encoded.
 *
 * The sidecar (mirajazz) handles resize / Rot180 / JPEG encoding, so the C++
 * side ships raw RGBA8 exactly as IDisplayCapable::setKeyImage receives it.
 *
 * @param serial    Target device serial.
 * @param key       0-based mirajazz hardware index (zones 0..3, keys 5..14).
 * @param touchzone true to use the touch-zone format (128x128) vs key (112x112).
 * @param width     Source image width.
 * @param height    Source image height.
 * @param rgba      Tightly packed RGBA8, length == width*height*4.
 */
[[nodiscard]] QByteArray buildSetImage(QString const& serial,
                                       std::uint8_t key,
                                       bool touchzone,
                                       std::uint16_t width,
                                       std::uint16_t height,
                                       std::span<std::uint8_t const> rgba);

/// `{"cmd":"render_test","serial":..}` — one-shot visual self-test.
[[nodiscard]] QByteArray buildRenderTest(QString const& serial);

// -----------------------------------------------------------------------------
// Event parsing — one JSON line in, one typed event out.
// -----------------------------------------------------------------------------

/// Decoded form of one sidecar -> app JSON line.
struct SidecarEvent {
    enum class Type : std::uint8_t {
        Connected,    ///< Device opened; `serial`, `firmware`, `vid`, `pid` set.
        Disconnected, ///< Device removed; `serial` set.
        Ready,        ///< Enumeration done; `deviceCount` set.
        Input,        ///< Raw input frame; `code`/`state`/`rawHex` set.
        Pong,         ///< Reply to ping.
        Ok,           ///< Command acknowledged.
        Error,        ///< Something failed; `message` set.
        Unknown,      ///< Unrecognised "event" value.
    };

    Type type{Type::Unknown};
    QString serial;   ///< Device serial (Connected/Disconnected/Input).
    QString firmware; ///< Firmware string (Connected).
    QString message;  ///< Human message (Error / generic).
    QString rawHex;   ///< First-16-byte hex of the input frame (Input).
    std::uint16_t vid{0};
    std::uint16_t pid{0};
    int deviceCount{0};    ///< Devices announced (Ready).
    std::uint8_t code{0};  ///< mirajazz byte9 (Input).
    std::uint8_t state{0}; ///< mirajazz byte10 (Input).
};

/**
 * @brief Parse one JSON line emitted by the sidecar.
 * @param jsonLine A single line (trailing newline tolerated).
 * @return The decoded event, or std::nullopt if the line is not a JSON object
 *         with a string "event" field.
 */
[[nodiscard]] std::optional<SidecarEvent> parseEvent(QByteArray const& jsonLine);

} // namespace ajazz::app::sidecar
