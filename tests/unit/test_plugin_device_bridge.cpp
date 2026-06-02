// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_device_bridge.cpp
 * @brief Unit tests for Phase 19 Plan 19-01/19-02 PluginDeviceBridge.
 *
 * 19-01: pure helpers + ContextRegistry unit tests.
 * 19-02 (PLUGIN-10): loopback e2e tests composing SdPluginServer +
 *       StreamDockControlService (in-process FakeStreamDockDevice) +
 *       PluginDeviceBridge.
 *
 * 19-01 test coverage:
 *  - coordsForKeyIndex / keyIndexForCoords round-trip (Pitfall 2).
 *  - decodeDataUriImage: valid/invalid/empty URI cases.
 *  - ownerForActionUuid: longest-prefix match, unowned, boundary rule.
 *  - ContextRegistry: registerContext / byContext / byCoord / retire / retirePage / clear.
 *
 * 19-02 e2e test coverage (PLUGIN-10):
 *  - e2e setImage paints the right 1-based key (setKeyImage on the fake device).
 *  - Malformed data-URI -> placeholder (solid fill), no image burst, no crash.
 *  - Cross-plugin denial: unknown-owner context produces no paint.
 *  - Visual family no-crash: setTitle / setBG / setFeedback round-trip without crash.
 *
 * AKP05E grid: KeyRows=2, KeyCols=5, KeyCount=10.
 * Pitfall 5: ensureQCoreApp() leaked-singleton (no fresh QCoreApplication per TEST_CASE).
 * CLAUDE.md: ASCII-only TEST_CASE names/tags.
 */
#include "plugin_device_bridge.hpp"

// 19-02 e2e: needs SdPluginServer + StreamDockControlService + device fixture.
#ifdef AJAZZ_HAVE_WEBSOCKETS
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/profile.hpp"
#include "fixtures/fake_stream_dock_device.hpp"
#include "sd_plugin_server.hpp"
#include "stream_dock_control_service.hpp"
#include "stream_dock_input_service.hpp" // GAP-28B regression tests

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QWebSocket>
#endif

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

TEST_CASE("PluginDeviceBridge decodeDataUriImage rejects oversize base64 body T-19-img",
          "[plugin-device-bridge][decode][security]") {
    ensureQCoreApp();

    // Build a base64 body that exceeds kMaxBase64Bytes (512 KB).
    // Use 600 KB of 'A' characters — valid base64 alphabet, but over the cap.
    // We do NOT allocate a real image; the size check fires before fromBase64.
    constexpr qsizetype kOversize = 600 * 1024; // 600 KB > kMaxBase64Bytes (512 KB)
    QString const bigBody =
        QStringLiteral("data:image/png;base64,") + QString(kOversize, QLatin1Char('A'));
    DecodedImage const result = decodeDataUriImage(bigBody);

    // Must be rejected without OOM/crash (T-19-img size bound).
    CHECK(result.ok == false);
    CHECK(result.image.isNull());
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

TEST_CASE("PluginDeviceBridge ContextRegistry setState updates the stored state index",
          "[plugin-device-bridge][registry][state]") {
    ensureQCoreApp();

    ContextRegistry reg;
    ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 1;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.x.plugin.toggle");
    ctx.pluginUuid = QStringLiteral("com.x.plugin");

    QString const ctxId = reg.registerContext(ctx);
    // A freshly-registered context starts at state 0 (Elgato default).
    REQUIRE(reg.byContext(ctxId).has_value());
    CHECK(reg.byContext(ctxId)->stateIndex == 0);

    // setState updates the stored entry (not a copy) and returns true.
    CHECK(reg.setState(ctxId, 1));
    CHECK(reg.byContext(ctxId)->stateIndex == 1);

    // Re-setState to a different index.
    CHECK(reg.setState(ctxId, 3));
    CHECK(reg.byContext(ctxId)->stateIndex == 3);

    // Negative indices clamp to 0 (states are 0-based).
    CHECK(reg.setState(ctxId, -5));
    CHECK(reg.byContext(ctxId)->stateIndex == 0);

    // byCoord must also observe the updated state (same backing entry).
    reg.setState(ctxId, 2);
    auto const byCoord = reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 1);
    REQUIRE(byCoord.has_value());
    CHECK(byCoord->stateIndex == 2);
}

TEST_CASE("PluginDeviceBridge ContextRegistry setState returns false for unknown context",
          "[plugin-device-bridge][registry][state]") {
    ensureQCoreApp();

    ContextRegistry reg;
    CHECK_FALSE(reg.setState(QStringLiteral("nonexistent#root#Keypad#0#0"), 1));
}

TEST_CASE("PluginDeviceBridge ContextRegistry registerContext preserves state on re-registration",
          "[plugin-device-bridge][registry][state]") {
    ensureQCoreApp();

    ContextRegistry reg;
    ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 0;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.x.plugin.toggle");
    ctx.pluginUuid = QStringLiteral("com.x.plugin");

    QString const ctxId = reg.registerContext(ctx);
    REQUIRE(reg.setState(ctxId, 1));

    // Idempotent re-registration (e.g. page re-activation) builds a fresh
    // ActionContext (stateIndex 0). The registry must PRESERVE the prior state so
    // a navigated-away-and-back action does not silently reset to state 0.
    ActionContext fresh = ctx; // stateIndex defaults to 0
    QString const ctxId2 = reg.registerContext(fresh);
    CHECK(ctxId2 == ctxId); // same encoded-tuple id (state excluded from the id)
    REQUIRE(reg.byContext(ctxId).has_value());
    CHECK(reg.byContext(ctxId)->stateIndex == 1); // preserved, not reset
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

    auto const found = reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 1, 3);
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
    auto const result = reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 0);
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
    CHECK_FALSE(reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 0).has_value());
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

    // CR-02: With deviceId in coordKey, the other_device context at (Keypad,0,0) is
    // now independently keyed. After retireDevice("akp05e"), the other_device entry
    // must still be reachable via byCoord with "other_device".
    auto const surviving =
        reg.byCoord(QStringLiteral("other_device"), QStringLiteral("Keypad"), 0, 0);
    REQUIRE(surviving.has_value());
    CHECK(surviving->deviceId == QStringLiteral("other_device"));

    // The "akp05e" coord entry must have been removed.
    auto const retired = reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 0);
    CHECK_FALSE(retired.has_value());
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

