// SPDX-License-Identifier: GPL-3.0-or-later
/** @file akp05.cpp
 *  @brief AJAZZ AKP05 / AKP05E "Stream Dock Plus"-class device backend.
 *
 *  Implements the full wire protocol for the AKP05 family: 10 keys in a 2×5
 *  grid (85×85 JPEG, reusing the AKP153 image format), 4 endless rotary
 *  encoders each with a dedicated 100×100 LCD above them, one capacitive
 *  touch strip, and one 800×100 main LCD strip (full panel is 800×480).
 *
 *  The protocol is a clean-room reconstruction from the notes in
 *  docs/protocols/streamdeck/akp05.md; no third-party source is incorporated.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp05_protocol.hpp"
#include "image_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <string>

namespace ajazz::streamdeck {

namespace akp05 {

// -----------------------------------------------------------------------------
// Protocol helpers — pure functions, covered by unit tests.
// -----------------------------------------------------------------------------

/** @brief Construct a zero-initialised 1024-byte command packet with the
 *         standard AKP prefix and the given three-byte ASCII command word.
 *
 *  The AKP framing layout is:
 *  - Bytes 0–2:  "CRT" prefix (0x43 0x52 0x54)
 *  - Bytes 3–4:  0x00 0x00
 *  - Bytes 5–7:  @p cmd (three ASCII command bytes)
 *  - Bytes 8–511: zero-padded payload area (filled by callers)
 *
 *  @param cmd  Three-byte ASCII command identifier (e.g. CmdLight, CmdClear).
 *  @return     A fully initialised command packet ready for payload injection.
 */
std::array<std::uint8_t, PacketSize> buildCmdHeader(std::array<std::uint8_t, 3> const& cmd) {
    std::array<std::uint8_t, PacketSize> pkt{};
    pkt[0] = CmdPrefix[0];
    pkt[1] = CmdPrefix[1];
    pkt[2] = CmdPrefix[2];
    pkt[5] = cmd[0];
    pkt[6] = cmd[1];
    pkt[7] = cmd[2];
    return pkt;
}

/** @brief Build a backlight brightness command packet.
 *
 *  Sets the global brightness of all key LCDs and the main LCD.
 *  Byte 10 carries the clamped brightness value (0–100).
 *
 *  @param percent  Desired brightness in the range 0–100; values above 100
 *                  are silently clamped to 100.
 *  @return         Ready-to-send 1024-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent) {
    auto pkt = buildCmdHeader(CmdLight);
    pkt[10] = std::min<std::uint8_t>(percent, 100);
    return pkt;
}

/** @brief Build a "clear all keys" command packet.
 *
 *  Instructs the firmware to black-out every key LCD simultaneously.
 *  Byte 10 = 0x00, byte 11 = 0xFF (broadcast sentinel).
 *
 *  @return Ready-to-send 1024-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildClearAll() {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = 0xff;
    return pkt;
}

/** @brief Build a "clear single key" command packet.
 *
 *  Instructs the firmware to black-out one specific key LCD.
 *  Byte 10 = 0x00, byte 11 = @p keyIndex.
 *
 *  @param keyIndex  1-based key index (1..KeyCount).
 *  @return          Ready-to-send 1024-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex) {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = keyIndex;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildVersionRequest() {
    return buildCmdHeader(CmdVersion);
}

std::array<std::uint8_t, PacketSize> buildSecondaryScreenHeader(std::uint8_t location,
                                                                std::uint16_t width,
                                                                std::uint16_t height,
                                                                std::uint16_t x,
                                                                std::uint16_t y,
                                                                std::uint32_t jpegSize) {
    auto pkt = buildCmdHeader(CmdSecondaryScreen);
    // BE32 JPEG payload size at bytes 8..11.
    pkt[8] = static_cast<std::uint8_t>((jpegSize >> 24) & 0xffu);
    pkt[9] = static_cast<std::uint8_t>((jpegSize >> 16) & 0xffu);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = location;
    // BE16 rect width / height / x / y at bytes 13..20.
    pkt[13] = static_cast<std::uint8_t>((width >> 8) & 0xffu);
    pkt[14] = static_cast<std::uint8_t>(width & 0xffu);
    pkt[15] = static_cast<std::uint8_t>((height >> 8) & 0xffu);
    pkt[16] = static_cast<std::uint8_t>(height & 0xffu);
    pkt[17] = static_cast<std::uint8_t>((x >> 8) & 0xffu);
    pkt[18] = static_cast<std::uint8_t>(x & 0xffu);
    pkt[19] = static_cast<std::uint8_t>((y >> 8) & 0xffu);
    pkt[20] = static_cast<std::uint8_t>(y & 0xffu);
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildUploadFinished() {
    // ULEND is the only AKP-family opcode (alongside QUCMD) with a 5-byte
    // command word: bytes 5..9 = "ULEND". The bytes 0..2 still carry the
    // standard "CRT" prefix; bytes 3..4 stay zero per the family convention.
    std::array<std::uint8_t, PacketSize> pkt{};
    pkt[0] = CmdPrefix[0];
    pkt[1] = CmdPrefix[1];
    pkt[2] = CmdPrefix[2];
    pkt[5] = UploadFinishedMarker[0];
    pkt[6] = UploadFinishedMarker[1];
    pkt[7] = UploadFinishedMarker[2];
    pkt[8] = UploadFinishedMarker[3];
    pkt[9] = UploadFinishedMarker[4];
    return pkt;
}

/** @brief Build the header packet for a key-image transfer.
 *
 *  The firmware expects one header packet followed immediately by one or more
 *  1024-byte raw JPEG data packets.  The header encodes:
 *  - Bytes 10–11: big-endian total JPEG byte count
 *  - Byte 12:     firmware wire key byte (callers map the 1-based logical index
 *                 through akp05KeyWire() first; see that helper).
 *
 *  @param keyIndex  Firmware wire key byte (see akp05KeyWire()).
 *  @param jpegSize  Total byte length of the JPEG payload (≤ 0xFFFF).
 *  @return          Ready-to-send 1024-byte header packet.
 */
