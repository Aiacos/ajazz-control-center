// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sdplugin_extractor.cpp
 * @brief Implementation of the in-process `.sdPlugin` archive extractor.
 *
 * Uses Qt's QZipReader (private API but stable since Qt 4.7). The
 * matching link target is `Qt6::CorePrivate`.
 */
#include "sdplugin_extractor.hpp"

#include "ajazz/core/logger.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <private/qzipreader_p.h>

namespace ajazz::app {

bool extractSdPluginArchive(QString const& archivePath, QString const& destDir,
                            QString const& targetSubdir) {
    QZipReader zip(archivePath);
    // QZipReader::isReadable() returned spurious false on Qt 6.11.1 even for
    // structurally-valid archives written by the matching QZipWriter; check
    // status() + count() instead which directly probe the central directory.
    if (zip.status() != QZipReader::NoError || zip.count() == 0) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "extract '{}': QZipReader rejected archive (status={}, count={})",
                       archivePath.toStdString(), static_cast<int>(zip.status()),
                       zip.count());
        return false;
    }
    // Stage under a hidden tmp dir adjacent to the final destination so
    // the rename is on the same filesystem (atomic on POSIX, best-effort
    // on Win32). The leading dot keeps it out of any plugin-host scan.
    QString const tmpPath = destDir + QStringLiteral("/.tmp_") + targetSubdir;
    QDir().mkpath(tmpPath);
    // Iterate the central directory ourselves instead of relying on
    // QZipReader::extractAll. On Qt 6.8.3 (CI baseline) extractAll fails
    // when an archive omits explicit directory entries — a common shape
    // for zips produced by tools like Python's zipfile and (per local
    // testing) by QZipWriter itself when only addFile() is used. Qt
    // 6.11.1's extractAll silently mkpath()s missing parents, but we
    // can't rely on that. Mkpath every file's parent dir before writing.
    bool extractOk = true;
    for (auto const& info : zip.fileInfoList()) {
        QString const outPath = tmpPath + QStringLiteral("/") + info.filePath;
        if (info.isDir) {
            QDir().mkpath(outPath);
        } else if (info.isFile) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            QFile out(outPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                extractOk = false;
                break;
            }
            QByteArray const data = zip.fileData(info.filePath);
            if (out.write(data) != data.size()) {
                extractOk = false;
                out.close();
                break;
            }
            out.close();
        }
        // Symlinks intentionally skipped: vendor `.sdPlugin` payloads
        // are flat HTML/JS/PNG trees, never symlinked, and honouring
        // symlinks from an untrusted archive is a security hazard.
    }
    if (!extractOk) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "extract '{}': iteration into '{}' failed",
                       archivePath.toStdString(), tmpPath.toStdString());
        QDir(tmpPath).removeRecursively();
        return false;
    }
    zip.close();

    // Detect single-folder wrapper. If yes, the wrapper itself becomes
    // the source of the rename and the tmp shell is wiped after.
    QDir const tmpDir(tmpPath);
    QStringList const topEntries =
        tmpDir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    QString sourcePath = tmpPath;
    bool stripWrapper = false;
    if (topEntries.size() == 1) {
        QString const sole = tmpDir.filePath(topEntries.first());
        if (QFileInfo(sole).isDir()) {
            sourcePath = sole;
            stripWrapper = true;
        }
    }

    QString const finalPath = destDir + QStringLiteral("/") + targetSubdir;
    // Atomic-ish overwrite: clear anything already at the target so the
    // rename has a free slot. Two cases to handle:
    //   1. A prior extracted directory — sweep with removeRecursively().
    //   2. The archive file itself, when the install path stages the
    //      download as <destDir>/<targetSubdir> (same name we want for
    //      the final directory). QDir::removeRecursively only removes
    //      directories, so we explicitly remove the file first.
    // The .tmp_ stage protects against a half-extracted directory
    // landing under the final name on failure.
    QFileInfo const final(finalPath);
    if (final.exists()) {
        if (final.isDir()) {
            QDir(finalPath).removeRecursively();
        } else {
            QFile::remove(finalPath);
        }
    }
    if (!QDir().rename(sourcePath, finalPath)) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "extract: rename '{}' -> '{}' failed (stripWrapper={})",
                       sourcePath.toStdString(), finalPath.toStdString(),
                       stripWrapper);
        QDir(tmpPath).removeRecursively();
        return false;
    }
    if (stripWrapper) {
        QDir(tmpPath).removeRecursively();
    }
    return true;
}

void extractStandalonePluginArchives(QString const& pluginsDir) {
    QDir const dir(pluginsDir);
    if (!dir.exists()) {
        return;
    }
    QStringList const archives = dir.entryList(
        QStringList{QStringLiteral("*.sdPlugin")}, QDir::Files | QDir::NoDotAndDotDot);
    for (QString const& name : archives) {
        QString const archivePath = dir.filePath(name);
        if (extractSdPluginArchive(archivePath, dir.absolutePath(), name)) {
            QFile::remove(archivePath);
            AJAZZ_LOG_INFO("plugin-catalog", "first-launch extract: '{}'",
                           name.toStdString());
        } else {
            AJAZZ_LOG_WARN("plugin-catalog",
                           "first-launch extract: cannot read '{}' as zip; left in place",
                           name.toStdString());
        }
    }
}

} // namespace ajazz::app