TEST_CASE("PluginDeviceBridge ContextRegistry CR-02 two devices same coord do not collide",
          "[plugin-device-bridge][registry][security]") {
    ensureQCoreApp();

    // CR-02: two simultaneously-connected devices sharing the same controller/row/col
    // must have independent coord entries and independent retire semantics.
    ContextRegistry reg;

    auto makeCtx = [](QString const& dev, QString const& plugin) {
        ActionContext ctx;
        ctx.deviceId = dev;
        ctx.pageId = QStringLiteral("root");
        ctx.row = 0;
        ctx.column = 0;
        ctx.controller = QStringLiteral("Keypad");
        ctx.actionUUID = plugin + QStringLiteral(".action1");
        ctx.pluginUuid = plugin;
        return ctx;
    };

    QString const idA =
        reg.registerContext(makeCtx(QStringLiteral("akp05e"), QStringLiteral("com.a.plug")));
    QString const idB =
        reg.registerContext(makeCtx(QStringLiteral("akp153"), QStringLiteral("com.b.plug")));

    // Both contexts must be independently reachable by their respective device.
    auto const foundA = reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 0);
    auto const foundB = reg.byCoord(QStringLiteral("akp153"), QStringLiteral("Keypad"), 0, 0);
    REQUIRE(foundA.has_value());
    REQUIRE(foundB.has_value());
    CHECK(foundA->pluginUuid == QStringLiteral("com.a.plug"));
    CHECK(foundB->pluginUuid == QStringLiteral("com.b.plug"));

    // Retiring akp05e's context must NOT remove akp153's entry.
    reg.retire(idA);
    CHECK(reg.size() == 1);

    auto const stillB = reg.byCoord(QStringLiteral("akp153"), QStringLiteral("Keypad"), 0, 0);
    REQUIRE(stillB.has_value());
    CHECK(stillB->pluginUuid == QStringLiteral("com.b.plug"));
    CHECK(stillB->deviceId == QStringLiteral("akp153"));

    // akp05e coord must be gone.
    auto const goneA = reg.byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 0);
    CHECK_FALSE(goneA.has_value());
}

// ==========================================================================
// Phase 19-02 e2e tests (PLUGIN-10): loopback client -> fake-device capability spy
// Gated on AJAZZ_HAVE_WEBSOCKETS — same as the sd_plugin_server tests.
// ==========================================================================
#ifdef AJAZZ_HAVE_WEBSOCKETS