std::array<std::uint8_t, PacketSize> buildKeyImageHeader(std::uint8_t keyIndex,
                                                         std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdKeyImage);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = keyIndex;
    return pkt;
}

/** @brief Build the header packet for an encoder-LCD image transfer.
 *
 *  Each of the 4 encoders has a small 100×100 JPEG LCD.  The header layout
 *  mirrors the key-image header but uses CmdEncImage and places the
 *  encoder index (0-based) at byte 12.
 *
 *  @param encoderIndex  0-based encoder index (0..EncoderCount-1).
 *  @param jpegSize      Total byte length of the JPEG payload (≤ 0xFFFF).
 *  @return              Ready-to-send 1024-byte header packet.
 */
std::array<std::uint8_t, PacketSize> buildEncoderImageHeader(std::uint8_t encoderIndex,
                                                             std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdEncImage);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = encoderIndex;
    return pkt;
}

/** @brief Build the header packet for a main-LCD image transfer.
 *
 *  The 800×100 main LCD (bottom of the unit) accepts a full-width JPEG.
 *  No index byte is needed — the CmdMainImage command targets it implicitly.
 *  Bytes 10–11 carry the big-endian JPEG size.
 *
 *  @param jpegSize  Total byte length of the JPEG payload (≤ 0xFFFF).
 *  @return          Ready-to-send 1024-byte header packet.
 */
std::array<std::uint8_t, PacketSize> buildMainImageHeader(std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdMainImage);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    return pkt;
}

/** @brief Build the header packet for a firmware boot-logo upload.
 *
 *  Per vendor RE (akp05_vendor.md §2 row 188, SDDevice::sendLogoSizeCommand at
 *  0x180023a70): emits the "LOG" command word with the total JPEG size at
 *  bytes 10..11 as BE16, mirroring buildKeyImageHeader. The JPEG payload
 *  follows in 1024-byte chunks through the normal sendImage() path, then the
 *  ULEND commit sentinel.
 *
 *  @param jpegSize  Total byte length of the JPEG payload (capped at 0xFFFF;
 *                   the on-wire size field is 16 bits).
 *  @return          Ready-to-send 1024-byte header packet.
 */
