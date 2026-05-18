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

#include <private/qzipwriter_p.h>

#include <catch2/catch_test_macros.hpp>

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

    REQUIRE_FALSE(extractSdPluginArchive(bogus, scratch.path(),
                                         QStringLiteral("not-a-zip.sdPlugin")));
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