namespace {

// ---------------------------------------------------------------------------
// Helper: event loop pump (mirrors test_sd_plugin_server.cpp pattern).
// ---------------------------------------------------------------------------
void pump19(int ms = 200) {
    auto until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

bool waitForSpy19(QSignalSpy& spy, int timeout_ms = 3000) {
    auto until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (spy.count() == 0 && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return spy.count() > 0;
}

// ---------------------------------------------------------------------------
// Helper: build a minimal 1x1 ARGB32 PNG as a base64-encoded data: URI
// (test fixture only — NOT a second encode path; QImageWriter is test-only).
// ---------------------------------------------------------------------------
QString makeSmallPngDataUri() {
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    QByteArray pngBytes;
    QBuffer buf(&pngBytes);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "PNG");
    writer.write(img);
    buf.close();
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(pngBytes.toBase64());
}

// ---------------------------------------------------------------------------
// Fixture: in-process fake AKP05E device (mirrors test_stream_dock_control_service.cpp).
// ---------------------------------------------------------------------------
struct E2eFixture {
    ajazz::app::SdPluginServer* server;
    ajazz::app::StreamDockControlService* control;
    std::unique_ptr<ajazz::app::PluginDeviceBridge> bridge;

    QString contextId;       ///< Pre-registered context for key at {row:0, col:2} (keyIndex 3).
    QString pluginUuid;      ///< The owning plugin UUID for that context.
    QString otherPluginUuid; ///< UUID of a different plugin (for cross-plugin denial test).
    QString otherContextId;  ///< Context owned by otherPluginUuid.
};

/// Build the e2e fixture.  Note: server and control are non-owning — caller owns them.
E2eFixture makeE2eFixture(ajazz::app::SdPluginServer* server,
                          ajazz::app::StreamDockControlService* control) {
    E2eFixture fx;
    fx.server = server;
    fx.control = control;
    fx.pluginUuid = QStringLiteral("com.test.plug");
    fx.otherPluginUuid = QStringLiteral("com.other.plug");

    // Wire the bridge the same way Application does it.
    fx.bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(server, control, nullptr);

    // Pre-register a context for key {row:0, col:2} (1-based keyIndex = 0*5+2+1 = 3).
    ajazz::app::ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 2;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.test.plug.action1");
    ctx.pluginUuid = fx.pluginUuid;
    fx.contextId = fx.bridge->registry().registerContext(ctx);

    // Pre-register a context owned by the OTHER plugin (for cross-plugin denial test).
    ajazz::app::ActionContext other;
    other.deviceId = QStringLiteral("akp05e");
    other.pageId = QStringLiteral("root");
    other.row = 1;
    other.column = 1;
    other.controller = QStringLiteral("Keypad");
    other.actionUUID = QStringLiteral("com.other.plug.action1");
    other.pluginUuid = fx.otherPluginUuid;
    fx.otherContextId = fx.bridge->registry().registerContext(other);

    return fx;
}

/// Build a minimal AKP05E device descriptor for the in-process fake.
ajazz::core::DeviceDescriptor makeTestAkp05eDescriptor() {
    ajazz::core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = ajazz::core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (e2e-test)";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.gridColumns = 5;
    d.keyRows = 2;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.touchZoneCount = 4;
    d.hasClock = false;
    return d;
}

ajazz::core::DeviceId makeTestAkp05eId() {
    ajazz::core::DeviceId id{};
    id.vendorId = 0x0300;
    id.productId = 0x3004;
    id.serial = "TEST-19-02";
    return id;
}

/// Build the in-process fake AKP05E device. Records setKeyImage capability calls
/// instead of producing BAT/ULEND wire bytes (that framing coverage moved to the
/// Rust sidecar's cargo tests). The e2e chain (WebSocket -> SdPluginServer ->
/// PluginDeviceBridge -> StreamDockControlService -> device) is otherwise intact.
std::shared_ptr<ajazz::tests::FakeStreamDockDevice> makeE2eFake() {
    auto fake = std::make_shared<ajazz::tests::FakeStreamDockDevice>(makeTestAkp05eDescriptor(),
                                                                     makeTestAkp05eId());
    fake->setFirmwareVersion("V3.AKP05E.01.007");
    return fake;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// e2e: setImage paints the right key (PLUGIN-10)
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E setImage paints correct key via control service spy",
          "[plugin-device-bridge][e2e][PLUGIN-10]") {
    ensureQCoreApp();

    // In-process fake AKP05E device (records setKeyImage capability calls).
    auto fake = makeE2eFake();

    // StreamDockControlService backed by the fake device.
    ajazz::app::StreamDockControlService control(
        [fake](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return fake; }, nullptr);
    control.setActiveDevice(QStringLiteral("akp05e"));
    // Drain the open/brightness work from setActiveDevice.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    auto const paintsAfterOpen = fake->keyImages.size();

    // SdPluginServer (loopback).
    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    // Build the e2e fixture (wires the bridge to server + control).
    auto fx = makeE2eFixture(&server, &control);
    // fx.contextId is for key {row:0, col:2} = 1-based keyIndex 3.

    // Connect a loopback plugin client and register.
    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy19(connectedSpy));
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.plug"})"));
    REQUIRE(waitForSpy19(registeredSpy));

    // Send setImage for the pre-registered context.
    QString const imageMsg =
        QStringLiteral(R"({"event":"setImage","context":"%1","payload":{"image":"%2","target":0}})")
            .arg(fx.contextId, makeSmallPngDataUri());
    client.sendTextMessage(imageMsg);

    // Drain until the control service timer fires and paints the key.
    auto deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (fake->keyImages.size() > paintsAfterOpen) {
            break;
        }
    }

    // The paint must have landed on device key index 3 (row:0, col:2 -> 0*5+2+1).
    REQUIRE(fake->keyImages.size() > paintsAfterOpen);
    bool paintedKey3 = false;
    for (std::size_t i = paintsAfterOpen; i < fake->keyImages.size(); ++i) {
        if (fake->keyImages[i].index == 3) {
            paintedKey3 = true;
        }
    }
    CHECK(paintedKey3);
}

// ---------------------------------------------------------------------------
// e2e: malformed data-URI -> placeholder, no crash, no failure event back
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E malformed image URI paints placeholder no crash",
          "[plugin-device-bridge][e2e][placeholder]") {
    ensureQCoreApp();

    auto fake = makeE2eFake();

    ajazz::app::StreamDockControlService control(
        [fake](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return fake; }, nullptr);
    control.setActiveDevice(QStringLiteral("akp05e"));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    auto const paintsAfterOpen = fake->keyImages.size();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto fx = makeE2eFixture(&server, &control);

    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy19(connectedSpy));
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.plug"})"));
    REQUIRE(waitForSpy19(registeredSpy));

    // Send setImage with a malformed data-URI.
    QString const badMsg =
        QStringLiteral(
            R"({"event":"setImage","context":"%1","payload":{"image":"data:image/png;base64,!!!notbase64!!!","target":0}})")
            .arg(fx.contextId);
    client.sendTextMessage(badMsg);

    // Drain: the placeholder (solid fill via assignKeyImage) should paint the key.
    auto deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (fake->keyImages.size() > paintsAfterOpen) {
            break;
        }
    }

    // The placeholder path calls assignKeyImage (solid fill). Verify the process
    // did not crash (implicit by reaching this point) and that the key was painted.
    REQUIRE(fake->keyImages.size() > paintsAfterOpen); // placeholder paint must have fired

    // No failure event should have been sent back to the client (spec §5).
    // The msgSpy must not contain any non-passHello frame after the malformed setImage.
    // passHello (17-03) may arrive before; filter for any 'error'-like event.
    pump19(200); // extra drain
    bool failureEventFound = false;
    for (auto const& args : msgSpy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        QString const ev = obj.value(QStringLiteral("event")).toString();
        // The only expected host->plugin event is passHello; any 'error' or 'setError'
        // would be a violation of the §5 "send no failure event" contract.
        if (ev == QStringLiteral("error") || ev == QStringLiteral("setError")) {
            failureEventFound = true;
        }
    }
    CHECK_FALSE(failureEventFound);
}

