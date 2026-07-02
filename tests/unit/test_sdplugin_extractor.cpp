// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_sdplugin_extractor.cpp
 * @brief Round-trip tests for the `.sdPlugin` archive extractor (issue #62).
 *
 * Each test builds a synthetic `.sdPlugin` zip via QZipWriter, then runs
 * it through extractSdPluginArchive() / extractStandalonePluginArchives()
 * and asserts the resulting on-disk shape. Both helpers live in
 * `src/app/src/sdplugin_extractor.{hpp,cpp}` and are linked PRIVATE into
 * this test binary alongside `Qt6::CorePrivate` (the only Qt module that
 * exposes QZipReader / QZipWriter).
 */
#include "sdplugin_extractor.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>
#include <private/qzipwriter_p.h>

using namespace ajazz::app;

namespace {

/// Build a synthetic `.sdPlugin` archive at @p archivePath. When
/// @p wrapperDir is non-empty, each file is nested under that top-level
/// directory (the canonical Elgato layout); otherwise files land at the
/// archive root.
///
/// On Windows the QZipWriter holds an exclusive lock on the file via its
/// internal QFile until the writer is destroyed. We scope the writer in
/// a sub-block + explicit close() so the lock is released BEFORE QZipReader
/// in the test body tries to open the same path. Without the sub-block,
/// QZipReader returns "not a zip file" on Win32 even though the bytes
/// on disk are a valid PK archive.
void buildSdPluginArchive(QString const& archivePath, QString const& wrapperDir) {
    {
        QZipWriter zip(archivePath);
        REQUIRE(zip.status() == QZipWriter::NoError);
        QString const prefix = wrapperDir.isEmpty() ? QString{} : wrapperDir + QStringLiteral("/");
        zip.addFile(prefix + QStringLiteral("manifest.json"),
                    QByteArray(R"({"UUID":"com.example.foo","Name":"Foo"})"));
        zip.addFile(prefix + QStringLiteral("Code/index.html"),
                    QByteArray("<!doctype html><title>foo</title>"));
        zip.addFile(prefix + QStringLiteral("Icons/icon.png"), QByteArray("\x89PNG\r\n\x1a\n", 8));
        zip.close();
        REQUIRE(zip.status() == QZipWriter::NoError);
    }
}

} // namespace

TEST_CASE("extractSdPluginArchive strips a single-folder wrapper", "[plugin-store][issue-62]") {
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString const archive = scratch.filePath(QStringLiteral("com.example.foo.sdPlugin"));
    buildSdPluginArchive(archive, QStringLiteral("com.example.foo.sdPlugin"));
    REQUIRE(QFileInfo::exists(archive));

    QString const target = QStringLiteral("com.example.foo.sdPlugin");
    REQUIRE(extractSdPluginArchive(archive, scratch.path(), target));

    QDir const out(scratch.filePath(target));
    REQUIRE(out.exists());
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("manifest.json"))));
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("Code/index.html"))));
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("Icons/icon.png"))));

    // The staging dir must not leak after a successful extraction.
    REQUIRE_FALSE(QFileInfo::exists(scratch.filePath(QStringLiteral(".tmp_") + target)));
}

TEST_CASE("extractSdPluginArchive accepts an archive with no top-level wrapper",
          "[plugin-store][issue-62]") {
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString const archive = scratch.filePath(QStringLiteral("rooted.sdPlugin"));
    buildSdPluginArchive(archive, /*wrapperDir*/ QString{});

    QString const target = QStringLiteral("rooted.sdPlugin");
    REQUIRE(extractSdPluginArchive(archive, scratch.path(), target));

    QDir const out(scratch.filePath(target));
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("manifest.json"))));
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("Code/index.html"))));
}

TEST_CASE("extractSdPluginArchive overwrites a previous extraction at the target",
          "[plugin-store][issue-62]") {
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    // Stage the archive under a distinct name so the target subdirectory
    // pre-seeding below doesn't collide with the source archive file.
    QString const archive = scratch.filePath(QStringLiteral("over.archive.sdPlugin"));
    buildSdPluginArchive(archive, QStringLiteral("over.sdPlugin"));

    QString const target = QStringLiteral("over.sdPlugin");
    // Pre-seed the target dir with a stale file that must NOT survive
    // the second extract.
    QString const staleDir = scratch.filePath(target);
    QDir().mkpath(staleDir);
    QFile stale(staleDir + QStringLiteral("/stale.txt"));
    REQUIRE(stale.open(QIODevice::WriteOnly));
    stale.write("old");
    stale.close();

    REQUIRE(extractSdPluginArchive(archive, scratch.path(), target));
    REQUIRE_FALSE(QFileInfo::exists(staleDir + QStringLiteral("/stale.txt")));
    REQUIRE(QFileInfo::exists(staleDir + QStringLiteral("/manifest.json")));
}

