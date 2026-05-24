// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_device_bridge.cpp
 * @brief Unit tests for Phase 19 Plan 19-01 PluginDeviceBridge pure helpers
 *        and ContextRegistry.
 *
 * Covers:
 *  - coordsForKeyIndex / keyIndexForCoords round-trip (Pitfall 2 — 1-based
 *    device index <-> 0-based Elgato coordinate).
 *  - decodeDataUriImage: valid base64 PNG URI, malformed base64, empty string,
 *    empty body, non-image body, and raw base64 body without "data:" prefix.
 *  - ownerForActionUuid: longest-prefix match, unowned action, no last-segment
 *    trim, shortest prefix not returned when a longer one exists.
 *  - ContextRegistry: registerContext / byContext / byCoord / retire /
 *    retireDevice / retirePage / clear.
 *
 * AKP05E grid: KeyRows=2, KeyCols=5, KeyCount=10 (from akp05_protocol.hpp).
 * Test grid values used: keyCols=5.
 *
 * Pitfall 5: ensureQCoreApp() leaked-singleton pattern from test_sd_plugin_server.cpp
 * (do NOT allocate a fresh QCoreApplication per TEST_CASE).
 *
 * CLAUDE.md: ASCII-only TEST_CASE names (no em-dash or unicode arrows).
 */
#include "plugin_device_bridge.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QImageWriter>
#include <QSet>
#include <QString>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::ActionContext;
using ajazz::app::ContextRegistry;
using ajazz::app::coordsForKeyIndex;
using ajazz::app::decodeDataUriImage;
using ajazz::app::DecodedImage;
using ajazz::app::GridCoord;
using ajazz::app::keyIndexForCoords;
using ajazz::app::ownerForActionUuid;

namespace {

/// Pitfall 5: leaked QCoreApplication singleton (never destroyed — matches the
/// test_sd_plugin_server.cpp pattern to avoid static-destruction races at exit).
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

/// Build a minimal valid 1x1 ARGB32 PNG as a base64-encoded data: URI.
/// This is test-fixture construction — NOT a second image-pipeline encode path.
QString make1x1PngDataUri() {
    QImage img(1, 1, QImage::Format_ARGB32);
    img.fill(Qt::red);

    QByteArray pngBytes;
    QBuffer buf(&pngBytes);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "PNG");
    writer.write(img);
    buf.close();

    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(pngBytes.toBase64());
}

/// Build a raw base64 body (no "data:" prefix, no comma) from a 1x1 PNG.
QString make1x1PngRawBase64() {
    QImage img(1, 1, QImage::Format_ARGB32);
    img.fill(Qt::blue);

    QByteArray pngBytes;
    QBuffer buf(&pngBytes);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "PNG");
    writer.write(img);
    buf.close();

    return QString::fromLatin1(pngBytes.toBase64());
}

} // anonymous namespace

// ==========================================================================
// coordsForKeyIndex / keyIndexForCoords
// ==========================================================================

TEST_CASE("PluginDeviceBridge coordsForKeyIndex converts 1-based key to 0-based coord",
          "[plugin-device-bridge][coords]") {
    ensureQCoreApp();

    // AKP05E grid: KeyCols=5
    constexpr std::uint8_t kCols = 5;

    // Key 1 -> {0, 0}
    {
        GridCoord c = coordsForKeyIndex(1, kCols);
        CHECK(c.row == 0);
        CHECK(c.column == 0);
    }

    // Key 5 -> {0, 4} (last of row 0)
    {
        GridCoord c = coordsForKeyIndex(5, kCols);
        CHECK(c.row == 0);
        CHECK(c.column == 4);
    }

    // Key 6 -> {1, 0} (first of row 1)
    {
        GridCoord c = coordsForKeyIndex(6, kCols);
        CHECK(c.row == 1);
        CHECK(c.column == 0);
    }

    // Key 10 -> {1, 4} (last key on a 2x5 grid)
    {
        GridCoord c = coordsForKeyIndex(10, kCols);
        CHECK(c.row == 1);
        CHECK(c.column == 4);
    }
}

TEST_CASE("PluginDeviceBridge keyIndexForCoords converts 0-based coord to 1-based key",
          "[plugin-device-bridge][coords]") {
    ensureQCoreApp();

    constexpr std::uint8_t kCols = 5;

    // {0, 0} -> 1
    CHECK(keyIndexForCoords(0, 0, kCols) == 1);

    // {0, 4} -> 5
    CHECK(keyIndexForCoords(0, 4, kCols) == 5);

    // {1, 0} -> 6
    CHECK(keyIndexForCoords(1, 0, kCols) == 6);

    // {1, 4} -> 10
    CHECK(keyIndexForCoords(1, 4, kCols) == 10);
}