// ---------------------------------------------------------------------------
// e2e: cross-plugin denial — a plugin cannot paint a key it does not own
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E cross-plugin denial produces no paint",
          "[plugin-device-bridge][e2e][security]") {
    ensureQCoreApp();

    auto fake = makeE2eFake();

    ajazz::app::StreamDockControlService control(
        [fake](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return fake; }, nullptr);
    control.setActiveDevice(QStringLiteral("akp05e"));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    auto const paintsAfterOpen = fake->keyImages.size();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto fx = makeE2eFixture(&server, &control);
    // fx.otherContextId is owned by com.other.plug; our client is com.test.plug.

    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy19(connectedSpy));
    // Register as com.test.plug (NOT the owner of otherContextId).
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.plug"})"));
    REQUIRE(waitForSpy19(registeredSpy));

    // Send setImage for a context owned by com.other.plug (cross-plugin attack).
    QString const crossMsg =
        QStringLiteral(R"({"event":"setImage","context":"%1","payload":{"image":"%2","target":0}})")
            .arg(fx.otherContextId, makeSmallPngDataUri());
    client.sendTextMessage(crossMsg);

    // Drain and verify: NO key paint should occur after the denial.
    pump19(500);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    // No new setKeyImage calls after the open baseline — cross-plugin denial.
    CHECK(fake->keyImages.size() == paintsAfterOpen);
}

// ---------------------------------------------------------------------------
// e2e: visual family no-crash (setTitle / setBG / setFeedback)
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E visual family events do not crash",
          "[plugin-device-bridge][e2e][visual-family]") {
    ensureQCoreApp();

    auto fake = makeE2eFake();

    ajazz::app::StreamDockControlService control(
        [fake](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return fake; }, nullptr);
    control.setActiveDevice(QStringLiteral("akp05e"));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    auto const paintsAfterOpen = fake->keyImages.size();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto fx = makeE2eFixture(&server, &control);

    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy19(connectedSpy));
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.plug"})"));
    REQUIRE(waitForSpy19(registeredSpy));

    // setTitle: must not crash; renders title to QImage -> setKeyImage paint.
    client.sendTextMessage(
        QStringLiteral(
            R"({"event":"setTitle","context":"%1","payload":{"title":"Hello","target":0}})")
            .arg(fx.contextId));

    // setBG: must not crash; renders solid fill -> setKeyImage paint.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"setBG","context":"%1","payload":{"color":"#FF0000"}})")
            .arg(fx.contextId));

    // setFeedback: must not crash; acknowledged-but-deferred (Phase 23).
    // Must NOT produce a key-image paint.
    // Drain setTitle + setBG first (each paints the key once).
    auto deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        // Wait for both paints (setTitle + setBG).
        if (fake->keyImages.size() >= paintsAfterOpen + 2) {
            break;
        }
    }

    client.sendTextMessage(
        QStringLiteral(R"({"event":"setFeedback","context":"%1","payload":{"title":"Enc"}})")
            .arg(fx.contextId));
    std::size_t const paintsBeforeFeedback = fake->keyImages.size();
    pump19(300); // drain the setFeedback (should be a no-op paint)

    // setTitle and setBG must have painted the key.
    REQUIRE(fake->keyImages.size() > paintsAfterOpen);

    // setFeedback must NOT produce a new key-image paint (aux-surface deferred).
    CHECK(fake->keyImages.size() == paintsBeforeFeedback); // no new paint after setFeedback

    // Implicit crash-free assertion: reaching this point means nothing threw.
    CHECK(true);
}

// ==========================================================================
// Phase 19-03 e2e tests (PLUGIN-10): outbound device->plugin event routing
//
// These tests feed a canned DeviceEvent directly into the bridge's
// onDeviceEvent() slot (no real device needed) and assert that the bound
// loopback client receives the correct §4.4 event envelope.
// Still inside the AJAZZ_HAVE_WEBSOCKETS guard opened above.
// ==========================================================================

namespace {

/// Parse the text WebSocket frames received by a loopback client and return
/// the event names (the "event" field from each JSON frame).
QStringList receivedEventNames(QSignalSpy const& spy) {
    QStringList names;
    for (auto const& args : spy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        QString const ev = obj.value(QStringLiteral("event")).toString();
        if (!ev.isEmpty()) {
            names << ev;
        }
    }
    return names;
}

/// Return the first JSON payload object from a spy that matches the given event name.
QJsonObject firstPayloadForEvent(QSignalSpy const& spy, QString const& eventName) {
    for (auto const& args : spy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("event")).toString() == eventName) {
            return obj.value(QStringLiteral("payload")).toObject();
        }
    }
    return {};
}

/// Return the first FULL event object from a spy that matches the given event name
/// (the complete Elgato envelope: event + top-level action/context/device + payload).
QJsonObject firstEventForEvent(QSignalSpy const& spy, QString const& eventName) {
    for (auto const& args : spy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("event")).toString() == eventName) {
            return obj;
        }
    }
    return {};
}