TEST_CASE("extractSdPluginArchive replaces the archive file at the target path",
          "[plugin-store][issue-62]") {
    // Mirrors the production install path: the download lands at
    // <destDir>/<id>.sdPlugin AS A FILE, then we extract in place to
    // <destDir>/<id>.sdPlugin AS A DIRECTORY. The extractor must clear
    // the file before renaming or the rename silently fails on Win32.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString const target = QStringLiteral("inplace.sdPlugin");
    QString const archive = scratch.filePath(target);
    buildSdPluginArchive(archive, QStringLiteral("inplace.sdPlugin"));
    REQUIRE(QFileInfo(archive).isFile());

    REQUIRE(extractSdPluginArchive(archive, scratch.path(), target));
    REQUIRE(QFileInfo(archive).isDir());
    REQUIRE(QFileInfo::exists(scratch.filePath(target + QStringLiteral("/manifest.json"))));
}

TEST_CASE("extractSdPluginArchive fails gracefully on a non-zip input",
          "[plugin-store][issue-62]") {
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString const bogus = scratch.filePath(QStringLiteral("not-a-zip.sdPlugin"));
    QFile f(bogus);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("definitely not zip bytes");
    f.close();

    REQUIRE_FALSE(
        extractSdPluginArchive(bogus, scratch.path(), QStringLiteral("not-a-zip.sdPlugin")));
    // Failure path leaves the source archive untouched and the target dir absent.
    REQUIRE(QFileInfo::exists(bogus));
    REQUIRE_FALSE(QDir(scratch.filePath(QStringLiteral("not-a-zip.sdPlugin"))).exists());
}

TEST_CASE("extractStandalonePluginArchives sweeps a plugins dir and deletes consumed archives",
          "[plugin-store][issue-62]") {
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    QString const pluginsDir = scratch.path();

    // Two valid archives + one bogus file. After the sweep the two valid
    // archives are extracted to directories and the originals removed;
    // the bogus file is left in place.
    QString const a1 = pluginsDir + QStringLiteral("/com.example.alpha.sdPlugin");
    QString const a2 = pluginsDir + QStringLiteral("/com.example.beta.sdPlugin");
    QString const bogus = pluginsDir + QStringLiteral("/garbage.sdPlugin");

    buildSdPluginArchive(a1, QStringLiteral("com.example.alpha.sdPlugin"));
    buildSdPluginArchive(a2, QString{});
    QFile f(bogus);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("nope");
    f.close();

    extractStandalonePluginArchives(pluginsDir);

    // a1 + a2 are now directories (archives removed).
    REQUIRE(QDir(a1).exists());
    REQUIRE(QDir(a2).exists());
    REQUIRE_FALSE(QFileInfo(a1).isFile());
    REQUIRE_FALSE(QFileInfo(a2).isFile());
    REQUIRE(QFileInfo::exists(a1 + QStringLiteral("/manifest.json")));
    REQUIRE(QFileInfo::exists(a2 + QStringLiteral("/manifest.json")));

    // Bogus file is preserved untouched (sweep is best-effort).
    REQUIRE(QFileInfo(bogus).isFile());
}