std::array<std::uint8_t, PacketSize> buildLogoSizeHeader(std::uint32_t jpegSize) {
    auto pkt = buildCmdHeader(CmdLogo);
    auto const capped = std::min<std::uint32_t>(jpegSize, 0xFFFFu);
    pkt[10] = static_cast<std::uint8_t>((capped >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(capped & 0xffu);
    return pkt;
}

/** @brief Decode one raw HID input report into a structured @ref InputEvent.
 *
 *  The AKP05 multiplexes keys, encoders, and touch-strip events over a single
 *  512-byte HID report.  The tag byte at offset 9 determines the event class:
 *
 *  | report[9]     | Class                  | Notes                              |
 *  |--------------|------------------------|------------------------------------|
 *  | 1..KeyCount   | Key press/release      | report[10] = edge (non-zero = down)|
 *  | ActionEncoder*| Encoder rotation/press | code carries dir+index; no delta   |
 *  | ActionTouch*  | Touch down/move/up     | report[10] = X (0..255); no gesture|
 *
 *  ACK frames (bytes 0–2 == "ACK") are silently discarded.
 *
 *  @param frame  Raw HID report bytes (must be at least 16 bytes).
 *  @return       Decoded event, or @c std::nullopt for ACK / unknown frames.
 */
std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame) {
    if (frame.size() < 16) {
        return std::nullopt;
    }

    // ACK frames.
    if (frame[0] == 0x41 && frame[1] == 0x43 && frame[2] == 0x4b) {
        return std::nullopt;
    }

    auto const tag = frame[9];

    // Key events: 1-based key index 1..KeyCount. Byte 10 = press/release edge.
    if (tag >= 1 && tag <= KeyCount) {
        bool const pressed = frame[10] != 0x00;
        InputEvent ev{};
        ev.kind = pressed ? InputEvent::Kind::KeyPressed : InputEvent::Kind::KeyReleased;
        ev.index = tag;
        return ev;
    }

    // Encoder rotation/press. Per vendor RE (akp05_input_corrections.md §3,
    // handleKeyEvents @0x1400d02b0) the report carries NO rotation-magnitude
    // byte: direction AND encoder identity are BOTH encoded by the report[9]
    // action code, and one report == exactly one detent (value = ±1). Codes
    // CONVERGE across the 2026-05-27 Ghidra jump-table (DAT_1400d9ef4) and
    // opendeck-akp05's inputs.rs; per-encoder index + CW/CCW polarity follow
    // opendeck's convention and stay [PROVISIONAL] until a retail unit confirms
    // them (the 0x3004 demo unit's input path is stubbed — §7.1, re-confirmed
    // live 2026-05-29). Structure mirrors akp03::parseInputReport.
    auto const decodeRotation =
        [](std::uint8_t code) -> std::optional<std::pair<std::uint8_t, std::int8_t>> {
        switch (code) {
        case ActionEncoder0Ccw:
            return std::pair{std::uint8_t{0}, std::int8_t{-1}};
        case ActionEncoder0Cw:
            return std::pair{std::uint8_t{0}, std::int8_t{+1}};
        case ActionEncoder1Ccw:
            return std::pair{std::uint8_t{1}, std::int8_t{-1}};
        case ActionEncoder1Cw:
            return std::pair{std::uint8_t{1}, std::int8_t{+1}};
        case ActionEncoder2Ccw:
            return std::pair{std::uint8_t{2}, std::int8_t{-1}};
        case ActionEncoder2Cw:
            return std::pair{std::uint8_t{2}, std::int8_t{+1}};
        case ActionEncoder3Ccw:
            return std::pair{std::uint8_t{3}, std::int8_t{-1}};
        case ActionEncoder3Cw:
            return std::pair{std::uint8_t{3}, std::int8_t{+1}};
        default:
            return std::nullopt;
        }
    };
    if (auto const rot = decodeRotation(tag)) {
        if (rot->first >= EncoderCount) {
            return std::nullopt;
        }
        InputEvent ev{};
        ev.kind = InputEvent::Kind::EncoderTurned;
        ev.index = rot->first;
        ev.value = rot->second; // ±1 step, no magnitude byte (§3)
        return ev;
    }

    auto const decodePress = [](std::uint8_t code) -> std::optional<std::uint8_t> {
        switch (code) {
        case ActionEncoder0Press:
            return std::uint8_t{0};
        case ActionEncoder1Press:
            return std::uint8_t{1};
        case ActionEncoder2Press:
            return std::uint8_t{2};
        case ActionEncoder3Press:
            return std::uint8_t{3};
        default:
            return std::nullopt;
        }
    };
    if (auto const encIndex = decodePress(tag)) {
        if (*encIndex >= EncoderCount) {
            return std::nullopt;
        }
        InputEvent ev{};
        ev.index = *encIndex;
        // The wire is press-dominant; report[10] carries the edge when the
        // firmware supports it (opendeck reads `state != 0` as pressed). Treat
        // byte 10 == 0 as a release so the synthesis path stays uniform with the
        // key branch; the input service also synthesises a release on press.
        ev.kind = (frame[10] == 0x00) ? InputEvent::Kind::EncoderReleased
                                      : InputEvent::Kind::EncoderPressed;
        return ev;
    }

    // Touch strip. CONFIRMED by the vendor decompile (handleKeyEvents @0x1400d02b0,
    // akp05_input_corrections.md §4): the firmware emits only raw down/move/up
    // (report[9] == 0x98/0x97/0x99) with the touch X as the SINGLE byte report[10]
    // (0..255 — NOT a BE16 over [10..11]). Tap/swipe/long-press do NOT exist on
    // the wire; they are host-side gestures synthesised from the down->up X delta
    // by StreamDockInputService. (0x78/0x79 setCoreX and 0xB1/0xB2 N4-Pro
    // touchbar-mode toggles are not surfaced as input events.)
    auto const touchKind = [](std::uint8_t code) -> std::optional<InputEvent::Kind> {
        switch (code) {
        case ActionTouchDown:
            return InputEvent::Kind::TouchDown;
        case ActionTouchMove:
            return InputEvent::Kind::TouchMove;
        case ActionTouchUp:
            return InputEvent::Kind::TouchUp;
        default:
            return std::nullopt;
        }
    };
    if (auto const kind = touchKind(tag)) {
        InputEvent ev{};
        ev.kind = *kind;
        ev.value = static_cast<std::int16_t>(frame[10]); // single-byte X, 0..255 (§4)
        return ev;
    }

    return std::nullopt;
}

std::optional<std::string> parseVersionResponse(std::span<std::uint8_t const> frame) {
    // The device answers buildVersionRequest() not on the interrupt-IN endpoint
    // but via a HID GET_REPORT pull (readFeature). The response is a leading
    // report-id byte (0x00) followed by an ASCII version string such as
    // "V3.AKP05E.01.007", NUL-terminated and zero-padded. Confirmed on a
    // physical AKP05E 2026-05-20. Skip leading non-printable bytes (the
    // report-id), collect the printable ASCII run, and stop at the first
    // non-printable byte that follows it.
    std::string out;
    bool started = false;
    for (auto const b : frame) {
        if (b >= 0x20 && b < 0x7f) {
            out.push_back(static_cast<char>(b));
            started = true;
        } else if (started) {
            break;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    if (out.empty()) {
        return std::nullopt;
    }
    return out;
}

} // namespace akp05

namespace {

using namespace ajazz::core;

// File-static once_flag (Pitfall 14): WARN emits at most once per process
// lifetime for this backend. Distinct flag — not shared across backends.
std::once_flag s_warned_akp05;

// ARCH-04 image-pipeline transforms for the three AKP05 display surfaces. Pulled
// out as free helpers so the per-call setKeyImage / setEncoderImage / setMainImage
// bodies stay one-liners and the geometry/orientation/quality constants live in
// exactly one place.
inline ImageTransform akp05KeyTransform() noexcept {
    return ImageTransform{
        .targetWidth = akp05::KeyWidthPx,
        .targetHeight = akp05::KeyHeightPx,
        .format = ImageFormat::Jpeg,
        // Rot180: the AKP05E panel mounts the key LCDs inverted, so each key image
        // must be pre-rotated 180° to read upright — hardware-confirmed 2026-05-31
        // on 0x0300:0x3004 (digits rendered upside-down at 0°; upright at 180°).
        // Per-image rotation only; key ORDER is unaffected (037bd8d wire map).
        // Matches the sibling AKP815 Rot180 key convention (image_pipeline.hpp).
        // The RE is silent on key-image orientation (vendor 'setRotation' QUCMD
        // undecoded); the live device is the source of truth here.
        .rotationDegrees = 180,
        .mirror = false,
        .jpegQuality = 85,
    };
}
inline ImageTransform akp05EncoderTransform() noexcept {
    return ImageTransform{
        .targetWidth = akp05::EncoderScreenWidthPx,
        .targetHeight = akp05::EncoderScreenHeightPx,
        .format = ImageFormat::Jpeg,
        // Rot180 like the keys — the strip zones share the panel's inverted mount
        // (hardware-confirmed 2026-05-31 on 0x0300:0x3004).
        .rotationDegrees = 180,
        .mirror = false,
        .jpegQuality = 85,
    };
}
inline ImageTransform akp05MainTransform() noexcept {
    return ImageTransform{
        .targetWidth = akp05::MainDisplayWidthPx,
        .targetHeight = akp05::MainDisplayHeightPx,
        .format = ImageFormat::Jpeg,
        .rotationDegrees = 0,
        .mirror = false,
        .jpegQuality = 85,
    };
}

/** @brief Concrete IDevice implementation for the AJAZZ AKP05 / AKP05E.
 *
 *  @class Akp05Device
 *
 *  Glues the stateless @c akp05:: protocol helpers to the HID transport layer
 *  and exposes the full @ref IDisplayCapable and @ref IEncoderCapable mix-in
 *  interfaces.  All public methods are thread-safe with respect to the event
 *  callback registration and the cached firmware version (both guarded by
 *  @c m_mutex); individual writes to the transport are serialised by the caller
 *  because Qt's event loop drives @c poll() from a single thread.
 *
 *  Hardware capabilities:
 *  - 10 keys arranged in a 2×5 grid, each with an 85×85 JPEG LCD
 *  - 4 endless rotary encoders with 100×100 JPEG LCDs
 *  - One capacitive touch strip (X range 0–639)
 *  - One 800×100 main LCD strip (full panel is 800×480)
 */
class Akp05Device final : public IDevice,
                          public IDisplayCapable,
                          public IEncoderCapable,
                          public IClockCapable,
                          public IBootLogoCapable,
                          public ITouchStripDisplayCapable {
public:
    Akp05Device(DeviceDescriptor descriptor, DeviceId id)
        : Akp05Device(std::move(descriptor),
                      id,
                      makeHidTransport(id.vendorId,
                                       id.productId,
                                       id.serial,
                                       /*usagePage*/ 0,
                                       /*usage*/ 0,
                                       /*prependReportIdPosix*/ true)) {}

    /// Test constructor with injected transport (COD-026).
    Akp05Device(DeviceDescriptor descriptor, DeviceId id, TransportPtr transport)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(std::move(transport)) {}

    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string firmwareVersion() const override {
        // m_firmwareVersion is written by probeFirmwareVersion() on the I/O
        // thread (via open()) and may be read here from the UI thread; guard
        // the std::string against a torn read / reallocating write (WR-03).
        std::lock_guard const lock(m_mutex);
        return m_firmwareVersion;
    }

    void open() override {
        if (m_transport->isOpen()) {
            return;
        }
        m_transport->open();
        probeFirmwareVersion();
        AJAZZ_LOG_INFO("akp05", "device opened: {} (fw {})", m_descriptor.model, m_firmwareVersion);
    }

    void close() override {
        if (!m_transport->isOpen()) {
            return;
        }
        try {
            auto const stop = akp05::buildCmdHeader(akp05::CmdStop);
            (void)m_transport->write(stop);
        } catch (...) { /* best-effort */
        }
        m_transport->close();
    }

    [[nodiscard]] bool isOpen() const noexcept override { return m_transport->isOpen(); }

    void onEvent(EventCallback cb) override {
        std::lock_guard const lock(m_mutex);
        m_callback = std::move(cb);
    }

    std::size_t poll() override {
        std::array<std::uint8_t, akp05::PacketSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 8; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            auto ev = akp05::parseInputReport({buf.data(), n});
            if (!ev) {
                continue;
            }
            DeviceEvent devEv{};
            devEv.index = ev->index;
            devEv.value = ev->value;
            switch (ev->kind) {
            case akp05::InputEvent::Kind::KeyPressed:
                devEv.kind = DeviceEvent::Kind::KeyPressed;
                break;
            case akp05::InputEvent::Kind::KeyReleased:
                devEv.kind = DeviceEvent::Kind::KeyReleased;
                break;
            case akp05::InputEvent::Kind::EncoderTurned:
                devEv.kind = DeviceEvent::Kind::EncoderTurned;
                break;
            case akp05::InputEvent::Kind::EncoderPressed:
                devEv.kind = DeviceEvent::Kind::EncoderPressed;
                devEv.value = 1;
                break;
            case akp05::InputEvent::Kind::EncoderReleased:
                devEv.kind = DeviceEvent::Kind::EncoderReleased;
                devEv.value = 0;
                break;
            // Raw touch events pass straight through with X in `value`. Tap vs
            // swipe is synthesised host-side by StreamDockInputService from the
            // down->up X delta (the firmware has no gesture concept — §4).
            case akp05::InputEvent::Kind::TouchDown:
                devEv.kind = DeviceEvent::Kind::TouchDown;
                break;
            case akp05::InputEvent::Kind::TouchMove:
                devEv.kind = DeviceEvent::Kind::TouchMove;
                break;
            case akp05::InputEvent::Kind::TouchUp:
                devEv.kind = DeviceEvent::Kind::TouchUp;
                break;
            }

            EventCallback cb;
            {
                std::lock_guard const lock(m_mutex);
                cb = m_callback;
            }
            if (cb) {
                cb(devEv);
            }
            ++emitted;
        }
        return emitted;
    }

    // ---- IDisplayCapable ----------------------------------------------------
    [[nodiscard]] DisplayInfo displayInfo() const noexcept override {
        // Stream-Dock-Plus class: 2×5 LCD-key grid. Encoder LCDs and the
        // touchscreen strip are addressed via separate sendImage paths
        // (`buildEncoderImageHeader`, `buildMainImageHeader`) — they are
        // intentionally not exposed via DisplayInfo because the API contract
        // there is per-key-grid only.
        return DisplayInfo{
            .widthPx = akp05::KeyWidthPx,
            .heightPx = akp05::KeyHeightPx,
            .keyRows = akp05::KeyRows,
            .keyCols = akp05::KeyCols,
            .jpegEncoded = true,
        };
    }

    /// Validate a 1-based key index against the AKP05's 10-key geometry,
    /// logging + returning false on an out-of-range value (WR-02). Shared by
    /// setKeyImage / setKeyColor / clearKey.
    [[nodiscard]] static bool keyIndexInRange(std::uint8_t keyIndex) {
        if (keyIndex >= 1U && keyIndex <= akp05::KeyCount) {
            return true;
        }
        AJAZZ_LOG_WARN("akp05",
                       "key index {} out of range 1..{}; refusing",
                       static_cast<int>(keyIndex),
                       static_cast<int>(akp05::KeyCount));
        return false;
    }

    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        // WR-02: keys are 1-based 1..KeyCount; reject out-of-range before
        // shipping a bogus index to firmware (mirrors the parser's range check).
        if (!keyIndexInRange(keyIndex)) {
            return;
        }
        // ARCH-04: caller passes RGBA8 at any resolution per IDisplayCapable contract;
        // backend resizes to the device's native 85×85 and JPEG-encodes host-side.
        // The 1-based logical index is mapped to the firmware wire byte (the
        // AKP05E addresses keys non-linearly — akp05KeyWire(), commit 037bd8d).
        auto const jpeg = encodeForDevice(rgba, width, height, akp05KeyTransform());
        auto const sized = static_cast<std::uint16_t>(std::min<std::size_t>(jpeg.size(), 0xffff));
        sendImage(akp05::buildKeyImageHeader(akp05::akp05KeyWire(keyIndex), sized), jpeg);
    }

    void setKeyColor(std::uint8_t keyIndex, Rgb color) override {
        if (!keyIndexInRange(keyIndex)) {
            return;
        }
        // ARCH-04: synthesise a solid-color JPEG at native dimensions and ship via
        // the standard key-image path. The 1×1 source is upscaled cheaply by
        // QImage::scaled inside encodeSolid. Logical index -> wire byte (037bd8d).
        auto const jpeg = encodeSolid(color, akp05KeyTransform());
        auto const sized = static_cast<std::uint16_t>(std::min<std::size_t>(jpeg.size(), 0xffff));
        sendImage(akp05::buildKeyImageHeader(akp05::akp05KeyWire(keyIndex), sized), jpeg);
    }

    void clearKey(std::uint8_t keyIndex) override {
        // 0xff is the deliberate "clear all" broadcast sentinel — keep it; any
        // other out-of-range index is rejected (WR-02).
        if (keyIndex != 0xffU && !keyIndexInRange(keyIndex)) {
            return;
        }
        // Map the 1-based logical index to the firmware wire byte (037bd8d);
        // the 0xff clear-all sentinel must NOT be remapped.
        auto const pkt = (keyIndex == 0xff) ? akp05::buildClearAll()
                                            : akp05::buildClearKey(akp05::akp05KeyWire(keyIndex));
        (void)m_transport->write(pkt);
    }

    void setMainImage(std::span<std::uint8_t const> rgba,
                      std::uint16_t width,
                      std::uint16_t height) override {
        // ARCH-04: 800×100 main LCD strip. Caller passes RGBA8 at any resolution;
        // backend resizes to native + JPEG-encodes. Used by host-rendered clock
        // widgets and any user-driven main-strip imagery.
        auto const jpeg = encodeForDevice(rgba, width, height, akp05MainTransform());
        auto const sized = static_cast<std::uint16_t>(std::min<std::size_t>(jpeg.size(), 0xffff));
        sendImage(akp05::buildMainImageHeader(sized), jpeg);
    }

    void setBrightness(std::uint8_t percent) override {
        auto const pkt = akp05::buildSetBrightness(percent);
        (void)m_transport->write(pkt);
    }

    void flush() override {
        auto const pkt = akp05::buildCmdHeader(akp05::CmdStop);
        (void)m_transport->write(pkt);
    }

    // ---- IEncoderCapable ----------------------------------------------------
    [[nodiscard]] EncoderInfo encoderInfo() const noexcept override {
        return EncoderInfo{
            .count = akp05::EncoderCount,
            .pressable = true,
            .hasScreens = true,
            .stepsPerRevolution = 0, // endless
        };
    }

    void setEncoderImage(std::uint8_t encoderIndex,
                         std::span<std::uint8_t const> rgba,
                         std::uint16_t width,
                         std::uint16_t height) override {
        // WR-02: encoders are 0-based 0..EncoderCount-1; reject out-of-range.
        if (encoderIndex >= akp05::EncoderCount) {
            AJAZZ_LOG_WARN("akp05",
                           "setEncoderImage: encoderIndex {} out of range 0..{}; refusing",
                           static_cast<int>(encoderIndex),
                           static_cast<int>(akp05::EncoderCount - 1));
            return;
        }
        // The AKP05E renders the 4 encoder/strip zones through the SAME BAT opcode
        // as the keys, addressed at wire bytes 1..4 (encoderIndex + 1) — NOT the
        // vendor ENC opcode (buildEncoderImageHeader), which does not paint on the
        // live 0x0300:0x3004 firmware. Hardware-confirmed 2026-05-31: BAT wire 1..4
        // lit the 4 strip zones aligned to the dials; ENC/MAI/DRA stayed blank.
        // The strip's 4 zones ARE the encoder displays (akp_device_matrix §4: "no
        // separate encoder LCD"). Image is Rot180 + ~128 px, same as a key.
        auto const jpeg = encodeForDevice(rgba, width, height, akp05EncoderTransform());
        auto const sized = static_cast<std::uint16_t>(std::min<std::size_t>(jpeg.size(), 0xffff));
        sendImage(akp05::buildKeyImageHeader(static_cast<std::uint8_t>(encoderIndex + 1U), sized),
                  jpeg);
    }

    // ---- Rect-addressable touch-strip update (DRA, roadmap §11.4) -----------
    //
    // Vendor RE (akp05_vendor.md §3 row 190) — partial-update on the 800×480
    // touch strip without re-uploading the whole panel. Massive bandwidth win
    // for per-encoder overlay redraws (4 × 200×100 vs the whole 800×100/480).
    //
    // Shared helper invoked by the public ITouchStripDisplayCapable surface
    // above (setTouchStripImage). Held distinct from that override because
    // the M_V boot-logo variant uses the same rect-shape wire format with a
    // sentinel location=0x12 — the capability-level entry point rejects 0x12
    // outright (location-range check), while this helper carries an internal
    // guard so a future M_V wire-up doesn't accidentally route through here.
    //
    // Caller is responsible for keeping `srcWidth × srcHeight` aligned to the
    // intended zone, and for picking a sane `location` id (DO NOT pass 0x12 —
    // vendor uses that to route through a different packet shape that we
    // haven't yet captured; the runtime check below refuses).
    bool setSecondaryScreenImage(std::uint8_t location,
                                 std::uint16_t zoneWidth,
                                 std::uint16_t zoneHeight,
                                 std::uint16_t zoneX,
                                 std::uint16_t zoneY,
                                 std::span<std::uint8_t const> rgba,
                                 std::uint16_t srcWidth,
                                 std::uint16_t srcHeight) {
        if (location == 0x12) {
            AJAZZ_LOG_WARN("akp05",
                           "setSecondaryScreenImage: location=0x12 is reserved for the M_V "
                           "boot-logo variant which is not yet captured; refusing");
            return false;
        }
        ImageTransform const transform{
            .targetWidth = zoneWidth,
            .targetHeight = zoneHeight,
            .format = ImageFormat::Jpeg,
            .rotationDegrees = 0,
            .mirror = false,
            .jpegQuality = 85,
        };
        auto const jpeg = encodeForDevice(rgba, srcWidth, srcHeight, transform);
        auto const header = akp05::buildSecondaryScreenHeader(
            location, zoneWidth, zoneHeight, zoneX, zoneY, static_cast<std::uint32_t>(jpeg.size()));
        return sendImage(header, jpeg);
    }

    // ---- ITouchStripDisplayCapable (CRT DRA — akp05_vendor.md §3 row 190) --
    //
    // Public capability surface over the rect-addressable strip-update path
    // (formerly only reachable via the package-private setSecondaryScreenImage
    // method below). Validates the vendor zone-id, then delegates to the
    // shared helper so the chunked-upload + ULEND sentinel discipline lives
    // in one place. Distinct from setMainImage (whole-strip BAT-style upload)
    // because DRA only redraws ONE rect and leaves the rest of the strip
    // intact — massive bandwidth win on per-encoder overlay redraws.
    [[nodiscard]] TouchStripInfo touchStripInfo() const noexcept override {
        return TouchStripInfo{
            .widthPx = akp05::TouchStripWidthPx,
            .heightPx = akp05::TouchStripHeightPx,
            .zoneCount = akp05::TouchZoneCount,
        };
    }

    bool setTouchStripImage(std::span<std::uint8_t const> rgba,
                            std::uint16_t srcWidth,
                            std::uint16_t srcHeight,
                            std::uint8_t location,
                            std::uint16_t x,
                            std::uint16_t y,
                            std::uint16_t rectWidth,
                            std::uint16_t rectHeight) override {
        // Range-check the zone-id BEFORE reaching the lower-level helper so
        // callers see a clean false-return instead of a silent WARN-and-no-op
        // refusal on the reserved 0x12 boot-logo variant.
        if (location >= akp05::TouchZoneCount) {
            AJAZZ_LOG_WARN("akp05",
                           "setTouchStripImage: location={} >= zoneCount={}; refusing",
                           static_cast<int>(location),
                           static_cast<int>(akp05::TouchZoneCount));
            return false;
        }
        if (rgba.empty() || srcWidth == 0 || srcHeight == 0 || rectWidth == 0 || rectHeight == 0) {
            AJAZZ_LOG_WARN("akp05", "setTouchStripImage: empty input or zero-area rect; refusing");
            return false;
        }
        // Reuse the existing rect-addressable helper for resize + JPEG encode
        // + DRA header + chunked sendImage + ULEND commit sentinel. Propagate
        // its success so a device-yank mid-burst is reported as false per the
        // ITouchStripDisplayCapable contract.
        return setSecondaryScreenImage(
            location, rectWidth, rectHeight, x, y, rgba, srcWidth, srcHeight);
    }

    bool clearTouchStrip() override {
        // Simplest path: full-panel black image at zone 0, origin (0,0). Uses
        // the encodeSolid helper to skip allocating a 1.5 MB RGBA8 buffer.
        ImageTransform const transform{
            .targetWidth = akp05::TouchStripWidthPx,
            .targetHeight = akp05::TouchStripHeightPx,
            .format = ImageFormat::Jpeg,
            .rotationDegrees = 0,
            .mirror = false,
            .jpegQuality = 85,
        };
        auto const jpeg = encodeSolid(Rgb{0, 0, 0}, transform);
        auto const header = akp05::buildSecondaryScreenHeader(
            /*location=*/0,
            /*width=*/akp05::TouchStripWidthPx,
            /*height=*/akp05::TouchStripHeightPx,
            /*x=*/0,
            /*y=*/0,
            static_cast<std::uint32_t>(jpeg.size()));
        return sendImage(header, jpeg);
    }

    // ---- IBootLogoCapable (CRT LOG — akp05_vendor.md §2 row 188) ----------
    //
    // Vendor's "Custom boot logo / screensaver" feature. The boot logo lives
    // in device flash and renders at firmware power-on; this is distinct from
    // the live LCD-strip path which is volatile across reboot.
    //
    // We follow the main-strip dimensions per akp05::MainDisplay{Width,Height}Px
    // because the vendor doc names no specific boot-logo size, and the strip
    // geometry is the natural full-frame image the firmware paints at boot.
    void setBootLogo(std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        ImageTransform const transform{
            .targetWidth = akp05::MainDisplayWidthPx,
            .targetHeight = akp05::MainDisplayHeightPx,
            .format = ImageFormat::Jpeg,
            .rotationDegrees = 0,
            .mirror = false,
            .jpegQuality = 85,
        };
        auto const jpeg = encodeForDevice(rgba, width, height, transform);
        auto const header = akp05::buildLogoSizeHeader(static_cast<std::uint32_t>(jpeg.size()));
        sendImage(header, jpeg);
    }

    // ---- IClockCapable ------------------------------------------------------
    //
    // Scaffolded stub — no AJAZZ firmware exposes a host-settable RTC over HID
    // today. WARN-once per process via s_warned_akp05 (Pitfall 14).
    [[nodiscard]] TimeSyncResult
    setTime([[maybe_unused]] std::chrono::system_clock::time_point tp) override {
        std::call_once(s_warned_akp05, [] {
            AJAZZ_LOG_WARN("streamdeck.akp05", "setTime() not yet implemented for akp05");
        });
        return TimeSyncResult::NotImplemented;
    }

private:
    /** @brief Probe and cache the firmware version at open time.
     *
     *  Method (authoritative): the firmware version is a HID **GET_FEATURE_REPORT
     *  on report id `0x01` with a 20-byte buffer, with NO preceding write** —
     *  matching the `mirajazz` reference library (`read_firmware_version_from_raw_device`,
     *  github.com/4ndv/mirajazz) which explicitly supports the Mirabox N4 / AKP05
     *  family. The reply is the report-id byte followed by an ASCII string like
     *  "V3.AKP05E.01.007"; @ref parseVersionResponse skips the leading
     *  non-printable byte and returns the printable run.
     *
     *  Earlier attempts (write "CRT VER" then read interrupt-IN, or GET_REPORT
     *  with report id 0 / a 1024-byte buffer) returned nothing on a live AKP05E
     *  — the report id (0 vs 0x01) and buffer size were wrong.
     *
     *  **KNOWN PLATFORM LIMITATION:** this GET_FEATURE_REPORT does not work
     *  through the Windows HID stack — `mirajazz` returns `None` on Windows
     *  outright (their issue #10). On Windows the version therefore stays
     *  "unknown"; the path works on Linux/macOS hidraw. Best-effort: any failure
     *  leaves @c m_firmwareVersion at "unknown" rather than aborting @ref open().
     */
    void probeFirmwareVersion() {
        try {
            // GET_FEATURE_REPORT, report id 0x01, 20-byte buffer, no write
            // (mirajazz read_firmware_version_from_raw_device).
            std::array<std::uint8_t, 20> resp{};
            resp[0] = 0x01;
            auto const n = m_transport->readFeature(resp);
            if (auto v = akp05::parseVersionResponse({resp.data(), n})) {
                std::lock_guard const lock(m_mutex); // pairs with firmwareVersion() (WR-03)
                m_firmwareVersion = std::move(*v);
            }
        } catch (...) {
            // Best-effort probe (incl. the known Windows GET_FEATURE_REPORT
            // limitation, mirajazz #10) — leave m_firmwareVersion as "unknown".
        }
    }

    /** @brief Transmit an image to any display surface on the device.
     *
     *  Sends the pre-built @p header packet first, then streams @p payload
     *  in 1024-byte chunks.  Partial trailing chunks are zero-padded by the
     *  zero-initialised @c chunk array before each write.
     *
     *  @param header   Pre-built command header (key, encoder, or main LCD).
     *  @param payload  Raw JPEG bytes to transmit.
     */
    /// @return false on an oversized payload or a transport write failure
    /// (e.g. device-yank mid-burst), true once the full image + ULEND
    /// sentinel are written. void callers (setKeyImage/setMainImage/
    /// setEncoderImage/setBootLogo) discard this; the bool capability
    /// surface (setTouchStripImage/clearTouchStrip) propagates it.
    bool sendImage(std::array<std::uint8_t, akp05::PacketSize> const& header,
                   std::span<std::uint8_t const> payload) {
        // SEC-008 / COD-013 / CWE-190: header length field is 16 bits across
        // all three image variants (key/main/encoder). Refuse oversized
        // payloads to prevent firmware desync.
        if (payload.size() > 0xFFFFu) {
            AJAZZ_LOG_WARN("akp05",
                           "sendImage: payload {} bytes exceeds 65535-byte protocol max; "
                           "refusing",
                           payload.size());
            return false;
        }
        // ITransport::write() throws on a failed HID write; on a device-yank
        // mid-burst the throw must become a false return, not escape the
        // capability override (mirrors AjSeriesMouse::factoryReset).
        try {
            (void)m_transport->write(header);
            std::size_t offset = 0;
            while (offset < payload.size()) {
                std::array<std::uint8_t, akp05::PacketSize> chunk{};
                auto const take = std::min<std::size_t>(akp05::PacketSize, payload.size() - offset);
                std::memcpy(chunk.data(), payload.data() + offset, take);
                (void)m_transport->write(chunk);
                offset += take;
            }
            // Vendor RE (akp05_vendor.md §3 + roadmap §11.3): emit the 5-byte
            // ULEND commit-after-image-burst sentinel. Previously we relied on
            // STP from flush() but the vendor sends ULEND specifically after
            // image streams. Missing this may cause firmware desync on large
            // bursts (vendor RE annotation).
            (void)m_transport->write(akp05::buildUploadFinished());
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("akp05", "sendImage: HID write failed: {}", e.what());
            return false;
        }
        return true;
    }

    DeviceDescriptor m_descriptor; ///< Static hardware description supplied at construction.
    DeviceId m_id;                 ///< HID bus identity (VID, PID, serial string).
    TransportPtr m_transport;      ///< Underlying HID I/O channel.
    std::string m_firmwareVersion{"unknown"}; ///< Cached CRT VER response; set by open().
    EventCallback m_callback;                 ///< Registered input-event sink (may be null).
    mutable std::mutex m_mutex;               ///< Guards m_callback and m_firmwareVersion.
};

} // namespace