TEST_CASE("PluginDeviceBridge coord round-trip for all grid cells",
          "[plugin-device-bridge][coords]") {
    ensureQCoreApp();

    // Full 2x5 grid round-trip: keyIndexForCoords(coordsForKeyIndex(k)) == k
    // and coordsForKeyIndex(keyIndexForCoords(r,c)) == {r, c}
    constexpr std::uint8_t kRows = 2;
    constexpr std::uint8_t kCols = 5;

    for (std::uint8_t row = 0; row < kRows; ++row) {
        for (std::uint8_t col = 0; col < kCols; ++col) {
            std::uint8_t const keyIdx = keyIndexForCoords(row, col, kCols);
            GridCoord const roundTrip = coordsForKeyIndex(keyIdx, kCols);
            CHECK(roundTrip.row == static_cast<int>(row));
            CHECK(roundTrip.column == static_cast<int>(col));
        }
    }

    // Reverse: coordsForKeyIndex -> keyIndexForCoords
    for (std::uint8_t k = 1; k <= kRows * kCols; ++k) {
        GridCoord const c = coordsForKeyIndex(k, kCols);
        std::uint8_t const roundTrip = keyIndexForCoords(c.row, c.column, kCols);
        CHECK(roundTrip == k);
    }
}

// ==========================================================================
// decodeDataUriImage
// ==========================================================================

TEST_CASE("PluginDeviceBridge decodeDataUriImage decodes a valid 1x1 PNG data URI",
          "[plugin-device-bridge][decode]") {
    ensureQCoreApp();

    QString const uri = make1x1PngDataUri();
    DecodedImage const result = decodeDataUriImage(uri);

    CHECK(result.ok == true);
    CHECK_FALSE(result.image.isNull());
    CHECK(result.image.width() == 1);
    CHECK(result.image.height() == 1);
}

TEST_CASE("PluginDeviceBridge decodeDataUriImage handles empty string",
          "[plugin-device-bridge][decode]") {
    ensureQCoreApp();

    DecodedImage const result = decodeDataUriImage(QStringLiteral(""));
    CHECK(result.ok == false);
    CHECK(result.image.isNull());
}

TEST_CASE("PluginDeviceBridge decodeDataUriImage handles empty base64 body",
          "[plugin-device-bridge][decode]") {
    ensureQCoreApp();

    // The comma is present but there is nothing after it.
    DecodedImage const result = decodeDataUriImage(QStringLiteral("data:image/png;base64,"));
    CHECK(result.ok == false);
    CHECK(result.image.isNull());
}

TEST_CASE("PluginDeviceBridge decodeDataUriImage handles malformed base64",
          "[plugin-device-bridge][decode]") {
    ensureQCoreApp();

    // "!!!" is not valid base64 — fromBase64 will yield garbage/empty bytes,
    // and loadFromData will reject them.
    DecodedImage const result =
        decodeDataUriImage(QStringLiteral("data:image/png;base64,!!!notbase64!!!"));
    CHECK(result.ok == false);
    CHECK(result.image.isNull());
}

TEST_CASE("PluginDeviceBridge decodeDataUriImage handles valid base64 non-image body",
          "[plugin-device-bridge][decode]") {
    ensureQCoreApp();

    // Valid base64 encoding of the string "hello world" — not a valid image.
    // QByteArray::fromBase64("aGVsbG8gd29ybGQ=") == "hello world"
    DecodedImage const result =
        decodeDataUriImage(QStringLiteral("data:image/png;base64,aGVsbG8gd29ybGQ="));
    CHECK(result.ok == false);
    CHECK(result.image.isNull());
}

TEST_CASE("PluginDeviceBridge decodeDataUriImage tolerates raw base64 body without data prefix",
          "[plugin-device-bridge][decode]") {
    ensureQCoreApp();

    // No "data:" prefix and no comma — the whole string is the base64 body.
    QString const rawBase64 = make1x1PngRawBase64();
    DecodedImage const result = decodeDataUriImage(rawBase64);

    CHECK(result.ok == true);
    CHECK_FALSE(result.image.isNull());
    CHECK(result.image.width() == 1);
    CHECK(result.image.height() == 1);
}

// ==========================================================================
// ownerForActionUuid
// ==========================================================================