TEST_CASE("extractSdPluginArchive rejects a zip-slip path-traversal entry",
          "[plugin-store][security]") {
    // A hostile archive carries a traversal entry that climbs out of the
    // staging dir to overwrite a file outside the destination. These
    // archives arrive over HTTPS (PluginCatalogModel::install) and from any
    // file dropped in the plugins dir, so the extractor must reject the
    // traversal and write nothing outside its staging root.
    // Regression for the zip-slip finding (Phase 13 CR-01).
    //
    // Note: QZipReader (Qt 6.11) strips a *leading* "../" or "/", so the
    // textbook "../../escape.txt" is neutralised on read. It does NOT strip
    // ".." that follows a real path segment, so the live exploit entry is
    // "x/../../../escape.txt", which resolves to <scratch>/escape.txt — one
    // level above destDir. The malicious archive is built by Python's
    // zipfile (which preserves the raw name) and embedded here as base64;
    // QZipWriter cannot produce it because it sanitises names on write.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    // destDir is a subdir so the surviving traversal lands above it.
    QString const destDir = scratch.filePath(QStringLiteral("dest"));
    QDir().mkpath(destDir);

    // Entries: benign "manifest.json" + traversal "x/../../../escape.txt".
    static constexpr char kEvilZipB64[] =
        "UEsDBBQAAAAIAOCFtlyYhKfZIAAAACEAAAANAAAAbWFuaWZlc3QuanNvbqtWCg31dFGyUkrO"
        "z9VLLcvMUdJR8kvMTVWyUnIF8WoBUEsDBBQAAAAIAOCFtlx+UwTZBwAAAAUAAAAVAAAAeC8u"
        "Li8uLi8uLi9lc2NhcGUudHh0KyjPS00BAFBLAQIUAxQAAAAIAOCFtlyYhKfZIAAAACEAAAAN"
        "AAAAAAAAAAAAAACAAQAAAABtYW5pZmVzdC5qc29uUEsBAhQDFAAAAAgA4IW2XH5TBNkHAAAA"
        "BQAAABUAAAAAAAAAAAAAAIABSwAAAHgvLi4vLi4vLi4vZXNjYXBlLnR4dFBLBQYAAAAAAgAC"
        "AH4AAACFAAAAAAA=";

    QString const archive = destDir + QStringLiteral("/evil.sdPlugin");
    {
        QFile f(archive);
        REQUIRE(f.open(QIODevice::WriteOnly));
        QByteArray const bytes =
            QByteArray::fromBase64(QByteArray::fromRawData(kEvilZipB64, sizeof(kEvilZipB64) - 1));
        REQUIRE(f.write(bytes) == bytes.size());
        f.close();
    }

    QString const escaped = scratch.filePath(QStringLiteral("escape.txt"));
    REQUIRE_FALSE(QFileInfo::exists(escaped)); // precondition: nothing there yet

    QString const target = QStringLiteral("evil.sdPlugin");
    REQUIRE_FALSE(extractSdPluginArchive(archive, destDir, target));

    // The traversal target was never written, the staging dir is cleaned up,
    // and no partial extraction landed at the final path.
    REQUIRE_FALSE(QFileInfo::exists(escaped));
    REQUIRE_FALSE(QDir(destDir + QStringLiteral("/.tmp_") + target).exists());
    REQUIRE_FALSE(QDir(destDir + QStringLiteral("/") + target).exists());
}

TEST_CASE("extractSdPluginArchive extracts a ZIP64 archive with real file contents",
          "[plugin-store][zip64]") {
    // Real `.streamDeckPlugin` packagers (Node `archiver`, many tools) emit
    // ZIP64 entries — the 32-bit size fields are 0xFFFFFFFF sentinels and the
    // true sizes live in the central-directory extra field (header 0x0001),
    // often paired with a data-descriptor flag. Qt's QZipReader trusted the
    // local-header sizes and extracted ZERO bytes for these, so manifests came
    // out empty and every such plugin failed to install. The extractor now
    // sources sizes from the central directory and raw-inflates via zlib.
    // This fixture (built by Python's zipfile with force_zip64=True) carries a
    // wrapper dir `com.test.zip64.sdPlugin/` with a real manifest.json + a code
    // file; the test asserts both extract with their FULL contents.
    static constexpr char kZip64B64[] =
        "UEsDBC0AAAAIAAAAIQDRTAqo//////////8lABQAY29tLnRlc3QuemlwNjQuc2RQbHVnaW4v"
        "bWFuaWZlc3QuanNvbgEAEABBAAAAAAAAAEMAAAAAAAAAq1YKDfV0UbJSSs7P1StJLS7Rq8os"
        "MDNR0lHyS8xNVbJSilLSUQpLLSrOzM9TslIy1DNQ0lFyTC7JzM8rVrKKjq0FAFBLAwQtAAAA"
        "CAAAACEAPe4jIP//////////JQAUAGNvbS50ZXN0LnppcDY0LnNkUGx1Z2luL2Jpbi9wbHVn"
        "aW4uanMBABAADwAAAAAAAAARAAAAAAAAAEvOzyvOz0nVy8lP1zDUtAYAUEsBAi0DLQAAAAgA"
        "AAAhANFMCqhDAAAAQQAAACUAAAAAAAAAAAAAAIABAAAAAGNvbS50ZXN0LnppcDY0LnNkUGx1"
        "Z2luL21hbmlmZXN0Lmpzb25QSwECLQMtAAAACAAAACEAPe4jIBEAAAAPAAAAJQAAAAAAAAAA"
        "AAAAgAGaAAAAY29tLnRlc3QuemlwNjQuc2RQbHVnaW4vYmluL3BsdWdpbi5qc1BLBQYAAAAA"
        "AgACAKYAAAACAQAAAAA=";

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    QString const destDir = scratch.path();
    QString const archive = destDir + QStringLiteral("/z64.sdPlugin");
    {
        QFile f(archive);
        REQUIRE(f.open(QIODevice::WriteOnly));
        QByteArray const bytes =
            QByteArray::fromBase64(QByteArray::fromRawData(kZip64B64, sizeof(kZip64B64) - 1));
        REQUIRE(f.write(bytes) == bytes.size());
        f.close();
    }

    QString const target = QStringLiteral("com.test.zip64.sdPlugin");
    REQUIRE(extractSdPluginArchive(archive, destDir, target));

    // The wrapper dir was stripped; the manifest extracted with its REAL bytes
    // (the regression: it used to be a 0-byte file).
    QString const manifestPath =
        destDir + QStringLiteral("/") + target + QStringLiteral("/manifest.json");
    REQUIRE(QFileInfo::exists(manifestPath));
    QFile mf(manifestPath);
    REQUIRE(mf.open(QIODevice::ReadOnly));
    QByteArray const manifest = mf.readAll();
    mf.close();
    REQUIRE(manifest.size() > 0);
    REQUIRE(manifest.contains("com.test.zip64"));

    // The nested code file also extracted with content.
    QString const codePath =
        destDir + QStringLiteral("/") + target + QStringLiteral("/bin/plugin.js");
    REQUIRE(QFileInfo::exists(codePath));
    QFile cf(codePath);
    REQUIRE(cf.open(QIODevice::ReadOnly));
    QByteArray const code = cf.readAll();
    cf.close();
    REQUIRE(code.contains("console.log"));
}