/// Connect a loopback QWebSocket client to server and register with pluginUuid.
/// Returns the connected client (caller must keep it alive).
/// REQUIRES: spy for SdPluginServer::pluginRegistered is set up before calling this.
bool connectAndRegister(QWebSocket& client,
                        ajazz::app::SdPluginServer& server,
                        QString const& pluginUuid,
                        QSignalSpy& registeredSpy) {
    QSignalSpy connSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    if (!waitForSpy19(connSpy)) {
        return false;
    }
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})").arg(pluginUuid));
    return waitForSpy19(registeredSpy);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 19-03 e2e: keyDown delivered with 0-based coordinates (PLUGIN-10)
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E outbound keyDown delivers to bound plugin with coordinates",
          "[plugin-device-bridge][e2e][outbound][PLUGIN-10]") {
    ensureQCoreApp();

    // Server + bridge (no real device needed for outbound tests — only registry + sendEvent).
    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    // Construct bridge without a control service (outbound test only needs server + registry).
    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Pre-register a context for key 3 (0-based {row:0, col:2}) for "com.test.plug".
    ajazz::app::ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 2;
    ctx.controller = QStringLiteral("Keypad");
    ctx.actionUUID = QStringLiteral("com.test.plug.action1");
    ctx.pluginUuid = QStringLiteral("com.test.plug");
    [[maybe_unused]] auto ctxId1 = bridge->registry().registerContext(ctx);

    // Connect a loopback client as "com.test.plug".
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Feed a KeyPressed for index=3 (1-based) into the bridge.
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::KeyPressed;
    ev.index = 3; // 1-based key 3 -> {row:0, col:2}
    ev.value = 0;
    bridge->onDeviceEvent(QStringLiteral("akp05e"), ev);

    // Pump and assert the client received a keyDown with the correct coordinates.
    pump19(500);

    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("keyDown")));

    auto const payload = firstPayloadForEvent(msgSpy, QStringLiteral("keyDown"));
    auto const coords = payload.value(QStringLiteral("coordinates")).toObject();
    CHECK(coords.value(QStringLiteral("row")).toInt() == 0);
    CHECK(coords.value(QStringLiteral("column")).toInt() == 2);
    CHECK(payload.value(QStringLiteral("isInMultiAction")).toBool() == false);
}

// ---------------------------------------------------------------------------
// 19-03 e2e: dialRotate with signed ticks (PLUGIN-10)
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E outbound dialRotate delivers signed ticks to bound plugin",
          "[plugin-device-bridge][e2e][outbound][PLUGIN-10]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Pre-register an encoder context for encoder index 0 (column=0, row=0, Encoder).
    ajazz::app::ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 0; // encoder index 0
    ctx.controller = QStringLiteral("Encoder");
    ctx.actionUUID = QStringLiteral("com.test.plug.enc.action");
    ctx.pluginUuid = QStringLiteral("com.test.plug");
    [[maybe_unused]] auto encCtxId = bridge->registry().registerContext(ctx);

    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Feed EncoderTurned with negative delta (CCW rotation).
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::EncoderTurned;
    ev.index = 0;  // 0-based encoder 0
    ev.value = -2; // signed delta
    bridge->onDeviceEvent(QStringLiteral("akp05e"), ev);

    pump19(500);

    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("dialRotate")));

    auto const payload = firstPayloadForEvent(msgSpy, QStringLiteral("dialRotate"));
    CHECK(payload.value(QStringLiteral("ticks")).toInt() == -2);
    CHECK(payload.value(QStringLiteral("controller")).toString() == QStringLiteral("Encoder"));
    CHECK(payload.value(QStringLiteral("pressed")).toBool() == false);
}

// ---------------------------------------------------------------------------
// 19-03 e2e: willAppear on plugin registration (PLUGIN-10)
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E willAppear sent on plugin registration with bound action",
          "[plugin-device-bridge][e2e][lifecycle][PLUGIN-10]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    // Bridge with a profile accessor that exposes a key 3 (0-based profile index 2)
    // bound to a plugin action owned by "com.test.plug".
    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Build a Profile with key 2 (0-based) bound to com.test.plug.action1.
    ajazz::core::Profile prof;
    prof.id = "test-profile";
    prof.name = "Test";
    prof.deviceCodename = "akp05e";
    // Key index 2 (0-based in Profile::keys) = 1-based device key 3 = {row:0, col:2}.
    ajazz::core::Binding binding;
    ajazz::core::Action act;
    act.kind = ajazz::core::ActionKind::Plugin;
    act.id = "com.test.plug.action1";
    binding.onPress.push_back(act);
    prof.keys[2] = std::move(binding);

    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });

    // Connect a loopback client as "com.test.plug".
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Simulate the bridge receiving the pluginRegistered signal.
    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));

    // Pump and assert the client received willAppear.
    pump19(500);

    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("willAppear")));

    // Full Elgato envelope: action/context/device are TOP-LEVEL siblings to event.
    auto const event = firstEventForEvent(msgSpy, QStringLiteral("willAppear"));
    CHECK(event.value(QStringLiteral("action")).toString() ==
          QStringLiteral("com.test.plug.action1"));
    CHECK(event.value(QStringLiteral("device")).toString() == QStringLiteral("akp05e"));
    CHECK_FALSE(event.value(QStringLiteral("context")).toString().isEmpty());
    auto const payload = event.value(QStringLiteral("payload")).toObject();
    auto const coords = payload.value(QStringLiteral("coordinates")).toObject();
    CHECK(coords.value(QStringLiteral("row")).toInt() == 0);
    CHECK(coords.value(QStringLiteral("column")).toInt() == 2);
    CHECK(payload.value(QStringLiteral("controller")).toString() == QStringLiteral("Keypad"));
    CHECK(payload.contains(QStringLiteral("settings")));
}