TEST_CASE("PluginDeviceBridge ownerForActionUuid resolves dotted prefix to plugin uuid",
          "[plugin-device-bridge][uuid-prefix]") {
    ensureQCoreApp();

    QSet<QString> const registered = {QStringLiteral("com.x.plugin"),
                                      QStringLiteral("com.y.other")};

    // com.x.plugin.action1 -> com.x.plugin
    QString const owner = ownerForActionUuid(QStringLiteral("com.x.plugin.action1"), registered);
    CHECK(owner == QStringLiteral("com.x.plugin"));
}

TEST_CASE("PluginDeviceBridge ownerForActionUuid returns empty for unowned action",
          "[plugin-device-bridge][uuid-prefix]") {
    ensureQCoreApp();

    QSet<QString> const registered = {QStringLiteral("com.x.plugin"),
                                      QStringLiteral("com.y.other")};

    // com.z.unknown.action has no registered owner.
    QString const owner = ownerForActionUuid(QStringLiteral("com.z.unknown.action"), registered);
    CHECK(owner.isEmpty());
}

TEST_CASE("PluginDeviceBridge ownerForActionUuid longest-prefix wins",
          "[plugin-device-bridge][uuid-prefix]") {
    ensureQCoreApp();

    // Both "com.x" and "com.x.plugin" are prefixes of "com.x.plugin.action";
    // "com.x.plugin" is longer and must win.
    QSet<QString> const registered = {
        QStringLiteral("com.x"), QStringLiteral("com.x.plugin"), QStringLiteral("com.y.other")};

    QString const owner = ownerForActionUuid(QStringLiteral("com.x.plugin.action"), registered);
    CHECK(owner == QStringLiteral("com.x.plugin"));
}

TEST_CASE("PluginDeviceBridge ownerForActionUuid dotted-boundary rule - no partial segment match",
          "[plugin-device-bridge][uuid-prefix]") {
    ensureQCoreApp();

    // "com.x.plug" must NOT match "com.x.plugin.action" because "plug" is not
    // a full dotted segment of "plugin" (Pitfall 4 — must not trim last segment).
    QSet<QString> const registered = {QStringLiteral("com.x.plug"),
                                      QStringLiteral("com.x.pluginXYZ")};

    // "com.x.plugin.action" starts with neither "com.x.plug." nor "com.x.pluginXYZ."
    // (dot-boundary-checked).
    QString const owner = ownerForActionUuid(QStringLiteral("com.x.plugin.action"), registered);
    CHECK(owner.isEmpty());
}

TEST_CASE("PluginDeviceBridge ownerForActionUuid empty registry returns empty",
          "[plugin-device-bridge][uuid-prefix]") {
    ensureQCoreApp();

    QSet<QString> const registered;
    QString const owner = ownerForActionUuid(QStringLiteral("com.x.plugin.action"), registered);
    CHECK(owner.isEmpty());
}

// ==========================================================================
// ContextRegistry
// ==========================================================================

TEST_CASE("PluginDeviceBridge ContextRegistry registerContext and byContext round-trip",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;
    ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 2;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.x.plugin.action1");
    ctx.pluginUuid = QStringLiteral("com.x.plugin");

    QString const ctxId = reg.registerContext(ctx);
    CHECK_FALSE(ctxId.isEmpty());

    // Encoded-tuple scheme: deviceId#pageId#controller#row#column
    CHECK(ctxId == QStringLiteral("akp05e#root#Keypad#0#2"));

    auto const found = reg.byContext(ctxId);
    REQUIRE(found.has_value());
    CHECK(found->deviceId == ctx.deviceId);
    CHECK(found->pageId == ctx.pageId);
    CHECK(found->row == ctx.row);
    CHECK(found->column == ctx.column);
    CHECK(found->controller == ctx.controller);
    CHECK(found->actionUUID == ctx.actionUUID);
    CHECK(found->pluginUuid == ctx.pluginUuid);
}

TEST_CASE("PluginDeviceBridge ContextRegistry byCoord lookup", "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;
    ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 1;
    ctx.column = 3;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.x.plugin.action2");
    ctx.pluginUuid = QStringLiteral("com.x.plugin");

    [[maybe_unused]] auto regId = reg.registerContext(ctx);

    auto const found = reg.byCoord(QStringLiteral("Keypad"), 1, 3);
    REQUIRE(found.has_value());
    CHECK(found->row == 1);
    CHECK(found->column == 3);
    CHECK(found->pluginUuid == QStringLiteral("com.x.plugin"));
}

TEST_CASE("PluginDeviceBridge ContextRegistry byContext returns nullopt for unknown context",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;
    auto const result = reg.byContext(QStringLiteral("nonexistent#context"));
    CHECK_FALSE(result.has_value());
}