/** @brief Factory function: construct an AKP05 device object.
 *
 *  Instantiates an @c Akp05Device with the provided descriptor and HID
 *  identity.  The transport is opened lazily on the first call to
 *  @ref IDevice::open().
 *
 *  @param d    Static device descriptor (model name, family, codename).
 *  @param id   HID bus identity used to open the underlying transport.
 *  @return     Owning pointer to the new device instance.
 */
core::DevicePtr makeAkp05(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_shared<Akp05Device>(d, std::move(id));
}

/**
 * @brief Test-only factory exposing the @c Akp05Device COD-026 DI constructor
 *        across translation-unit boundaries (parallels @c makeAjSeriesWithTransport).
 *
 * Production code uses @ref makeAkp05 above; this overload exposes the same
 * backend with a substitutable transport so unit tests can assert byte-level
 * wire-format equality via @c MockTransport::writes() without touching real
 * HID hardware. Used by @c test_factory_reset_and_logo.cpp to pin the
 * boot-logo (LOG opcode) upload sequence.
 */
core::DevicePtr makeAkp05WithTransport(core::DeviceDescriptor const& d,
                                       core::DeviceId id,
                                       core::TransportPtr transport) {
    return std::make_shared<Akp05Device>(d, std::move(id), std::move(transport));
}

} // namespace ajazz::streamdeck
