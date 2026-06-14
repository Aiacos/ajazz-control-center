// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sdplugin_extractor.cpp
 * @brief Implementation of the in-process `.sdPlugin` archive extractor.
 *
 * Self-contained ZIP reader: parses the End-Of-Central-Directory record and the
 * central directory, then raw-inflates each entry via zlib. This replaces Qt's
 * private QZipReader, which extracts ZERO bytes for any entry whose
 * general-purpose flag bit 3 (data descriptor) is set — the local file header
 * carries 0/0 for the compressed/uncompressed sizes and QZipReader::fileData()
 * trusts them. Streaming ZIP writers (Node `archiver`, many `.streamDeckPlugin`
 * packagers) set that bit, so real plugin archives extracted as empty files and
 * their manifests failed to parse. Sourcing sizes + the data offset from the
 * central directory (always authoritative) fixes the whole class.
 */
#include "sdplugin_extractor.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <zlib.h>

namespace ajazz::app {

namespace {

/// Little-endian field readers over a raw byte cursor.
std::uint16_t rd16(unsigned char const* p) {
    return static_cast<std::uint16_t>(p[0]) | static_cast<std::uint16_t>(p[1] << 8);
}
std::uint32_t rd32(unsigned char const* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t rd64(unsigned char const* p) {
    return static_cast<std::uint64_t>(rd32(p)) | (static_cast<std::uint64_t>(rd32(p + 4)) << 32);
}

/// One central-directory record we care about. Sizes are 64-bit to carry ZIP64
/// values (the 32-bit fields are 0xFFFFFFFF sentinels pointing at the extra
/// field — see readZipCentralDirectory).
struct ZipEntry {
    QString name;
    std::uint16_t method{0};      ///< 0=stored, 8=deflate.
    std::uint64_t compSize{0};    ///< Compressed size (central dir = authoritative).
    std::uint64_t uncompSize{0};  ///< Uncompressed size.
    std::uint64_t localOffset{0}; ///< Offset of the local file header.
    bool isDir{false};
};

/// Raw-inflate @p comp (raw DEFLATE, no zlib/gzip wrapper) to exactly
/// @p uncompSize bytes. Returns an empty array on any failure.
QByteArray rawInflate(QByteArray const& comp, std::uint64_t uncompSize) {
    QByteArray out;
    out.resize(static_cast<qsizetype>(uncompSize));
    z_stream s;
    std::memset(&s, 0, sizeof(s));
    // -MAX_WBITS selects a raw DEFLATE stream (no header/trailer).
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) {
        return {};
    }
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(comp.constData()));
    s.avail_in = static_cast<uInt>(comp.size());
    s.next_out = reinterpret_cast<Bytef*>(out.data());
    s.avail_out = static_cast<uInt>(uncompSize);
    int const rc = inflate(&s, Z_FINISH);
    uLong const total = s.total_out;
    inflateEnd(&s);
    if (rc != Z_STREAM_END || total != uncompSize) {
        return {};
    }
    return out;
}

/// Parse the central directory of @p buf into @p out. Returns false on a
/// malformed / unsupported (zip64, encrypted) archive.
bool readZipCentralDirectory(QByteArray const& buf, std::vector<ZipEntry>& out) {
    qsizetype const n = buf.size();
    if (n < 22) {
        return false;
    }
    auto const* d = reinterpret_cast<unsigned char const*>(buf.constData());
    // Locate the End-Of-Central-Directory record (sig 0x06054b50). Scan back
    // from the end across at most the 64KiB max-comment window + the 22-byte
    // record itself.
    qsizetype eocd = -1;
    qsizetype const minPos = std::max<qsizetype>(0, n - (22 + 65535));
    for (qsizetype i = n - 22; i >= minPos; --i) {
        if (rd32(d + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return false;
    }
    std::uint16_t const total = rd16(d + eocd + 10);
    std::uint32_t const cdOffset = rd32(d + eocd + 16);
    std::uint32_t p = cdOffset;
    for (std::uint16_t i = 0; i < total; ++i) {
        if (p + 46 > static_cast<std::uint32_t>(n) || rd32(d + p) != 0x02014b50u) {
            return false;
        }
        ZipEntry e;
        e.method = rd16(d + p + 10);
        std::uint32_t const comp32 = rd32(d + p + 20);
        std::uint32_t const unc32 = rd32(d + p + 24);
        std::uint16_t const fnLen = rd16(d + p + 28);
        std::uint16_t const exLen = rd16(d + p + 30);
        std::uint16_t const cmLen = rd16(d + p + 32);
        std::uint32_t const lo32 = rd32(d + p + 42);
        e.compSize = comp32;
        e.uncompSize = unc32;
        e.localOffset = lo32;
        if (p + 46u + fnLen + exLen > static_cast<std::uint32_t>(n)) {
            return false;
        }
        e.name = QString::fromUtf8(buf.constData() + p + 46, fnLen);
        e.isDir = e.name.endsWith(QLatin1Char('/'));
        // ZIP64: a 0xFFFFFFFF field is a sentinel; the real 64-bit value lives in
        // the extra field (header id 0x0001), present in this fixed order for
        // whichever of {uncompressed, compressed, localOffset} were sentinels.
        // Modern streaming packagers (Node `archiver`) emit this even for tiny
        // files, so it must be handled — not bailed on.
        if (comp32 == 0xFFFFFFFFu || unc32 == 0xFFFFFFFFu || lo32 == 0xFFFFFFFFu) {
            std::uint32_t ep = p + 46u + fnLen; // start of extra field
            std::uint32_t const eend = ep + exLen;
            bool z64ok = false;
            while (ep + 4 <= eend) {
                std::uint16_t const hid = rd16(d + ep);
                std::uint16_t const hsz = rd16(d + ep + 2);
                if (ep + 4u + hsz > eend) {
                    break;
                }
                if (hid == 0x0001u) {
                    std::uint32_t fp = ep + 4;
                    std::uint32_t const fend = ep + 4u + hsz;
                    if (unc32 == 0xFFFFFFFFu && fp + 8 <= fend) {
                        e.uncompSize = rd64(d + fp);
                        fp += 8;
                    }
                    if (comp32 == 0xFFFFFFFFu && fp + 8 <= fend) {
                        e.compSize = rd64(d + fp);
                        fp += 8;
                    }
                    if (lo32 == 0xFFFFFFFFu && fp + 8 <= fend) {
                        e.localOffset = rd64(d + fp);
                        fp += 8;
                    }
                    z64ok = true;
                    break;
                }
                ep += 4u + hsz;
            }
            if (!z64ok) {
                return false; // sentinel without a ZIP64 extra field — malformed
            }
        }
        out.push_back(std::move(e));
        p += 46u + fnLen + exLen + cmLen;
    }
    return true;
}

/// Read + decompress one entry's bytes from the whole-archive buffer. Returns
/// empty for directory entries (and on any structural failure).
QByteArray readZipEntryData(QByteArray const& buf, ZipEntry const& e) {
    if (e.isDir || e.uncompSize == 0) {
        return {};
    }
    auto const n = static_cast<std::uint64_t>(buf.size());
    auto const* d = reinterpret_cast<unsigned char const*>(buf.constData());
    std::uint64_t const lo = e.localOffset;
    // A single plugin entry over ~512 MiB is pathological — refuse rather than
    // attempt a multi-GB allocation (the download cap already bounds the archive,
    // but this guards the in-archive declared sizes too).
    constexpr std::uint64_t kMaxEntryBytes = 512ull * 1024 * 1024;
    if (e.compSize > kMaxEntryBytes || e.uncompSize > kMaxEntryBytes) {
        return {};
    }
    if (lo + 30 > n || rd32(d + lo) != 0x04034b50u) {
        return {};
    }
    // The local header's filename/extra lengths can differ from the central
    // directory's, so the data offset MUST be computed from the local header.
    std::uint16_t const fnLen = rd16(d + lo + 26);
    std::uint16_t const exLen = rd16(d + lo + 28);
    std::uint64_t const dataStart = lo + 30u + fnLen + exLen;
    if (dataStart + e.compSize > n) {
        return {};
    }
    QByteArray const comp(buf.constData() + static_cast<qsizetype>(dataStart),
                          static_cast<qsizetype>(e.compSize));
    if (e.method == 0) {
        return comp; // stored
    }
    if (e.method == 8) {
        return rawInflate(comp, e.uncompSize); // deflate
    }
    return {}; // unsupported compression method
}

} // namespace

bool extractSdPluginArchive(QString const& archivePath,
                            QString const& destDir,
                            QString const& targetSubdir) {
    // Read the whole archive into memory and parse its central directory. The
    // central directory carries authoritative sizes + the data offset even when
    // an entry sets the data-descriptor flag (QZipReader extracted 0 bytes for
    // those — see the file header). Plugin archives are bounded upstream by the
    // download size cap, so a full read is safe.
    QFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::ReadOnly)) {
        AJAZZ_LOG_WARN(
            "plugin-catalog", "extract '{}': cannot open archive", archivePath.toStdString());
        return false;
    }
    QByteArray const buf = archiveFile.readAll();
    archiveFile.close();
    std::vector<ZipEntry> entries;
    if (!readZipCentralDirectory(buf, entries) || entries.empty()) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "extract '{}': not a readable zip (central directory parse failed, "
                       "{} bytes)",
                       archivePath.toStdString(),
                       buf.size());
        return false;
    }
    // Stage under a hidden tmp dir adjacent to the final destination so
    // the rename is on the same filesystem (atomic on POSIX, best-effort
    // on Win32). The leading dot keeps it out of any plugin-host scan.
    QString const tmpPath = destDir + QStringLiteral("/.tmp_") + targetSubdir;
    QDir().mkpath(tmpPath);
    bool extractOk = true;
    QString const rootCanon = QDir::cleanPath(tmpPath) + QStringLiteral("/");
    for (auto const& info : entries) {
        // Zip-slip guard: reject any entry whose normalised destination
        // escapes the staging root. A hostile archive can name an entry
        // "../../../.bashrc" — or an absolute / drive-prefixed path — to
        // overwrite arbitrary files with the user's permissions. This
        // extractor runs on archives downloaded over HTTPS by
        // PluginCatalogModel::install and on any file dropped in the
        // plugins dir, so the input is untrusted.
        QString const outPath = QDir::cleanPath(tmpPath + QStringLiteral("/") + info.name);
        if (info.name.startsWith(QLatin1Char('/')) || info.name.contains(QStringLiteral(":/")) ||
            info.name.contains(QStringLiteral(":\\")) || !outPath.startsWith(rootCanon)) {
            AJAZZ_LOG_WARN("plugin-catalog",
                           "extract '{}': rejecting entry escaping staging dir: '{}'",
                           archivePath.toStdString(),
                           info.name.toStdString());
            extractOk = false;
            break;
        }
        if (info.isDir) {
            QDir().mkpath(outPath);
            continue;
        }
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QByteArray const data = readZipEntryData(buf, info);
        // A non-empty entry that decoded to nothing is a hard failure (corrupt /
        // unsupported method) — do not silently write an empty file the manifest
        // parser would then reject.
        if (info.uncompSize > 0 && data.isEmpty()) {
            AJAZZ_LOG_WARN("plugin-catalog",
                           "extract '{}': failed to decode entry '{}' ({} bytes, method {})",
                           archivePath.toStdString(),
                           info.name.toStdString(),
                           info.uncompSize,
                           info.method);
            extractOk = false;
            break;
        }
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            extractOk = false;
            break;
        }
        if (out.write(data) != data.size()) {
            extractOk = false;
            out.close();
            break;
        }
        out.close();
        // Symlinks intentionally skipped: vendor `.sdPlugin` payloads are flat
        // HTML/JS/PNG trees, never symlinked, and honouring symlinks from an
        // untrusted archive is a security hazard.
    }
    if (!extractOk) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "extract '{}': iteration into '{}' failed",
                       archivePath.toStdString(),
                       tmpPath.toStdString());
        QDir(tmpPath).removeRecursively();
        return false;
    }

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
                       sourcePath.toStdString(),
                       finalPath.toStdString(),
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
    QStringList const archives = dir.entryList(QStringList{QStringLiteral("*.sdPlugin")},
                                               QDir::Files | QDir::NoDotAndDotDot);
    for (QString const& name : archives) {
        QString const archivePath = dir.filePath(name);
        if (extractSdPluginArchive(archivePath, dir.absolutePath(), name)) {
            QFile::remove(archivePath);
            AJAZZ_LOG_INFO("plugin-catalog", "first-launch extract: '{}'", name.toStdString());
        } else {
            AJAZZ_LOG_WARN("plugin-catalog",
                           "first-launch extract: cannot read '{}' as zip; left in place",
                           name.toStdString());
        }
    }
}

} // namespace ajazz::app