TEST_CASE("extractSdPluginArchive parses a ZIP64 archive structure (EOCD64 record + locator)",
          "[plugin-store][zip64]") {
    // Archive-level ZIP64 (audit 3.10): the EOCD64 record + locator precede
    // the EOCD, whose entry-count/offset fields are 0xFFFF/0xFFFFFFFF
    // sentinels. The parser must source the central-directory offset + entry
    // count from the EOCD64 record. Fixture built by Python zipfile, then
    // post-processed to insert the EOCD64 record/locator and sentinel-ise the
    // EOCD (scratch script make_zip64_eocd_fixture.py).
    static constexpr char kEocd64B64[] =
        "UEsDBBQAAAAIACFu4lyqi3FQQQAAAD8AAAAmAAAAY29tLnRlc3QuZW9jZDY0LnNkUGx1Z2luL21h"
        "bmlmZXN0Lmpzb26rVgoN9XRRslJKzs/VK0ktLtFLzU9OMTNR0lHyS8xNVbJScvV3djEzUXDLrCgp"
        "LUpV0lFyTC7JzM8rVrKKjq0FAFBLAwQUAAAACAAhbuJcroqfNBUAAAATAAAAJgAAAGNvbS50ZXN0"
        "LmVvY2Q2NC5zZFBsdWdpbi9iaW4vcGx1Z2luLmpzS87PK87PSdXLyU/XUMrIVNK05gIAUEsBAhQD"
        "FAAAAAgAIW7iXKqLcVBBAAAAPwAAACYAAAAAAAAAAAAAAIABAAAAAGNvbS50ZXN0LmVvY2Q2NC5z"
        "ZFBsdWdpbi9tYW5pZmVzdC5qc29uUEsBAhQDFAAAAAgAIW7iXK6KnzQVAAAAEwAAACYAAAAAAAAA"
        "AAAAAIABhQAAAGNvbS50ZXN0LmVvY2Q2NC5zZFBsdWdpbi9iaW4vcGx1Z2luLmpzUEsGBiwAAAAA"
        "AAAALQAtAAAAAAAAAAAAAgAAAAAAAAACAAAAAAAAAKgAAAAAAAAA3gAAAAAAAABQSwYHAAAAAIYB"
        "AAAAAAAAAQAAAFBLBQYAAAAA////////////////AAA=";

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    QString const destDir = scratch.path();
    QString const archive = destDir + QStringLiteral("/eocd64.sdPlugin");
    {
        QFile f(archive);
        REQUIRE(f.open(QIODevice::WriteOnly));
        QByteArray const bytes =
            QByteArray::fromBase64(QByteArray::fromRawData(kEocd64B64, sizeof(kEocd64B64) - 1));
        REQUIRE(f.write(bytes) == bytes.size());
        f.close();
    }

    QString const target = QStringLiteral("com.test.eocd64.sdPlugin");
    REQUIRE(extractSdPluginArchive(archive, destDir, target));

    QFile mf(destDir + QStringLiteral("/") + target + QStringLiteral("/manifest.json"));
    REQUIRE(mf.open(QIODevice::ReadOnly));
    REQUIRE(mf.readAll().contains("com.test.eocd64"));
    REQUIRE(QFileInfo::exists(destDir + QStringLiteral("/") + target +
                              QStringLiteral("/bin/plugin.js")));
}