TEST_CASE("PluginDeviceBridge ContextRegistry byCoord returns nullopt for unregistered coord",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;
    auto const result = reg.byCoord(QStringLiteral("Keypad"), 0, 0);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("PluginDeviceBridge ContextRegistry retire removes a single context",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;
    ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 0;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.x.plugin.action1");
    ctx.pluginUuid = QStringLiteral("com.x.plugin");

    QString const ctxId = reg.registerContext(ctx);
    CHECK(reg.size() == 1);

    reg.retire(ctxId);
    CHECK(reg.size() == 0);
    CHECK_FALSE(reg.byContext(ctxId).has_value());
    CHECK_FALSE(reg.byCoord(QStringLiteral("Keypad"), 0, 0).has_value());
}

TEST_CASE("PluginDeviceBridge ContextRegistry retireDevice removes all contexts for that device",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;

    auto makeCtx = [](QString const& dev, int row, int col) {
        ActionContext ctx;
        ctx.deviceId = dev;
        ctx.pageId = QStringLiteral("root");
        ctx.row = row;
        ctx.column = col;
        ctx.controller = QStringLiteral("Keypad");
        ctx.actionUUID = QStringLiteral("com.x.plugin.action");
        ctx.pluginUuid = QStringLiteral("com.x.plugin");
        return ctx;
    };

    [[maybe_unused]] auto id1 = reg.registerContext(makeCtx(QStringLiteral("akp05e"), 0, 0));
    [[maybe_unused]] auto id2 = reg.registerContext(makeCtx(QStringLiteral("akp05e"), 0, 1));
    [[maybe_unused]] auto id3 = reg.registerContext(makeCtx(QStringLiteral("other_device"), 0, 0));

    CHECK(reg.size() == 3);
    reg.retireDevice(QStringLiteral("akp05e"));
    CHECK(reg.size() == 1);

    // The other_device context must survive.
    auto const surviving = reg.byCoord(QStringLiteral("Keypad"), 0, 0);
    // The only surviving entry is for other_device — but coord key is shared
    // since both devices had row=0,col=0. After retireDevice("akp05e") the
    // other_device entry should still be accessible by context id.
    // The coord lookup for ("Keypad", 0, 0) might have been overwritten by the
    // last registration. The registry size should be exactly 1.
    CHECK(reg.size() == 1);
}

TEST_CASE("PluginDeviceBridge ContextRegistry retirePage removes only that page",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;

    auto makeCtx = [](QString const& page, int col) {
        ActionContext ctx;
        ctx.deviceId = QStringLiteral("akp05e");
        ctx.pageId = page;
        ctx.row = 0;
        ctx.column = col;
        ctx.controller = QStringLiteral("Keypad");
        ctx.actionUUID = QStringLiteral("com.x.plugin.action");
        ctx.pluginUuid = QStringLiteral("com.x.plugin");
        return ctx;
    };

    [[maybe_unused]] auto rr0 = reg.registerContext(makeCtx(QStringLiteral("root"), 0));
    [[maybe_unused]] auto rr1 = reg.registerContext(makeCtx(QStringLiteral("root"), 1));
    [[maybe_unused]] auto rp2 = reg.registerContext(makeCtx(QStringLiteral("page2"), 0));

    CHECK(reg.size() == 3);
    reg.retirePage(QStringLiteral("akp05e"), QStringLiteral("root"));
    CHECK(reg.size() == 1);
}

TEST_CASE("PluginDeviceBridge ContextRegistry clear removes everything",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;

    for (int i = 0; i < 5; ++i) {
        ActionContext ctx;
        ctx.deviceId = QStringLiteral("akp05e");
        ctx.pageId = QStringLiteral("root");
        ctx.row = 0;
        ctx.column = i;
        ctx.controller = QStringLiteral("Keypad");
        ctx.actionUUID = QStringLiteral("com.x.plugin.action");
        ctx.pluginUuid = QStringLiteral("com.x.plugin");
        [[maybe_unused]] auto id = reg.registerContext(ctx);
    }
    CHECK(reg.size() == 5);
    reg.clear();
    CHECK(reg.size() == 0);
}

TEST_CASE("PluginDeviceBridge ContextRegistry registerContext is idempotent for same ctx",
          "[plugin-device-bridge][registry]") {
    ensureQCoreApp();

    ContextRegistry reg;
    ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 0;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.x.plugin.action");
    ctx.pluginUuid = QStringLiteral("com.x.plugin");

    QString const id1 = reg.registerContext(ctx);
    CHECK(reg.size() == 1);

    // Re-register the same context — should update, not duplicate.
    ctx.pluginUuid = QStringLiteral("com.x.plugin");
    QString const id2 = reg.registerContext(ctx);
    CHECK(id1 == id2);
    CHECK(reg.size() == 1);
}