TEST_CASE("PluginDeviceBridgeE2E willAppear sent when action UUID is NOT a dotted prefix of "
          "plugin UUID via stored-owner resolver",
          "[plugin-device-bridge][e2e][lifecycle][owner]") {
    // Regression for GAP-PLUGIN-OWNER: a plugin whose action UUIDs are NOT dotted
    // children of the plugin UUID (e.g. plugin "com.test.plug", action "sysmon.cpu")
    // must still receive willAppear. The legacy ownerForActionUuid dotted-prefix
    // match returns empty for such a pair, silently dropping willAppear. The
    // injected stored-owner resolver (PluginManager::ownerForAction in production)
    // resolves the owner from the manifest instead. This mirrors OpenDeck stamping
    // action.plugin at load.
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    ajazz::core::Profile prof;
    prof.id = "test-profile";
    prof.name = "Test";
    prof.deviceCodename = "akp05e";
    ajazz::core::Binding binding;
    ajazz::core::Action act;
    act.kind = ajazz::core::ActionKind::Plugin;
    act.id = "sysmon.cpu"; // deliberately NOT a dotted child of com.test.plug
    binding.onPress.push_back(act);
    prof.keys[0] = std::move(binding); // key 1 -> {row:0, col:0}

    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });
    // Stored-owner map: sysmon.cpu is owned by com.test.plug.
    bridge->setActionOwnerResolver([](QString const& actionUuid) -> QString {
        return actionUuid == QStringLiteral("sysmon.cpu") ? QStringLiteral("com.test.plug")
                                                          : QString{};
    });

    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));
    pump19(500);

    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("willAppear")));
}

// ---------------------------------------------------------------------------
// 19-03 e2e: unbound-coordinate drop (T-19-leak) + no cross-plugin leak
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E outbound unbound coordinate sends no event no crash",
          "[plugin-device-bridge][e2e][outbound][security]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);
    // No contexts registered — every key press should be silently dropped.

    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Feed a KeyPressed for key 3 (unbound — no context registered).
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::KeyPressed;
    ev.index = 3;
    ev.value = 0;
    bridge->onDeviceEvent(QStringLiteral("akp05e"), ev);

    pump19(500);

    // The client must receive NO keyDown event (silent drop, T-19-leak).
    auto const names = receivedEventNames(msgSpy);
    CHECK_FALSE(names.contains(QStringLiteral("keyDown")));
}

TEST_CASE("PluginDeviceBridgeE2E outbound event for other-plugin context does not reach us",
          "[plugin-device-bridge][e2e][outbound][security]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Register key 4 (1-based) owned by "com.other.plug" (at {row:0, col:3}).
    ajazz::app::ActionContext otherCtx;
    otherCtx.deviceId = QStringLiteral("akp05e");
    otherCtx.pageId = QStringLiteral("root");
    otherCtx.row = 0;
    otherCtx.column = 3; // 1-based key 4 -> {row:0, col:3}
    otherCtx.controller = QStringLiteral("Keypad");
    otherCtx.actionUUID = QStringLiteral("com.other.plug.action1");
    otherCtx.pluginUuid = QStringLiteral("com.other.plug");
    [[maybe_unused]] auto otherCtxId = bridge->registry().registerContext(otherCtx);

    // Connect "com.test.plug" (NOT the owner of key 4's context).
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Feed KeyPressed for key 4 (owned by "com.other.plug").
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::KeyPressed;
    ev.index = 4; // 1-based key 4 -> {row:0, col:3}
    ev.value = 0;
    bridge->onDeviceEvent(QStringLiteral("akp05e"), ev);

    pump19(500);

    // com.test.plug must NOT receive the keyDown (cross-plugin leakage prevention).
    auto const names = receivedEventNames(msgSpy);
    CHECK_FALSE(names.contains(QStringLiteral("keyDown")));
}

// ---------------------------------------------------------------------------
// CR-02 e2e: two devices sharing the same coord -- events route to the correct
// per-device plugin; retire of one device does not affect the other.
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E CR-02 two-device isolation keyDown routes to correct plugin",
          "[plugin-device-bridge][e2e][outbound][security][CR-02]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Register key 3 (1-based, row:0 col:2) for "akp05e" -> com.plugA.
    ajazz::app::ActionContext ctxA;
    ctxA.deviceId = QStringLiteral("akp05e");
    ctxA.pageId = QStringLiteral("root");
    ctxA.row = 0;
    ctxA.column = 2;
    ctxA.controller = QStringLiteral("Keypad");
    ctxA.actionUUID = QStringLiteral("com.plugA.action1");
    ctxA.pluginUuid = QStringLiteral("com.plugA");
    [[maybe_unused]] auto idA = bridge->registry().registerContext(ctxA);

    // Register the same coord (row:0 col:2) for "akp153" -> com.plugB.
    ajazz::app::ActionContext ctxB;
    ctxB.deviceId = QStringLiteral("akp153");
    ctxB.pageId = QStringLiteral("root");
    ctxB.row = 0;
    ctxB.column = 2;
    ctxB.controller = QStringLiteral("Keypad");
    ctxB.actionUUID = QStringLiteral("com.plugB.action1");
    ctxB.pluginUuid = QStringLiteral("com.plugB");
    [[maybe_unused]] auto idB = bridge->registry().registerContext(ctxB);

    // Connect both clients.
    QWebSocket clientA;
    QSignalSpy spyA(&clientA, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(clientA, server, QStringLiteral("com.plugA"), registeredSpy));

    QSignalSpy registeredSpy2(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    QWebSocket clientB;
    QSignalSpy spyB(&clientB, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(clientB, server, QStringLiteral("com.plugB"), registeredSpy2));

    // Feed KeyPressed for key 3 (1-based) from "akp05e".
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::KeyPressed;
    ev.index = 3; // 1-based key 3 -> row:0 col:2
    ev.value = 0;
    bridge->onDeviceEvent(QStringLiteral("akp05e"), ev);

    pump19(500);

    // Only com.plugA (the akp05e owner) must receive keyDown; com.plugB must not.
    auto const namesA = receivedEventNames(spyA);
    auto const namesB = receivedEventNames(spyB);
    CHECK(namesA.contains(QStringLiteral("keyDown")));
    CHECK_FALSE(namesB.contains(QStringLiteral("keyDown")));

    // Feed the same event from "akp153" — now com.plugB must receive it.
    spyA.clear();
    spyB.clear();
    bridge->onDeviceEvent(QStringLiteral("akp153"), ev);
    pump19(500);

    auto const namesA2 = receivedEventNames(spyA);
    auto const namesB2 = receivedEventNames(spyB);
    CHECK_FALSE(namesA2.contains(QStringLiteral("keyDown")));
    CHECK(namesB2.contains(QStringLiteral("keyDown")));
}