TEST_CASE("extractSdPluginArchive rejects a wrapper directory with no manifest.json",
          "[plugin-store][issue-62]") {
    // Audit 3.11: a zip whose sole folder carries no manifest.json used to be
    // promoted as a successful install that discovery could never find. The
    // extractor must now fail and clean up instead.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString const archive = scratch.filePath(QStringLiteral("nomanifest.sdPlugin"));
    {
        QZipWriter zip(archive);
        REQUIRE(zip.status() == QZipWriter::NoError);
        zip.addFile(QStringLiteral("wrapper/readme.txt"), QByteArray("not a plugin"));
        zip.close();
        REQUIRE(zip.status() == QZipWriter::NoError);
    }

    QString const target = QStringLiteral("nomanifest.sdPlugin");
    REQUIRE_FALSE(extractSdPluginArchive(archive, scratch.path(), target));
    REQUIRE_FALSE(QDir(scratch.filePath(target)).exists());
    REQUIRE_FALSE(QDir(scratch.filePath(QStringLiteral(".tmp_") + target)).exists());
}

TEST_CASE("extractSdPluginArchive unwraps a doubly-nested single-folder wrapper",
          "[plugin-store][issue-62]") {
    // Hand-zipped bundles often compress the PARENT of the plugin folder,
    // producing outer/inner.sdPlugin/manifest.json. The wrapper descent must
    // keep going until the level that holds the manifest.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString const archive = scratch.filePath(QStringLiteral("double.sdPlugin"));
    buildSdPluginArchive(archive, QStringLiteral("outer/com.example.double.sdPlugin"));

    QString const target = QStringLiteral("double.sdPlugin");
    REQUIRE(extractSdPluginArchive(archive, scratch.path(), target));

    QDir const out(scratch.filePath(target));
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("manifest.json"))));
    REQUIRE(QFileInfo::exists(out.filePath(QStringLiteral("Code/index.html"))));
}

TEST_CASE("extractSdPluginArchive rejects a hostile ZIP64 local-header offset (no over-read)",
          "[plugin-store][security][zip64]") {
    // Regression for a buffer over-read in the central-directory parser: a ZIP64
    // central-directory entry whose local-header offset field is the 0xFFFFFFFF
    // sentinel can carry an arbitrary 64-bit offset in its ZIP64 extra field
    // (header 0x0001). Without a `localOffset >= bufferSize` guard, an offset
    // near UINT64_MAX makes the `lo + 30 > n` bounds check WRAP to a small value
    // that passes, then `rd32(buf + lo)` reads far past the buffer. This fixture
    // (hand-built) sets localOffset to 0xFFFFFFFFFFFFFFF0; the extractor must
    // reject it cleanly (return false), never over-read / crash.
    static constexpr char kHostileZip64B64[] =
        "UEsDBC0AAAAIAAAAAAAAAAAADgAAAAwAAAAfAAAAY29tLmV2aWwuc2RQbHVnaW4vbWFuaWZl"
        "c3QuanNvbqtWCg31dFGyUqpQqgUAUEsBAi0ALQAAAAgAAAAAAAAAAAAOAAAADAAAAB8ADAAA"
        "AAAAAAAAAAAA/////2NvbS5ldmlsLnNkUGx1Z2luL21hbmlmZXN0Lmpzb24BAAgA8P//////"
        "//9QSwUGAAAAAAEAAQBZAAAASwAAAAAA";

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    QString const destDir = scratch.path();
    QString const archive = destDir + QStringLiteral("/hostile.sdPlugin");
    {
        QFile f(archive);
        REQUIRE(f.open(QIODevice::WriteOnly));
        QByteArray const bytes = QByteArray::fromBase64(
            QByteArray::fromRawData(kHostileZip64B64, sizeof(kHostileZip64B64) - 1));
        REQUIRE(f.write(bytes) == bytes.size());
        f.close();
    }

    QString const target = QStringLiteral("com.evil.sdPlugin");
    // Must reject without crashing or extracting anything.
    REQUIRE_FALSE(extractSdPluginArchive(archive, destDir, target));
    REQUIRE_FALSE(QDir(destDir + QStringLiteral("/") + target).exists());
    REQUIRE_FALSE(QDir(destDir + QStringLiteral("/.tmp_") + target).exists());
}