// ---------------------------------------------------------------------------
// 28-04: populateContextsForActivePage registers encoder + touch-zone contexts
// ---------------------------------------------------------------------------

// Test: encoder[0].onPress Plugin binding -> byCoord("akp05e","Encoder",0,0) has value
// + willAppear emitted. Proves the existing encoder enumeration path and that
// activeDeviceId() is accessible.
TEST_CASE("PluginDeviceBridge populateContexts registers encoder plugin context",
          "[plugin-device-bridge][e2e][lifecycle][PLUGIN-19]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Build a Profile with encoder[0].onPress bound to a plugin action.
    ajazz::core::Profile prof;
    prof.id = "test-profile-enc";
    prof.name = "Test Encoder";
    prof.deviceCodename = "akp05e";

    ajazz::core::EncoderBinding encBinding;
    ajazz::core::Action encAct;
    encAct.kind = ajazz::core::ActionKind::Plugin;
    encAct.id = "com.test.plug.enc.action1";
    encBinding.onPress.push_back(encAct);
    prof.encoders[0] = std::move(encBinding);

    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });

    // Connect a loopback client as "com.test.plug".
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Register the plugin with the bridge so ownerForActionUuid can resolve it.
    // (mirrors the onPluginRegistered slot that fires in the live app when the
    // plugin connects to the WebSocket server).
    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));
    msgSpy.clear(); // discard the willAppear from onPluginRegistered itself

    // Call populateContextsForActivePage directly (mirrors onDeviceConnected path).
    bridge->populateContextsForActivePage(QStringLiteral("akp05e"));

    pump19(500);

    // The ContextRegistry must have an entry at (akp05e, Encoder, row=0, col=0).
    auto const ctxOpt =
        bridge->registry().byCoord(QStringLiteral("akp05e"), QStringLiteral("Encoder"), 0, 0);
    CHECK(ctxOpt.has_value());

    // willAppear must have been emitted to the client.
    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("willAppear")));
}

// Test: touchZones[1].onTap Plugin binding -> byCoord("akp05e","Encoder",0,1) has value
// after populateContextsForActivePage.  This exercises the NEW touch-zone enumeration
// path added in 28-04.  controller="Encoder" matches the LOCKED byCoord lookup in
// onDeviceEvent's TouchUp case (~line 628).
TEST_CASE("PluginDeviceBridge populateContexts registers touch zone as Encoder context",
          "[plugin-device-bridge][e2e][lifecycle][PLUGIN-19]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Build a Profile with touchZones[1].onTap bound to a plugin action.
    ajazz::core::Profile prof;
    prof.id = "test-profile-tz";
    prof.name = "Test TouchZone";
    prof.deviceCodename = "akp05e";

    ajazz::core::TouchZoneBinding tzBinding;
    ajazz::core::Action tzAct;
    tzAct.kind = ajazz::core::ActionKind::Plugin;
    tzAct.id = "com.test.plug.tz.action1";
    tzBinding.onTap.push_back(tzAct);
    prof.touchZones[1] = std::move(tzBinding);

    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });

    // Connect a loopback client as "com.test.plug".
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Register the plugin with the bridge so ownerForActionUuid can resolve it.
    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));
    msgSpy.clear(); // discard the willAppear from onPluginRegistered itself

    // Call populateContextsForActivePage.
    bridge->populateContextsForActivePage(QStringLiteral("akp05e"));

    pump19(500);

    // Touch zone at index 1 must be registered under controller="Encoder", row=0, col=1.
    // This matches the LOCKED onDeviceEvent TouchUp lookup:
    //   m_registry.byCoord(deviceId, "Encoder", 0, zone)  (~line 628)
    // Do NOT change that lookup — only feed it the registration it expects.
    auto const ctxOpt =
        bridge->registry().byCoord(QStringLiteral("akp05e"), QStringLiteral("Encoder"), 0, 1);
    CHECK(ctxOpt.has_value());

    // willAppear must have been emitted to the client.
    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("willAppear")));
}

// ---------------------------------------------------------------------------
// GAP-28B regression: injectSyntheticEvent must deliver dialDown and keyDown
// to the plugin via the deviceEvent signal path.
//
// The gap that tests #708-709 missed: they call populateContextsForActivePage
// directly and assert byCoord has a value.  They do NOT verify that
// StreamDockInputService::injectSyntheticEvent -> dispatch -> emit deviceEvent
// -> PluginDeviceBridge::onDeviceEvent -> sendEvent actually fires.
//
// This test wires the two services together (as Application does via
// QObject::connect), calls injectSyntheticEvent, and asserts the loopback
// plugin client receives the expected dialDown / keyDown event.
// ---------------------------------------------------------------------------

TEST_CASE("PluginDeviceBridgeE2E GAP-28B injectSyntheticEvent delivers dialDown via deviceEvent",
          "[plugin-device-bridge][e2e][gap-28b]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    // Build bridge (no control service needed for input->plugin routing test).
    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Profile: encoder[0].onPress -> plugin action "com.test.plug.enc"
    ajazz::core::Profile prof;
    prof.id = "gap28b-enc";
    ajazz::core::EncoderBinding encBinding;
    ajazz::core::Action encAct;
    encAct.kind = ajazz::core::ActionKind::Plugin;
    encAct.id = "com.test.plug.enc";
    encBinding.onPress.push_back(encAct);
    prof.encoders[0] = std::move(encBinding);

    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });

    // Build a minimal StreamDockInputService (no device handle needed for
    // injectSyntheticEvent — dispatch() does not require m_device to be set).
    ajazz::core::ActionExecutors nopExecs;
    auto engine = std::make_unique<ajazz::core::ActionEngine>(std::move(nopExecs));
    ajazz::app::StreamDockInputService inputSvc(
        [&prof]() -> ajazz::core::Profile const& { return prof; }, std::move(engine), nullptr);
    // Set the active device codename so deviceEvent carries "akp05e" (not empty).
    inputSvc.setActiveDeviceCodename(QStringLiteral("akp05e"));

    // Wire the services: deviceEvent -> onDeviceEvent (as Application does).
    QObject::connect(&inputSvc,
                     &ajazz::app::StreamDockInputService::deviceEvent,
                     bridge.get(),
                     &ajazz::app::PluginDeviceBridge::onDeviceEvent);

    // Connect a loopback plugin client and register it.
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    // Populate contexts (as commitEncoderBinding -> profileChanged -> lambda would do).
    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));
    bridge->populateContextsForActivePage(QStringLiteral("akp05e"));
    pump19(200);
    msgSpy.clear(); // discard willAppear

    // Verify context was registered before testing input delivery.
    REQUIRE(bridge->registry()
                .byCoord(QStringLiteral("akp05e"), QStringLiteral("Encoder"), 0, 0)
                .has_value());

    // Inject a synthetic EncoderPressed for encoder 0.
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::EncoderPressed;
    ev.index = 0;
    ev.value = 0;
    inputSvc.injectSyntheticEvent(ev);
    pump19(400);

    // The plugin client must receive dialDown.
    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("dialDown")));
}

TEST_CASE("PluginDeviceBridgeE2E GAP-28B injectSyntheticEvent delivers keyDown via deviceEvent",
          "[plugin-device-bridge][e2e][gap-28b]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Profile: key 3 (1-based) onPress -> plugin action "com.test.plug.key"
    // 1-based key 3 -> row=0, col=2 (keyIndex=3, cols=5: row=(3-1)/5=0, col=(3-1)%5=2)
    ajazz::core::Profile prof;
    prof.id = "gap28b-key";
    ajazz::core::Binding keyBinding;
    ajazz::core::Action keyAct;
    keyAct.kind = ajazz::core::ActionKind::Plugin;
    keyAct.id = "com.test.plug.key";
    keyBinding.onPress.push_back(keyAct);
    prof.keys[2] = std::move(keyBinding); // profile uses 0-based index 2 for 1-based key 3

    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });

    ajazz::core::ActionExecutors nopExecs;
    auto engine = std::make_unique<ajazz::core::ActionEngine>(std::move(nopExecs));
    ajazz::app::StreamDockInputService inputSvc(
        [&prof]() -> ajazz::core::Profile const& { return prof; }, std::move(engine), nullptr);
    inputSvc.setActiveDeviceCodename(QStringLiteral("akp05e"));

    QObject::connect(&inputSvc,
                     &ajazz::app::StreamDockInputService::deviceEvent,
                     bridge.get(),
                     &ajazz::app::PluginDeviceBridge::onDeviceEvent);

    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));
    bridge->populateContextsForActivePage(QStringLiteral("akp05e"));
    pump19(200);
    msgSpy.clear(); // discard willAppear

    // Verify key context was registered.
    // Profile::keys uses 0-based index 2; populateContextsForActivePage converts to
    // 1-based (keyIdx1 = 2+1 = 3) and then to grid coords for a 5-column grid.
    // coordsForKeyIndex(3, 5): row=(3-1)/5=0, col=(3-1)%5=2.
    REQUIRE(bridge->registry()
                .byCoord(QStringLiteral("akp05e"), QStringLiteral("Keypad"), 0, 2)
                .has_value());

    // Inject KeyPressed for 1-based index 3.
    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::KeyPressed;
    ev.index = 3; // 1-based
    ev.value = 0;
    inputSvc.injectSyntheticEvent(ev);
    pump19(400);

    auto const names = receivedEventNames(msgSpy);
    CHECK(names.contains(QStringLiteral("keyDown")));
}

#endif // AJAZZ_HAVE_WEBSOCKETS (Phase 19-02 + 19-03)
